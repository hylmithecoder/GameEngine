//! HTTP + WebSocket front: `/health` for the deployer, `/ws` for editors.
//!
//! Message flow on one connection:
//!   client: hello {protocol, role, pid, token}
//!   server: welcome {protocol, role:"realtime", pid, connection, server}
//!           (or bye {reason, retry:false} + close)
//!   client: request room.join {room, name, color}
//!   server: reply {self, room, seq, fresh, peers[], state[]}; others get peer.joined
//!   client: event op {key, slot, data} | {key, del:true}
//!   server: event op {seq, from, key, slot, data, del} to the whole room
//!   client: event presence {...}  → others get presence {from, data}
//!   client: request room.leave    → others get peer.left
//!   client: event lock.acquire {key} | lock.release {key}
//!   server: event lock {key, holder, queue} to the whole room (holder 0 = free);
//!           the join reply carries locks[] (see locks.rs)
//!
//! LAN: `POST /lan/announce` + `GET /lan/nearby` (token like `/rooms`) are the
//! rendezvous for servers on the caller's own network, see lan.rs.
//!
//! HTTP: `GET /rooms` (Authorization: Bearer <token>) lists the rooms for the
//! Hub's "online workspaces" — name, who is in it, object count, last change,
//! and the published project files (if any). The project-file endpoints
//! (`/files/...`, `/rooms/<room>/files`) are described in files.rs.
//!
//! Every room mutation and the broadcast it causes happen under one lock with
//! non-blocking sends, so all peers see ops in the same order and a join reply
//! always lands before the first op broadcast that follows it.

use crate::config::Config;
use crate::files::{self, Manifest, Store};
use crate::lan;
use crate::locks::Locks;
use crate::persist;
use crate::protocol::{self, Envelope, Kind, PROTOCOL_VERSION};
use crate::room::{OpIn, RoomState, now_unix};
use axum::Router;
use axum::body::Body;
use axum::extract::ws::{CloseFrame, Message, WebSocket, WebSocketUpgrade};
use axum::extract::{ConnectInfo, DefaultBodyLimit, Path, State};
use axum::http::{HeaderMap, StatusCode, header};
use axum::response::{IntoResponse, Json, Response};
use axum::routing::{get, post};
use futures_util::{SinkExt, StreamExt};
use serde_json::{Value, json};
use std::collections::HashMap;
use std::net::{IpAddr, SocketAddr};
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::{Arc, Mutex, MutexGuard};
use std::time::{Duration, Instant};
use tokio::sync::{Notify, mpsc};

pub const VERSION: &str = env!("CARGO_PKG_VERSION");
const OUTBOX: usize = 2048;
const HELLO_TIMEOUT: Duration = Duration::from_secs(10);
const MAX_NAME: usize = 64;
const MAX_LOCKS_PER_PEER: usize = 16;

enum Out {
    Text(String),
    Close(String),
}

struct Peer {
    name: String,
    color: u32,
    presence: Value,
    tx: mpsc::Sender<Out>,
    kick: Arc<Notify>,
}

#[derive(Default)]
struct Room {
    state: RoomState,
    peers: HashMap<u32, Peer>,
    locks: Locks,
    dirty: bool,
    /// Summary of the published project files, for the room list.
    files: Option<FilesSummary>,
}

#[derive(Clone, Copy)]
struct FilesSummary {
    count: usize,
    bytes: u64,
    updated_unix: u64,
}

impl FilesSummary {
    fn of(m: &Manifest) -> Self {
        Self {
            count: m.files.len(),
            bytes: m.total,
            updated_unix: m.updated_unix,
        }
    }
}

impl Room {
    /// Sends to every peer (but `except`); a peer whose outbox is full is too
    /// slow to keep up, so it is dropped rather than stalling the room.
    fn broadcast(&mut self, except: Option<u32>, text: &str) -> Vec<u32> {
        let mut slow = Vec::new();
        for (id, p) in &self.peers {
            if Some(*id) == except {
                continue;
            }
            if p.tx.try_send(Out::Text(text.to_string())).is_err() {
                slow.push(*id);
            }
        }
        slow
    }

    /// Frees `peer`'s locks and announces the new holders to everyone left.
    fn drop_locks(&mut self, peer: u32) -> Vec<u32> {
        let mut slow = Vec::new();
        for key in self.locks.release_peer(peer) {
            let text = protocol::event("lock", self.locks.describe(&key));
            slow.extend(self.broadcast(None, &text));
        }
        slow
    }
}

pub struct AppState {
    pub cfg: Config,
    rooms: Mutex<HashMap<String, Room>>,
    /// None when the files folder could not be created: the endpoints say 503.
    files: Option<Store>,
    next_conn: AtomicU32,
    started: Instant,
    /// LAN servers announced from each public IP (rendezvous, lan.rs).
    pub lan: lan::Directory,
}

impl AppState {
    pub fn new(cfg: Config, saved: HashMap<String, RoomState>) -> Arc<Self> {
        let files_dir = cfg
            .files_dir
            .clone()
            .or_else(|| cfg.data_dir.as_ref().map(|d| d.join("files")))
            .unwrap_or_else(|| {
                static N: AtomicU32 = AtomicU32::new(0);
                std::env::temp_dir().join(format!(
                    "ilmeee-rt-files-{}-{}",
                    std::process::id(),
                    N.fetch_add(1, Ordering::Relaxed)
                ))
            });
        let files = match Store::open(files_dir.clone()) {
            Ok(s) => {
                tracing::info!(dir = ?files_dir, used = s.used_bytes(), "project file store");
                Some(s)
            }
            Err(e) => {
                tracing::warn!(dir = ?files_dir, error = %e, "project files disabled");
                None
            }
        };
        let rooms = saved
            .into_iter()
            .map(|(name, state)| {
                let summary = files
                    .as_ref()
                    .and_then(|f| f.manifest(&name))
                    .map(|m| FilesSummary::of(&m));
                (
                    name,
                    Room {
                        state,
                        files: summary,
                        ..Default::default()
                    },
                )
            })
            .collect();
        Arc::new(Self {
            cfg,
            rooms: Mutex::new(rooms),
            files,
            next_conn: AtomicU32::new(1),
            started: Instant::now(),
            lan: lan::Directory::default(),
        })
    }

    fn rooms(&self) -> MutexGuard<'_, HashMap<String, Room>> {
        self.rooms.lock().unwrap_or_else(|e| e.into_inner())
    }

    /// Writes rooms changed since the last call.
    pub fn persist_dirty(&self) {
        let Some(dir) = &self.cfg.data_dir else {
            return;
        };
        let snapshot: Vec<(String, RoomState)> = {
            let mut rooms = self.rooms();
            rooms
                .iter_mut()
                .filter(|(_, r)| r.dirty)
                .map(|(n, r)| {
                    r.dirty = false;
                    (n.clone(), r.state.clone())
                })
                .collect()
        };
        for (name, state) in snapshot {
            if let Err(e) = persist::save_room(dir, &name, &state) {
                tracing::warn!(room = %name, error = %e, "could not save room");
                if let Some(r) = self.rooms().get_mut(&name) {
                    r.dirty = true; // retry next round
                }
            }
        }
    }

    /// Forgets empty rooms idle for longer than the TTL.
    pub fn sweep_idle(&self) {
        let ttl = self.cfg.room_ttl.as_secs();
        let now = now_unix();
        let expired: Vec<String> = {
            let mut rooms = self.rooms();
            let names: Vec<String> = rooms
                .iter()
                .filter(|(_, r)| {
                    r.peers.is_empty() && now.saturating_sub(r.state.updated_unix) > ttl
                })
                .map(|(n, _)| n.clone())
                .collect();
            for n in &names {
                rooms.remove(n);
            }
            names
        };
        for name in expired {
            tracing::info!(room = %name, "room expired");
            if let Some(dir) = &self.cfg.data_dir {
                persist::remove_room(dir, &name);
            }
            if let Some(f) = &self.files {
                f.remove_manifest(&name);
            }
        }
        if let Some(f) = &self.files {
            let n = f.gc();
            if n > 0 {
                tracing::info!(removed = n, "removed unused project files");
            }
        }
    }

    fn kick_slow(&self, rooms: &mut HashMap<String, Room>, room: &str, slow: Vec<u32>) {
        for id in slow {
            tracing::warn!(peer = id, room, "dropping peer: outbox full");
            if let Some(r) = rooms.get_mut(room)
                && let Some(p) = r.peers.remove(&id)
            {
                p.kick.notify_one();
                let left = protocol::event("peer.left", json!({"id": id, "reason": "too slow"}));
                r.broadcast(None, &left);
                r.drop_locks(id); // a second slow peer is caught on its next send
            }
        }
    }
}

pub fn router(app: Arc<AppState>) -> Router {
    Router::new()
        .route("/", get(index))
        .route("/health", get(health))
        .route("/rooms", get(list_rooms))
        .route(
            "/rooms/{room}/files",
            get(get_manifest)
                .put(put_manifest)
                .layer(DefaultBodyLimit::max(MAX_MANIFEST_BYTES)),
        )
        .route(
            "/files/missing",
            post(files_missing).layer(DefaultBodyLimit::max(MAX_MANIFEST_BYTES)),
        )
        .route("/files/blob/{hash}", get(get_blob).put(put_blob))
        .route(
            "/lan/announce",
            post(lan_announce).layer(DefaultBodyLimit::max(16 << 10)),
        )
        .route("/lan/nearby", get(lan_nearby))
        .route("/ws", get(ws_upgrade))
        .with_state(app)
}

async fn index() -> &'static str {
    "Ilmeee realtime collaboration server. Editors connect to /ws (wss in production).\n"
}

async fn health(State(app): State<Arc<AppState>>) -> Json<Value> {
    let (rooms, peers) = {
        let r = app.rooms();
        (r.len(), r.values().map(|x| x.peers.len()).sum::<usize>())
    };
    Json(json!({
        "status": "ok",
        "version": VERSION,
        "protocol": PROTOCOL_VERSION,
        "rooms": rooms,
        "peers": peers,
        "uptime_secs": app.started.elapsed().as_secs(),
    }))
}

/// Number of rooms (UDP discovery answer).
impl AppState {
    pub fn room_count(&self) -> usize {
        self.rooms().len()
    }
}

/// The address the request came from on the internet. Behind the tunnel /
/// reverse proxy that is the first `X-Forwarded-For` entry (or `X-Real-IP`);
/// a direct connection uses the socket address.
fn client_ip(headers: &HeaderMap, peer: SocketAddr) -> IpAddr {
    let header_ip = |name: &str| {
        headers
            .get(name)
            .and_then(|v| v.to_str().ok())
            .and_then(|v| v.split(',').next())
            .and_then(|v| v.trim().parse::<IpAddr>().ok())
    };
    let ip = header_ip("x-forwarded-for")
        .or_else(|| header_ip("x-real-ip"))
        .unwrap_or(peer.ip());
    // ::ffff:a.b.c.d and a.b.c.d are the same network.
    match ip {
        IpAddr::V6(v6) => v6.to_ipv4_mapped().map(IpAddr::V4).unwrap_or(ip),
        v4 => v4,
    }
}

async fn lan_announce(
    State(app): State<Arc<AppState>>,
    ConnectInfo(peer): ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    body: axum::body::Bytes,
) -> Response {
    if let Err(r) = check_token(&app, &headers) {
        return r;
    }
    let a: lan::Announce = match serde_json::from_slice(&body) {
        Ok(a) => a,
        Err(e) => return http_error(StatusCode::BAD_REQUEST, &format!("bad announcement: {e}")),
    };
    let ip = client_ip(&headers, peer);
    match app.lan.announce(ip, a) {
        Ok(()) => {
            Json(json!({"ok": true, "ttl_secs": lan::ANNOUNCE_TTL.as_secs()})).into_response()
        }
        Err(msg) => http_error(StatusCode::BAD_REQUEST, msg),
    }
}

async fn lan_nearby(
    State(app): State<Arc<AppState>>,
    ConnectInfo(peer): ConnectInfo<SocketAddr>,
    headers: HeaderMap,
) -> Response {
    if let Err(r) = check_token(&app, &headers) {
        return r;
    }
    let ip = client_ip(&headers, peer);
    Json(json!({"hosts": app.lan.nearby(ip)})).into_response()
}

fn http_error(status: StatusCode, msg: &str) -> Response {
    (status, Json(json!({"error": msg}))).into_response()
}

/// The HTTP endpoints need the server token like the WebSocket does.
#[allow(clippy::result_large_err)] // a Response is the natural error for a handler
fn check_token(app: &AppState, headers: &HeaderMap) -> Result<(), Response> {
    if app.cfg.token.is_empty() {
        return Ok(());
    }
    let given = headers
        .get(header::AUTHORIZATION)
        .and_then(|v| v.to_str().ok())
        .and_then(|v| v.strip_prefix("Bearer "))
        .unwrap_or("");
    if protocol::tokens_equal(given, &app.cfg.token) {
        Ok(())
    } else {
        Err(http_error(StatusCode::UNAUTHORIZED, "invalid token"))
    }
}

#[allow(clippy::result_large_err)]
fn file_store(app: &AppState) -> Result<&Store, Response> {
    app.files.as_ref().ok_or_else(|| {
        http_error(
            StatusCode::SERVICE_UNAVAILABLE,
            "project files are disabled on this server",
        )
    })
}

/// Room list for the Hub.
async fn list_rooms(State(app): State<Arc<AppState>>, headers: HeaderMap) -> Response {
    if let Err(r) = check_token(&app, &headers) {
        return r;
    }
    let mut list: Vec<(u64, Value)> = {
        let rooms = app.rooms();
        rooms
            .iter()
            .map(|(name, r)| {
                let mut peers: Vec<(u32, Value)> = r
                    .peers
                    .iter()
                    .map(|(id, p)| (*id, json!({"name": p.name, "color": p.color})))
                    .collect();
                peers.sort_by_key(|(id, _)| *id);
                let objects = r.state.keys.keys().filter(|k| k.starts_with('o')).count();
                (
                    r.state.updated_unix,
                    json!({
                        "name": name,
                        "peers": peers.into_iter().map(|(_, p)| p).collect::<Vec<_>>(),
                        "objects": objects,
                        "seq": r.state.seq,
                        "updated_unix": r.state.updated_unix,
                        "files": r.files.map(|f| json!({
                            "count": f.count,
                            "bytes": f.bytes,
                            "updated_unix": f.updated_unix,
                        })),
                    }),
                )
            })
            .collect()
    };
    list.sort_by_key(|(updated, _)| std::cmp::Reverse(*updated)); // most recently active first
    Json(json!({"rooms": list.into_iter().map(|(_, v)| v).collect::<Vec<_>>()})).into_response()
}

const MAX_MANIFEST_BYTES: usize = 32 << 20;
const MAX_MISSING_QUERY: usize = 200_000;

async fn get_manifest(
    State(app): State<Arc<AppState>>,
    Path(room): Path<String>,
    headers: HeaderMap,
) -> Response {
    if let Err(r) = check_token(&app, &headers) {
        return r;
    }
    let store = match file_store(&app) {
        Ok(s) => s,
        Err(r) => return r,
    };
    if !protocol::valid_room(&room) {
        return http_error(StatusCode::BAD_REQUEST, "bad room name");
    }
    match store.manifest(&room) {
        Some(mut m) => {
            m.linked_dir = None; // the host's folder is nobody else's business
            Json(m).into_response()
        }
        None => http_error(StatusCode::NOT_FOUND, "this workspace has no project files"),
    }
}

async fn put_manifest(
    State(app): State<Arc<AppState>>,
    Path(room): Path<String>,
    ConnectInfo(peer): ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    Json(m): Json<Manifest>,
) -> Response {
    if let Err(r) = check_token(&app, &headers) {
        return r;
    }
    let store = match file_store(&app) {
        Ok(s) => s,
        Err(r) => return r,
    };
    if !protocol::valid_room(&room) {
        return http_error(StatusCode::BAD_REQUEST, "bad room name");
    }
    {
        let rooms = app.rooms();
        if !rooms.contains_key(&room) && rooms.len() >= app.cfg.max_rooms {
            return http_error(
                StatusCode::SERVICE_UNAVAILABLE,
                "server is full (too many rooms)",
            );
        }
    }
    let now = now_unix();
    let cfg = &app.cfg;
    let result = match m.linked_dir.clone() {
        // Serve from the publisher's own folder: only the computer the
        // server runs on may ask (and only a LAN server started that way).
        Some(dir) => {
            if !cfg.allow_linked || !peer.ip().is_loopback() {
                return http_error(
                    StatusCode::FORBIDDEN,
                    "linked project folders are only accepted from this computer",
                );
            }
            store
                .put_linked_manifest(
                    &room,
                    m,
                    std::path::Path::new(&dir),
                    cfg.max_project_files,
                    now,
                )
                .map_err(|e| (vec![], e))
        }
        None => store.put_manifest(&room, m, cfg.max_project_files, cfg.max_project_bytes, now),
    };
    match result {
        Ok(saved) => {
            let summary = FilesSummary::of(&saved);
            let mut rooms = app.rooms();
            let r = rooms.entry(room.clone()).or_default();
            r.files = Some(summary);
            r.state.updated_unix = now; // keeps a file-only room from expiring at once
            r.dirty = true;
            drop(rooms);
            tracing::info!(room = %room, files = summary.count, bytes = summary.bytes, "project files published");
            Json(json!({
                "ok": true,
                "count": summary.count,
                "bytes": summary.bytes,
                "updated_unix": now,
            }))
            .into_response()
        }
        Err((missing, msg)) if !missing.is_empty() => (
            StatusCode::CONFLICT,
            Json(json!({"error": msg, "missing": missing})),
        )
            .into_response(),
        Err((_, msg)) => http_error(StatusCode::BAD_REQUEST, &msg),
    }
}

async fn files_missing(
    State(app): State<Arc<AppState>>,
    headers: HeaderMap,
    Json(body): Json<Value>,
) -> Response {
    if let Err(r) = check_token(&app, &headers) {
        return r;
    }
    let store = match file_store(&app) {
        Ok(s) => s,
        Err(r) => return r,
    };
    let Some(list) = body.get("hashes").and_then(Value::as_array) else {
        return http_error(StatusCode::BAD_REQUEST, "expected {hashes:[...]}");
    };
    if list.len() > MAX_MISSING_QUERY {
        return http_error(StatusCode::BAD_REQUEST, "too many hashes");
    }
    let mut hashes = Vec::with_capacity(list.len());
    for h in list {
        match h.as_str() {
            Some(h) if files::valid_hash(h) => hashes.push(h.to_string()),
            _ => return http_error(StatusCode::BAD_REQUEST, "bad hash"),
        }
    }
    Json(json!({"missing": store.missing(&hashes)})).into_response()
}

async fn get_blob(
    State(app): State<Arc<AppState>>,
    Path(hash): Path<String>,
    headers: HeaderMap,
) -> Response {
    if let Err(r) = check_token(&app, &headers) {
        return r;
    }
    let store = match file_store(&app) {
        Ok(s) => s,
        Err(r) => return r,
    };
    let Some(path) = store.blob_file(&hash) else {
        return http_error(StatusCode::NOT_FOUND, "no such file");
    };
    let file = match tokio::fs::File::open(&path).await {
        Ok(f) => f,
        Err(_) => return http_error(StatusCode::NOT_FOUND, "no such file"),
    };
    let len = file.metadata().await.map(|m| m.len()).unwrap_or(0);
    (
        [
            (header::CONTENT_TYPE, "application/octet-stream".to_string()),
            (header::CONTENT_LENGTH, len.to_string()),
            // Content-addressed: a hash never changes meaning.
            (
                header::CACHE_CONTROL,
                "private, max-age=31536000, immutable".to_string(),
            ),
        ],
        Body::from_stream(tokio_util::io::ReaderStream::new(file)),
    )
        .into_response()
}

/// Streams the upload to a temp file while hashing it; stored only if the
/// content matches the hash in the URL.
async fn put_blob(
    State(app): State<Arc<AppState>>,
    Path(hash): Path<String>,
    headers: HeaderMap,
    body: Body,
) -> Response {
    use tokio::io::AsyncWriteExt;
    if let Err(r) = check_token(&app, &headers) {
        return r;
    }
    let store = match file_store(&app) {
        Ok(s) => s,
        Err(r) => return r,
    };
    if !files::valid_hash(&hash) {
        return http_error(StatusCode::BAD_REQUEST, "bad hash");
    }
    if store.has_blob(&hash) {
        return Json(json!({"ok": true, "stored": false})).into_response();
    }
    let max = app.cfg.max_blob_bytes;
    let too_large = || {
        http_error(
            StatusCode::PAYLOAD_TOO_LARGE,
            &format!("file too large (max {max} bytes)"),
        )
    };
    let declared = headers
        .get(header::CONTENT_LENGTH)
        .and_then(|v| v.to_str().ok())
        .and_then(|v| v.parse::<u64>().ok())
        .unwrap_or(0);
    if declared > max {
        return too_large();
    }
    if store.used_bytes().saturating_add(declared) > app.cfg.max_storage_bytes {
        return http_error(StatusCode::INSUFFICIENT_STORAGE, "server storage is full");
    }
    let tmp = store.temp_path();
    let mut file = match tokio::fs::File::create(&tmp).await {
        Ok(f) => f,
        Err(e) => return http_error(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()),
    };
    let mut hasher = files::Hasher::default();
    let mut len: u64 = 0;
    let mut stream = body.into_data_stream();
    let failed = loop {
        match stream.next().await {
            None => break None,
            Some(Err(e)) => break Some(http_error(StatusCode::BAD_REQUEST, &e.to_string())),
            Some(Ok(chunk)) => {
                len += chunk.len() as u64;
                if len > max {
                    break Some(too_large());
                }
                hasher.update(&chunk);
                if let Err(e) = file.write_all(&chunk).await {
                    break Some(http_error(
                        StatusCode::INTERNAL_SERVER_ERROR,
                        &e.to_string(),
                    ));
                }
            }
        }
    };
    let flushed = file.flush().await;
    drop(file);
    if let Some(r) = failed {
        let _ = tokio::fs::remove_file(&tmp).await;
        return r;
    }
    if let Err(e) = flushed {
        let _ = tokio::fs::remove_file(&tmp).await;
        return http_error(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string());
    }
    match store.commit_blob(&tmp, &hash, &hasher.finish(), len) {
        Ok(()) => (
            StatusCode::CREATED,
            Json(json!({"ok": true, "stored": true})),
        )
            .into_response(),
        Err(e) => http_error(StatusCode::BAD_REQUEST, &e),
    }
}

async fn ws_upgrade(ws: WebSocketUpgrade, State(app): State<Arc<AppState>>) -> Response {
    let max = app.cfg.max_message_bytes;
    ws.max_message_size(max)
        .max_frame_size(max)
        .on_upgrade(move |socket| connection(socket, app))
        .into_response()
}

struct Conn {
    id: u32,
    authed: bool,
    room: Option<String>,
    tx: mpsc::Sender<Out>,
    kick: Arc<Notify>,
}

impl Conn {
    fn send(&self, text: String) {
        let _ = self.tx.try_send(Out::Text(text));
    }
    fn refuse(&self, reason: &str) {
        tracing::info!(conn = self.id, reason, "refusing connection");
        self.send(protocol::event(
            "bye",
            json!({"reason": reason, "retry": false}),
        ));
        let _ = self.tx.try_send(Out::Close(reason.to_string()));
    }
}

async fn connection(socket: WebSocket, app: Arc<AppState>) {
    let id = app.next_conn.fetch_add(1, Ordering::Relaxed);
    let (mut sink, mut stream) = socket.split();
    let (tx, mut rx) = mpsc::channel::<Out>(OUTBOX);

    let writer = tokio::spawn(async move {
        while let Some(out) = rx.recv().await {
            match out {
                Out::Text(t) => {
                    if sink.send(Message::Text(t.into())).await.is_err() {
                        break;
                    }
                }
                Out::Close(reason) => {
                    let mut reason = reason;
                    reason.truncate(120); // close reason must fit in a control frame
                    let _ = sink
                        .send(Message::Close(Some(CloseFrame {
                            code: 1008,
                            reason: reason.into(),
                        })))
                        .await;
                    break;
                }
            }
        }
        let _ = sink.close().await;
    });

    let mut conn = Conn {
        id,
        authed: false,
        room: None,
        tx,
        kick: Arc::new(Notify::new()),
    };
    let hello_deadline = tokio::time::sleep(HELLO_TIMEOUT);
    tokio::pin!(hello_deadline);

    loop {
        let kick = conn.kick.clone();
        let msg = tokio::select! {
            m = stream.next() => m,
            _ = kick.notified() => {
                let _ = conn.tx.try_send(Out::Close("too slow".into()));
                break;
            }
            _ = &mut hello_deadline, if !conn.authed => {
                conn.refuse("handshake timeout");
                break;
            }
        };
        match msg {
            Some(Ok(Message::Text(text))) => {
                if !handle_text(&app, &mut conn, text.as_str()) {
                    break;
                }
            }
            Some(Ok(Message::Binary(_))) => {
                if !conn.authed {
                    conn.refuse("handshake required before binary data");
                    break;
                }
                // No binary messages in protocol 1; ignore rather than drop the peer.
            }
            Some(Ok(Message::Close(_))) | None | Some(Err(_)) => break,
            Some(Ok(_)) => {} // ping/pong: answered by the websocket layer
        }
    }

    leave_room(&app, &mut conn, "disconnected");
    drop(conn);
    let _ = writer.await;
}

/// Returns false when the connection must be closed.
fn handle_text(app: &Arc<AppState>, conn: &mut Conn, text: &str) -> bool {
    let env = match protocol::decode(text) {
        Ok(e) => e,
        Err(e) => {
            if !conn.authed {
                conn.refuse(&format!("malformed handshake: {e}"));
                return false;
            }
            tracing::debug!(conn = conn.id, error = %e, "dropping malformed message");
            return true;
        }
    };

    if env.ty == "bye" {
        return true; // the close frame follows
    }

    if !conn.authed {
        if env.ty != "hello" || env.kind != Kind::Event {
            conn.refuse(&format!("handshake required before '{}'", env.ty));
            return false;
        }
        return hello(app, conn, &env);
    }

    match (&env.kind, env.ty.as_str()) {
        (Kind::Request(rid), "room.join") => join(app, conn, *rid, &env.payload),
        (Kind::Request(rid), "room.leave") => {
            leave_room(app, conn, "left");
            conn.send(protocol::reply("room.leave", *rid, json!({})));
        }
        (Kind::Request(rid), "ping") => {
            conn.send(protocol::reply("ping", *rid, json!({"unix": now_unix()})));
        }
        (Kind::Request(rid), other) => {
            conn.send(protocol::fail(
                other,
                *rid,
                &format!("unknown request '{other}'"),
            ));
        }
        (Kind::Event, "op") => op(app, conn, &env.payload),
        (Kind::Event, "presence") => presence(app, conn, env.payload),
        (Kind::Event, "lock.acquire") => lock(app, conn, &env.payload, true),
        (Kind::Event, "lock.release") => lock(app, conn, &env.payload, false),
        (Kind::Event, other) => tracing::debug!(conn = conn.id, ty = other, "ignoring event"),
        (Kind::Response(_), _) => {} // the server never sends requests
    }
    true
}

fn hello(app: &Arc<AppState>, conn: &mut Conn, env: &Envelope) -> bool {
    let p = &env.payload;
    let proto = p.get("protocol").and_then(Value::as_i64).unwrap_or(0);
    if proto != PROTOCOL_VERSION {
        conn.refuse(&format!(
            "protocol {proto} not supported, server speaks {PROTOCOL_VERSION}"
        ));
        return false;
    }
    let token = p.get("token").and_then(Value::as_str).unwrap_or("");
    if !app.cfg.token.is_empty() && !protocol::tokens_equal(token, &app.cfg.token) {
        conn.refuse("invalid token");
        return false;
    }
    conn.authed = true;
    let role = p.get("role").and_then(Value::as_str).unwrap_or("unknown");
    tracing::info!(conn = conn.id, role, "client connected");
    conn.send(protocol::event(
        "welcome",
        json!({
            "protocol": PROTOCOL_VERSION,
            "role": "realtime",
            "pid": std::process::id(),
            "connection": conn.id,
            "server": format!("ilmeee-realtime/{VERSION}"),
        }),
    ));
    true
}

fn peer_json(id: u32, p: &Peer) -> Value {
    json!({"id": id, "name": p.name, "color": p.color, "presence": p.presence})
}

fn join(app: &Arc<AppState>, conn: &mut Conn, rid: u64, p: &Value) {
    let room = p
        .get("room")
        .and_then(Value::as_str)
        .unwrap_or("")
        .to_string();
    if !protocol::valid_room(&room) {
        conn.send(protocol::fail(
            "room.join",
            rid,
            "room name must be 1-64 of A-Z a-z 0-9 _ - .",
        ));
        return;
    }
    let mut name: String = p
        .get("name")
        .and_then(Value::as_str)
        .unwrap_or("")
        .trim()
        .chars()
        .filter(|c| !c.is_control())
        .take(MAX_NAME)
        .collect();
    if name.is_empty() {
        name = format!("user-{}", conn.id);
    }
    let color = p.get("color").and_then(Value::as_u64).unwrap_or(0x4FC3F7) as u32;

    if conn.room.is_some() {
        leave_room(app, conn, "switched room");
    }

    let mut rooms = app.rooms();
    if !rooms.contains_key(&room) && rooms.len() >= app.cfg.max_rooms {
        conn.send(protocol::fail(
            "room.join",
            rid,
            "server is full (too many rooms)",
        ));
        return;
    }
    let r = rooms.entry(room.clone()).or_default();
    if r.peers.len() >= app.cfg.max_peers_per_room {
        conn.send(protocol::fail("room.join", rid, "room is full"));
        return;
    }
    let peers: Vec<Value> = r.peers.iter().map(|(id, p)| peer_json(*id, p)).collect();
    let fresh = r.state.keys.is_empty();
    conn.send(protocol::reply(
        "room.join",
        rid,
        json!({
            "self": conn.id,
            "room": room,
            "seq": r.state.seq,
            "fresh": fresh,
            "peers": peers,
            "state": r.state.replay(),
            "locks": r.locks.all(),
        }),
    ));
    let peer = Peer {
        name: name.clone(),
        color,
        presence: Value::Null,
        tx: conn.tx.clone(),
        kick: conn.kick.clone(),
    };
    let joined = protocol::event("peer.joined", peer_json(conn.id, &peer));
    r.peers.insert(conn.id, peer);
    r.state.updated_unix = now_unix();
    let slow = r.broadcast(Some(conn.id), &joined);
    app.kick_slow(&mut rooms, &room, slow);
    tracing::info!(conn = conn.id, room = %room, name = %name, "joined");
    conn.room = Some(room);
}

fn leave_room(app: &Arc<AppState>, conn: &mut Conn, reason: &str) {
    let Some(room) = conn.room.take() else { return };
    let mut rooms = app.rooms();
    let Some(r) = rooms.get_mut(&room) else {
        return;
    };
    if r.peers.remove(&conn.id).is_none() {
        return; // already dropped (too slow)
    }
    r.state.updated_unix = now_unix();
    let left = protocol::event("peer.left", json!({"id": conn.id, "reason": reason}));
    let mut slow = r.broadcast(None, &left);
    slow.extend(r.drop_locks(conn.id));
    app.kick_slow(&mut rooms, &room, slow);
    tracing::info!(conn = conn.id, room = %room, reason, "left");
}

fn op(app: &Arc<AppState>, conn: &mut Conn, p: &Value) {
    let Some(room) = conn.room.clone() else {
        conn.send(protocol::event(
            "notice",
            json!({"level": "warn", "message": "op ignored: join a room first"}),
        ));
        return;
    };
    let parsed = match OpIn::parse(p) {
        Ok(o) => o,
        Err(e) => {
            conn.send(protocol::event(
                "notice",
                json!({"level": "warn", "message": e}),
            ));
            return;
        }
    };
    let mut rooms = app.rooms();
    let Some(r) = rooms.get_mut(&room) else {
        return;
    };
    if !r.peers.contains_key(&conn.id) {
        return;
    }
    let max_keys = app.cfg.max_keys_per_room;
    match r.state.apply(conn.id, parsed, max_keys) {
        Ok(out) => {
            r.dirty = true;
            let text = protocol::event("op", out);
            let slow = r.broadcast(None, &text);
            app.kick_slow(&mut rooms, &room, slow);
        }
        Err(e) => {
            drop(rooms);
            conn.send(protocol::event(
                "notice",
                json!({"level": "error", "message": e}),
            ));
        }
    }
}

fn presence(app: &Arc<AppState>, conn: &mut Conn, data: Value) {
    let Some(room) = conn.room.clone() else {
        return;
    };
    if data.to_string().len() > app.cfg.max_presence_bytes {
        conn.send(protocol::event(
            "notice",
            json!({"level": "warn", "message": "presence too large"}),
        ));
        return;
    }
    let mut rooms = app.rooms();
    let Some(r) = rooms.get_mut(&room) else {
        return;
    };
    let Some(me) = r.peers.get_mut(&conn.id) else {
        return;
    };
    me.presence = data.clone();
    let text = protocol::event("presence", json!({"from": conn.id, "data": data}));
    let slow = r.broadcast(Some(conn.id), &text);
    app.kick_slow(&mut rooms, &room, slow);
}

fn lock(app: &Arc<AppState>, conn: &mut Conn, p: &Value, acquire: bool) {
    let Some(room) = conn.room.clone() else {
        return;
    };
    let key = p.get("key").and_then(Value::as_str).unwrap_or("");
    let mut rooms = app.rooms();
    let Some(r) = rooms.get_mut(&room) else {
        return;
    };
    if !r.peers.contains_key(&conn.id) {
        return;
    }
    let changed = if acquire {
        match r.locks.acquire(key, conn.id, MAX_LOCKS_PER_PEER) {
            Ok(c) => c,
            Err(e) => {
                drop(rooms);
                conn.send(protocol::event(
                    "notice",
                    json!({"level": "warn", "message": e}),
                ));
                return;
            }
        }
    } else {
        r.locks.release(key, conn.id)
    };
    // Unchanged: still tell the asker, its view may be stale (e.g. after a reconnect).
    let text = protocol::event("lock", r.locks.describe(key));
    if changed {
        let slow = r.broadcast(None, &text);
        app.kick_slow(&mut rooms, &room, slow);
    } else {
        drop(rooms);
        conn.send(text);
    }
}

/// Saves dirty rooms and expires idle ones until the process stops.
pub async fn housekeeping(app: Arc<AppState>) {
    let mut tick = tokio::time::interval(app.cfg.persist_interval);
    tick.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Delay);
    let mut rounds: u64 = 0;
    loop {
        tick.tick().await;
        let a = app.clone();
        let sweep = rounds.is_multiple_of(60);
        let _ = tokio::task::spawn_blocking(move || {
            a.persist_dirty();
            if sweep {
                a.sweep_idle();
                a.lan.sweep();
            }
        })
        .await;
        rounds += 1;
    }
}

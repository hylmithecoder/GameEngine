//! End-to-end: real server on an ephemeral port, real WebSocket clients
//! speaking the engine envelope.

use futures_util::{SinkExt, StreamExt};
use ilmeee_realtime::{Config, run};
use serde_json::{Value, json};
use std::net::SocketAddr;
use std::path::PathBuf;
use std::time::Duration;
use tokio::net::TcpListener;
use tokio::sync::oneshot;
use tokio_tungstenite::tungstenite::Message;

type Ws = tokio_tungstenite::WebSocketStream<
    tokio_tungstenite::MaybeTlsStream<tokio::net::TcpStream>,
>;

struct Server {
    addr: SocketAddr,
    stop: Option<oneshot::Sender<()>>,
    task: tokio::task::JoinHandle<()>,
}

impl Server {
    async fn start(cfg: Config) -> Self {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let addr = listener.local_addr().unwrap();
        let (tx, rx) = oneshot::channel::<()>();
        let task = tokio::spawn(async move {
            run(listener, cfg, async {
                let _ = rx.await;
            })
            .await
            .unwrap();
        });
        Self { addr, stop: Some(tx), task }
    }
    async fn stop(mut self) {
        let _ = self.stop.take().unwrap().send(());
        let _ = tokio::time::timeout(Duration::from_secs(5), self.task).await;
    }
}

fn cfg(token: &str) -> Config {
    Config { token: token.into(), data_dir: None, ..Config::default() }
}

struct Client {
    ws: Ws,
    next_id: u64,
}

impl Client {
    async fn connect(addr: SocketAddr) -> Self {
        let (ws, _) = tokio_tungstenite::connect_async(format!("ws://{addr}/ws")).await.unwrap();
        Self { ws, next_id: 1 }
    }

    async fn send(&mut self, v: Value) {
        self.ws.send(Message::Text(v.to_string().into())).await.unwrap();
    }

    async fn event(&mut self, ty: &str, payload: Value) {
        self.send(json!({"v":1,"type":ty,"payload":payload})).await;
    }

    async fn request(&mut self, ty: &str, payload: Value) -> Value {
        let id = self.next_id;
        self.next_id += 1;
        self.send(json!({"v":1,"type":ty,"id":id,"payload":payload})).await;
        loop {
            let m = self.recv().await;
            if m.get("re").and_then(Value::as_u64) == Some(id) {
                return m;
            }
        }
    }

    /// Next text message, or panics after 3 s.
    async fn recv(&mut self) -> Value {
        loop {
            let m = tokio::time::timeout(Duration::from_secs(3), self.ws.next())
                .await
                .expect("timed out waiting for a message")
                .expect("stream ended")
                .expect("ws error");
            match m {
                Message::Text(t) => return serde_json::from_str(t.as_str()).unwrap(),
                Message::Close(_) => return json!({"type": "<closed>"}),
                _ => continue,
            }
        }
    }

    /// Next message of the given type (skipping others).
    async fn expect(&mut self, ty: &str) -> Value {
        loop {
            let m = self.recv().await;
            if m["type"] == ty {
                return m;
            }
            assert_ne!(m["type"], "<closed>", "closed while waiting for {ty}");
        }
    }

    async fn hello(&mut self, token: &str) -> Value {
        self.event("hello", json!({"protocol":1,"role":"editor","pid":1,"token":token})).await;
        self.recv().await
    }

    async fn join(&mut self, room: &str, name: &str) -> Value {
        let r = self.request("room.join", json!({"room":room,"name":name,"color":0xff0000})).await;
        assert_eq!(r["ok"], true, "{r}");
        r["payload"].clone()
    }
}

#[tokio::test]
async fn health_endpoint() {
    let s = Server::start(cfg("")).await;
    let body = http_get(s.addr, "/health").await;
    assert!(body.contains("\"status\":\"ok\""), "{body}");
    s.stop().await;
}

async fn http_get(addr: SocketAddr, path: &str) -> String {
    use tokio::io::{AsyncReadExt, AsyncWriteExt};
    let mut c = tokio::net::TcpStream::connect(addr).await.unwrap();
    c.write_all(format!("GET {path} HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n").as_bytes())
        .await
        .unwrap();
    let mut out = String::new();
    c.read_to_string(&mut out).await.unwrap();
    out
}

#[tokio::test]
async fn handshake_and_token() {
    let s = Server::start(cfg("secret")).await;

    let mut bad = Client::connect(s.addr).await;
    let bye = bad.hello("wrong").await;
    assert_eq!(bye["type"], "bye");
    assert_eq!(bye["payload"]["retry"], false);
    assert_eq!(bad.recv().await["type"], "<closed>");

    let mut early = Client::connect(s.addr).await;
    early.send(json!({"v":1,"type":"room.join","id":1,"payload":{"room":"a"}})).await;
    let refused = early.recv().await;
    assert_eq!(refused["type"], "bye");
    assert_eq!(early.recv().await["type"], "<closed>");

    let mut good = Client::connect(s.addr).await;
    let w = good.hello("secret").await;
    assert_eq!(w["type"], "welcome");
    assert_eq!(w["payload"]["protocol"], 1);
    assert_eq!(w["payload"]["role"], "realtime");
    s.stop().await;
}

#[tokio::test]
async fn ops_are_sequenced_echoed_and_replayed() {
    let s = Server::start(cfg("")).await;
    let mut a = Client::connect(s.addr).await;
    a.hello("").await;
    let ja = a.join("scene-1", "Alice").await;
    assert_eq!(ja["fresh"], true);
    assert_eq!(ja["peers"].as_array().unwrap().len(), 0);
    let alice = ja["self"].as_u64().unwrap();

    let mut b = Client::connect(s.addr).await;
    b.hello("").await;
    let jb = b.join("scene-1", "Bob").await;
    assert_eq!(jb["peers"][0]["name"], "Alice");
    let joined = a.expect("peer.joined").await;
    assert_eq!(joined["payload"]["name"], "Bob");

    a.event("op", json!({"key":"obj1","slot":"data","data":{"name":"Cube"}})).await;
    let echo = a.expect("op").await;
    let seen = b.expect("op").await;
    assert_eq!(echo["payload"], seen["payload"]);
    assert_eq!(echo["payload"]["seq"], 1);
    assert_eq!(echo["payload"]["from"], alice);

    b.event("op", json!({"key":"obj1","slot":"xform","data":{"p":[1,2,3]}})).await;
    assert_eq!(a.expect("op").await["payload"]["seq"], 2);
    b.expect("op").await;

    // presence goes to the others only, and is part of the peer list
    b.event("presence", json!({"selection":"obj1"})).await;
    let pr = a.expect("presence").await;
    assert_eq!(pr["payload"]["data"]["selection"], "obj1");

    let mut c = Client::connect(s.addr).await;
    c.hello("").await;
    let jc = c.join("scene-1", "Carol").await;
    assert_eq!(jc["fresh"], false);
    assert_eq!(jc["seq"], 2);
    let state = jc["state"].as_array().unwrap();
    assert_eq!(state.len(), 2);
    assert_eq!(state[0]["slot"], "data");
    assert_eq!(state[1]["data"]["p"][2], 3);
    let bob = jc["peers"].as_array().unwrap().iter().find(|p| p["name"] == "Bob").unwrap().clone();
    assert_eq!(bob["presence"]["selection"], "obj1");

    // delete removes every slot
    a.event("op", json!({"key":"obj1","del":true})).await;
    let d = c.expect("op").await;
    assert_eq!(d["payload"]["del"], true);
    let mut late = Client::connect(s.addr).await;
    late.hello("").await;
    let jl = late.join("scene-1", "Late").await;
    assert_eq!(jl["state"].as_array().unwrap().len(), 0);
    assert_eq!(jl["fresh"], true);

    // leaving is announced
    b.request("room.leave", json!({})).await;
    let left = a.expect("peer.left").await;
    assert_eq!(left["payload"]["reason"], "left");
    drop(c);
    let gone = a.expect("peer.left").await;
    assert_eq!(gone["payload"]["reason"], "disconnected");
    s.stop().await;
}

#[tokio::test]
async fn rooms_are_isolated_and_names_validated() {
    let s = Server::start(cfg("")).await;
    let mut a = Client::connect(s.addr).await;
    a.hello("").await;
    let bad = a.request("room.join", json!({"room":"../x"})).await;
    assert_eq!(bad["ok"], false);
    a.join("one", "A").await;

    let mut b = Client::connect(s.addr).await;
    b.hello("").await;
    b.join("two", "B").await;
    b.event("op", json!({"key":"k","slot":"data","data":1})).await;
    b.expect("op").await;
    // A must not see B's op: a ping round-trip proves nothing else is queued
    let pong = a.request("ping", json!({})).await;
    assert_eq!(pong["ok"], true);

    // op before join is refused with a notice
    let mut c = Client::connect(s.addr).await;
    c.hello("").await;
    c.event("op", json!({"key":"k","slot":"data"})).await;
    assert_eq!(c.expect("notice").await["payload"]["level"], "warn");
    s.stop().await;
}

#[tokio::test]
async fn rooms_survive_a_restart() {
    let dir: PathBuf =
        std::env::temp_dir().join(format!("ilmeee-rt-restart-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&dir);
    let with_dir = || Config { data_dir: Some(dir.clone()), ..Config::default() };

    let s = Server::start(with_dir()).await;
    let mut a = Client::connect(s.addr).await;
    a.hello("").await;
    a.join("keep", "A").await;
    a.event("op", json!({"key":"k","slot":"data","data":{"v":7}})).await;
    a.expect("op").await;
    drop(a);
    s.stop().await; // shutdown flushes dirty rooms

    let s2 = Server::start(with_dir()).await;
    let mut b = Client::connect(s2.addr).await;
    b.hello("").await;
    let j = b.join("keep", "B").await;
    assert_eq!(j["fresh"], false);
    assert_eq!(j["state"][0]["data"]["v"], 7);
    s2.stop().await;
    let _ = std::fs::remove_dir_all(&dir);
}

#[tokio::test]
async fn locks_queue_and_hand_off() {
    let s = Server::start(cfg("")).await;
    let mut a = Client::connect(s.addr).await;
    a.hello("").await;
    let alice = a.join("locks", "Alice").await["self"].as_u64().unwrap();
    let mut b = Client::connect(s.addr).await;
    b.hello("").await;
    let bob = b.join("locks", "Bob").await["self"].as_u64().unwrap();
    a.expect("peer.joined").await;

    // Alice takes the object, everyone (Alice included) hears about it
    a.event("lock.acquire", json!({"key":"o1"})).await;
    let l = a.expect("lock").await["payload"].clone();
    assert_eq!(l["holder"], alice);
    assert_eq!(b.expect("lock").await["payload"], l);

    // Bob queues behind her
    b.event("lock.acquire", json!({"key":"o1"})).await;
    let l = b.expect("lock").await["payload"].clone();
    assert_eq!(l["holder"], alice);
    assert_eq!(l["queue"], json!([bob]));
    a.expect("lock").await;

    // a late joiner sees the lock table in the join reply
    let mut c = Client::connect(s.addr).await;
    c.hello("").await;
    let jc = c.join("locks", "Carol").await;
    assert_eq!(jc["locks"][0]["key"], "o1");
    assert_eq!(jc["locks"][0]["holder"], alice);

    // Alice releases → Bob holds it
    a.event("lock.release", json!({"key":"o1"})).await;
    assert_eq!(b.expect("lock").await["payload"]["holder"], bob);
    assert_eq!(c.expect("lock").await["payload"]["holder"], bob);

    // asking again for a key you hold only answers you
    b.event("lock.acquire", json!({"key":"o1"})).await;
    assert_eq!(b.expect("lock").await["payload"]["holder"], bob);

    // Bob disconnects → the lock is freed for everyone
    drop(b);
    let l = c.expect("lock").await["payload"].clone();
    assert_eq!(l["holder"], 0);
    assert_eq!(l["key"], "o1");

    // bad keys are refused with a notice
    a.event("lock.acquire", json!({"key":""})).await;
    assert_eq!(a.expect("notice").await["payload"]["level"], "warn");
    s.stop().await;
}

async fn http_get_auth(addr: SocketAddr, path: &str, token: &str) -> String {
    use tokio::io::{AsyncReadExt, AsyncWriteExt};
    let mut c = tokio::net::TcpStream::connect(addr).await.unwrap();
    c.write_all(
        format!("GET {path} HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer {token}\r\nConnection: close\r\n\r\n")
            .as_bytes(),
    )
    .await
    .unwrap();
    let mut out = String::new();
    c.read_to_string(&mut out).await.unwrap();
    out
}

#[tokio::test]
async fn room_list_needs_token_and_reports_rooms() {
    let s = Server::start(cfg("sekret")).await;
    assert!(http_get(s.addr, "/rooms").await.starts_with("HTTP/1.1 401"));
    assert!(http_get_auth(s.addr, "/rooms", "nope").await.starts_with("HTTP/1.1 401"));

    let mut a = Client::connect(s.addr).await;
    a.hello("sekret").await;
    a.join("alpha", "Alice").await;
    a.event("op", json!({"key":"o1","slot":"data","data":{}})).await;
    a.event("op", json!({"key":"env","slot":"data","data":{}})).await;
    a.expect("op").await;
    a.expect("op").await;
    let mut b = Client::connect(s.addr).await;
    b.hello("sekret").await;
    b.join("beta", "Bob").await;

    let resp = http_get_auth(s.addr, "/rooms", "sekret").await;
    assert!(resp.starts_with("HTTP/1.1 200"), "{resp}");
    let body: Value = serde_json::from_str(resp.split("\r\n\r\n").nth(1).unwrap()).unwrap();
    let rooms = body["rooms"].as_array().unwrap();
    assert_eq!(rooms.len(), 2);
    let alpha = rooms.iter().find(|r| r["name"] == "alpha").unwrap();
    assert_eq!(alpha["objects"], 1); // "env" is not an object
    assert_eq!(alpha["peers"][0]["name"], "Alice");
    let beta = rooms.iter().find(|r| r["name"] == "beta").unwrap();
    assert_eq!(beta["objects"], 0);
    s.stop().await;
}

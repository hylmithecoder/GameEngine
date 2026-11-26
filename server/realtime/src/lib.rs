//! Ilmeee realtime collaboration server.
//!
//! Several editors join the same room and edit one scene together. The server
//! is a sequencer + last-writer-wins store and knows nothing about scenes; see
//! `room.rs` for the model and `server.rs` for the wire flow. The client lives
//! in the engine: include/core_engine/net/CollabClient.hpp.

pub mod config;
pub mod files;
pub mod lan;
pub mod locks;
pub mod persist;
pub mod protocol;
pub mod room;
pub mod server;

use std::future::Future;
use std::sync::Arc;
use tokio::net::TcpListener;

pub use config::Config;
pub use server::AppState;

/// Loads saved rooms, serves until `shutdown` resolves, then saves again.
pub async fn run(
    listener: TcpListener,
    cfg: Config,
    shutdown: impl Future<Output = ()> + Send + 'static,
) -> std::io::Result<()> {
    let saved = cfg
        .data_dir
        .as_deref()
        .map(persist::load_rooms)
        .unwrap_or_default();
    if !saved.is_empty() {
        tracing::info!(rooms = saved.len(), "restored rooms from disk");
    }
    let discovery = cfg.discovery_port;
    let app = AppState::new(cfg, saved);
    let keeper = tokio::spawn(server::housekeeping(app.clone()));
    // LAN servers answer UDP discovery probes (lan.rs); off on the public one.
    let responder = (discovery != 0).then(|| {
        let a = app.clone();
        let info = lan::ServerInfo {
            id: a.cfg.server_id.clone(),
            name: a.cfg.server_name.clone(),
            port: listener
                .local_addr()
                .map(|x| x.port())
                .unwrap_or(a.cfg.port),
            auth: !a.cfg.token.is_empty(),
        };
        tokio::spawn(async move {
            let rooms = {
                let a = a.clone();
                move || a.room_count()
            };
            if let Err(e) = lan::serve_discovery(discovery, info, rooms).await {
                tracing::warn!(error = %e, "LAN discovery disabled");
            }
        })
    });
    let result = axum::serve(
        listener,
        server::router(app.clone()).into_make_service_with_connect_info::<std::net::SocketAddr>(),
    )
    .with_graceful_shutdown(shutdown)
    .await;
    keeper.abort();
    if let Some(r) = responder {
        r.abort();
    }
    let a: Arc<AppState> = app;
    let _ = tokio::task::spawn_blocking(move || a.persist_dirty()).await;
    result
}

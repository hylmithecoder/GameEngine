//! Ilmeee realtime collaboration server.
//!
//! Several editors join the same room and edit one scene together. The server
//! is a sequencer + last-writer-wins store and knows nothing about scenes; see
//! `room.rs` for the model and `server.rs` for the wire flow. The client lives
//! in the engine: include/core_engine/net/CollabClient.hpp.

pub mod config;
pub mod files;
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
    let app = AppState::new(cfg, saved);
    let keeper = tokio::spawn(server::housekeeping(app.clone()));
    let result = axum::serve(listener, server::router(app.clone()))
        .with_graceful_shutdown(shutdown)
        .await;
    keeper.abort();
    let a: Arc<AppState> = app;
    let _ = tokio::task::spawn_blocking(move || a.persist_dirty()).await;
    result
}

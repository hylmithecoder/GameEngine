//! Runtime settings, read from the environment.
//!
//! The servertunnel deployer always sets `HOST` and `PORT`; everything else is
//! optional and can be filled in on the dashboard (Aplikasi → Environment).

use std::path::PathBuf;
use std::time::Duration;

#[derive(Debug, Clone)]
pub struct Config {
    pub host: String,
    pub port: u16,
    /// Shared secret every client must present in `hello.token`.
    /// Empty = open server (fine for local testing, not for the internet).
    pub token: String,
    pub max_rooms: usize,
    pub max_peers_per_room: usize,
    pub max_keys_per_room: usize,
    /// Largest text frame accepted from a client, in bytes.
    pub max_message_bytes: usize,
    /// Largest `presence` payload (serialized), in bytes.
    pub max_presence_bytes: usize,
    /// An empty room is forgotten after this long without activity.
    pub room_ttl: Duration,
    /// Where rooms are saved so a redeploy does not wipe them. None = memory only.
    pub data_dir: Option<PathBuf>,
    pub persist_interval: Duration,
    /// Project files of each workspace (see files.rs). None = `<data_dir>/files`,
    /// or a fresh temp folder when there is no data_dir.
    pub files_dir: Option<PathBuf>,
    /// Largest single project file.
    pub max_blob_bytes: u64,
    /// Largest project (sum of its files) one room may publish.
    pub max_project_bytes: u64,
    pub max_project_files: usize,
    /// Disk budget for all stored files together.
    pub max_storage_bytes: u64,
    /// UDP port answering LAN discovery probes; 0 = off (the public server).
    /// The Hub's "Host on LAN" sets 9231.
    pub discovery_port: u16,
    /// Stable id + display name in discovery answers.
    pub server_id: String,
    pub server_name: String,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            host: "127.0.0.1".into(),
            port: 9230,
            token: String::new(),
            max_rooms: 256,
            max_peers_per_room: 32,
            max_keys_per_room: 50_000,
            max_message_bytes: 1 << 20,
            max_presence_bytes: 4 << 10,
            room_ttl: Duration::from_secs(7 * 24 * 3600),
            data_dir: None,
            persist_interval: Duration::from_secs(5),
            files_dir: None,
            max_blob_bytes: 512 << 20,
            max_project_bytes: 4 << 30,
            max_project_files: 100_000,
            max_storage_bytes: 16 << 30,
            discovery_port: 0,
            server_id: String::new(),
            server_name: String::new(),
        }
    }
}

fn env_or<T: std::str::FromStr>(key: &str, default: T) -> T {
    std::env::var(key)
        .ok()
        .and_then(|v| v.trim().parse().ok())
        .unwrap_or(default)
}

impl Config {
    pub fn from_env() -> Self {
        let d = Self::default();
        // `RT_DATA_DIR=` (empty) disables persistence; unset falls back to $HOME,
        // which the deployer points at the app's private data directory.
        let data_dir = match std::env::var("RT_DATA_DIR") {
            Ok(v) if v.trim().is_empty() => None,
            Ok(v) => Some(PathBuf::from(v)),
            Err(_) => std::env::var("HOME")
                .ok()
                .filter(|h| !h.is_empty())
                .map(|h| PathBuf::from(h).join("rooms")),
        };
        Self {
            host: std::env::var("HOST").unwrap_or(d.host),
            port: env_or("PORT", d.port),
            token: std::env::var("ILMEEE_RT_TOKEN").unwrap_or_default(),
            max_rooms: env_or("RT_MAX_ROOMS", d.max_rooms),
            max_peers_per_room: env_or("RT_MAX_PEERS", d.max_peers_per_room),
            max_keys_per_room: env_or("RT_MAX_KEYS", d.max_keys_per_room),
            max_message_bytes: env_or("RT_MAX_MESSAGE_BYTES", d.max_message_bytes),
            max_presence_bytes: d.max_presence_bytes,
            room_ttl: Duration::from_secs(env_or("RT_ROOM_TTL_SECS", d.room_ttl.as_secs())),
            data_dir,
            persist_interval: d.persist_interval,
            files_dir: std::env::var("RT_FILES_DIR")
                .ok()
                .filter(|v| !v.trim().is_empty())
                .map(PathBuf::from),
            max_blob_bytes: env_or("RT_MAX_FILE_BYTES", d.max_blob_bytes),
            max_project_bytes: env_or("RT_MAX_PROJECT_BYTES", d.max_project_bytes),
            max_project_files: env_or("RT_MAX_PROJECT_FILES", d.max_project_files),
            max_storage_bytes: env_or("RT_MAX_STORAGE_BYTES", d.max_storage_bytes),
            discovery_port: env_or("RT_DISCOVERY_PORT", d.discovery_port),
            server_id: std::env::var("RT_SERVER_ID")
                .ok()
                .filter(|v| crate::lan::valid_id(v))
                .unwrap_or_else(|| format!("rt-{:x}", std::process::id())),
            server_name: std::env::var("RT_SERVER_NAME")
                .ok()
                .filter(|v| !v.trim().is_empty())
                .unwrap_or_else(|| "Ilmeee LAN server".into()),
        }
    }
}

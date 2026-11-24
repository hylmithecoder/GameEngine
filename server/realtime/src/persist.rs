//! Rooms on disk (`<data_dir>/<room>.json`), so a redeploy or crash does not
//! wipe what people built. Written atomically (tmp + rename).

use crate::protocol::valid_room;
use crate::room::RoomState;
use std::collections::HashMap;
use std::path::Path;

pub fn save_room(dir: &Path, name: &str, state: &RoomState) -> std::io::Result<()> {
    std::fs::create_dir_all(dir)?;
    let path = dir.join(format!("{name}.json"));
    let tmp = dir.join(format!(".{name}.json.tmp"));
    let bytes = serde_json::to_vec(state).map_err(std::io::Error::other)?;
    std::fs::write(&tmp, bytes)?;
    std::fs::rename(&tmp, &path)
}

pub fn remove_room(dir: &Path, name: &str) {
    let _ = std::fs::remove_file(dir.join(format!("{name}.json")));
}

/// Every readable room; broken files are skipped with a warning, not fatal.
pub fn load_rooms(dir: &Path) -> HashMap<String, RoomState> {
    let mut out = HashMap::new();
    let Ok(entries) = std::fs::read_dir(dir) else {
        return out;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        let Some(name) = path
            .file_name()
            .and_then(|n| n.to_str())
            .and_then(|n| n.strip_suffix(".json"))
        else {
            continue;
        };
        if !valid_room(name) {
            continue;
        }
        match std::fs::read(&path).map(|b| serde_json::from_slice::<RoomState>(&b)) {
            Ok(Ok(state)) => {
                out.insert(name.to_string(), state);
            }
            Ok(Err(e)) => tracing::warn!(room = name, error = %e, "skipping unreadable room file"),
            Err(e) => tracing::warn!(room = name, error = %e, "skipping room file"),
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::room::OpIn;
    use serde_json::json;

    #[test]
    fn round_trip() {
        let dir = std::env::temp_dir().join(format!("ilmeee-rt-persist-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        let mut s = RoomState::default();
        s.apply(
            3,
            OpIn::parse(&json!({"key":"k","slot":"data","data":{"x":1}})).unwrap(),
            10,
        )
        .unwrap();
        save_room(&dir, "lobby", &s).unwrap();
        std::fs::write(dir.join("broken.json"), "{").unwrap();
        std::fs::write(dir.join("..bad.json"), "{}").unwrap();
        let rooms = load_rooms(&dir);
        assert_eq!(rooms.len(), 1);
        let back = &rooms["lobby"];
        assert_eq!(back.seq, 1);
        assert_eq!(back.replay(), s.replay());
        remove_room(&dir, "lobby");
        assert!(load_rooms(&dir).is_empty());
        let _ = std::fs::remove_dir_all(&dir);
    }
}

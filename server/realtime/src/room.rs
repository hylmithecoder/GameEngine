//! Room state: an ordered last-writer-wins key/value store.
//!
//! The server never looks inside the data. A client writes `(key, slot, data)`
//! (the editor uses key = object id, slot = "data" / "xform"); the server
//! stamps it with the next sequence number, keeps only the newest value per
//! slot, and broadcasts it to the whole room *including the sender*. Since
//! everyone applies ops in sequence order, every peer converges to the same
//! state. A late joiner gets the stored values as a replay list instead of the
//! whole history.

use indexmap::IndexMap;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::time::{SystemTime, UNIX_EPOCH};

pub const MAX_KEY_LEN: usize = 128;
pub const MAX_SLOT_LEN: usize = 32;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Stored {
    pub seq: u64,
    pub from: u32,
    pub data: Value,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct RoomState {
    pub seq: u64,
    /// Keys in creation order (a key deleted and written again moves to the
    /// end), each with its slots.
    pub keys: IndexMap<String, IndexMap<String, Stored>>,
    /// Unix seconds of the last change or join; drives the idle TTL.
    #[serde(default)]
    pub updated_unix: u64,
}

pub fn now_unix() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

/// One client write, already parsed from an `op` event.
#[derive(Debug, Clone)]
pub struct OpIn {
    pub key: String,
    pub slot: String,
    pub data: Value,
    pub del: bool,
}

impl OpIn {
    pub fn parse(p: &Value) -> Result<Self, String> {
        let key = p.get("key").and_then(Value::as_str).unwrap_or("");
        if key.is_empty() || key.len() > MAX_KEY_LEN {
            return Err(format!("op key must be 1..={MAX_KEY_LEN} bytes"));
        }
        let del = p.get("del").and_then(Value::as_bool).unwrap_or(false);
        let slot = p.get("slot").and_then(Value::as_str).unwrap_or("");
        if !del && (slot.is_empty() || slot.len() > MAX_SLOT_LEN) {
            return Err(format!("op slot must be 1..={MAX_SLOT_LEN} bytes"));
        }
        Ok(Self {
            key: key.to_string(),
            slot: if del { String::new() } else { slot.to_string() },
            data: p.get("data").cloned().unwrap_or(Value::Null),
            del,
        })
    }
}

fn op_json(seq: u64, from: u32, key: &str, slot: &str, data: &Value, del: bool) -> Value {
    json!({"seq": seq, "from": from, "key": key, "slot": slot, "data": data, "del": del})
}

impl RoomState {
    /// Applies a write and returns the `op` payload to broadcast.
    pub fn apply(&mut self, from: u32, op: OpIn, max_keys: usize) -> Result<Value, String> {
        if op.del {
            // Deleting an unknown key still gets a sequence number: the sender
            // relies on seeing its own echo, and it is harmless for others.
            self.keys.shift_remove(&op.key);
        } else {
            if !self.keys.contains_key(&op.key) && self.keys.len() >= max_keys {
                return Err(format!("room is full ({max_keys} keys)"));
            }
            self.seq += 1;
            let seq = self.seq;
            self.keys.entry(op.key.clone()).or_default().insert(
                op.slot.clone(),
                Stored {
                    seq,
                    from,
                    data: op.data.clone(),
                },
            );
            self.updated_unix = now_unix();
            return Ok(op_json(seq, from, &op.key, &op.slot, &op.data, false));
        }
        self.seq += 1;
        self.updated_unix = now_unix();
        Ok(op_json(self.seq, from, &op.key, "", &Value::Null, true))
    }

    /// Current values as ops, keys in creation order, slots in write order.
    pub fn replay(&self) -> Vec<Value> {
        let mut out = Vec::new();
        for (key, slots) in &self.keys {
            let mut s: Vec<(&String, &Stored)> = slots.iter().collect();
            s.sort_by_key(|(_, v)| v.seq);
            for (slot, v) in s {
                out.push(op_json(v.seq, v.from, key, slot, &v.data, false));
            }
        }
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn put(key: &str, slot: &str, data: Value) -> OpIn {
        OpIn {
            key: key.into(),
            slot: slot.into(),
            data,
            del: false,
        }
    }

    #[test]
    fn last_writer_wins_per_slot() {
        let mut r = RoomState::default();
        r.apply(1, put("a", "data", json!(1)), 10).unwrap();
        r.apply(2, put("a", "xform", json!("x")), 10).unwrap();
        let last = r.apply(2, put("a", "data", json!(2)), 10).unwrap();
        assert_eq!(last["seq"], 3);
        let replay = r.replay();
        assert_eq!(replay.len(), 2);
        // xform (seq 2) before data (seq 3)
        assert_eq!(replay[0]["slot"], "xform");
        assert_eq!(replay[1]["data"], 2);
        assert_eq!(replay[1]["from"], 2);
    }

    #[test]
    fn delete_drops_every_slot_and_recreate_moves_to_end() {
        let mut r = RoomState::default();
        r.apply(1, put("a", "data", json!(1)), 10).unwrap();
        r.apply(1, put("b", "data", json!(1)), 10).unwrap();
        let d = r
            .apply(1, OpIn::parse(&json!({"key":"a","del":true})).unwrap(), 10)
            .unwrap();
        assert_eq!(d["del"], true);
        assert_eq!(r.keys.len(), 1);
        r.apply(1, put("a", "data", json!(9)), 10).unwrap();
        let keys: Vec<_> = r.keys.keys().cloned().collect();
        assert_eq!(keys, ["b", "a"]);
    }

    #[test]
    fn key_limit() {
        let mut r = RoomState::default();
        r.apply(1, put("a", "data", json!(1)), 1).unwrap();
        assert!(r.apply(1, put("b", "data", json!(1)), 1).is_err());
        // updating an existing key is still allowed at the limit
        r.apply(1, put("a", "xform", json!(1)), 1).unwrap();
    }

    #[test]
    fn parse_validates() {
        assert!(OpIn::parse(&json!({"slot":"data"})).is_err());
        assert!(OpIn::parse(&json!({"key":"a"})).is_err());
        assert!(OpIn::parse(&json!({"key":"a","del":true})).is_ok());
        assert!(OpIn::parse(&json!({"key":"k".repeat(129),"slot":"d"})).is_err());
    }
}

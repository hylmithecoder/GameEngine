//! Wire envelope, identical to the engine's `ilmeee::net::Message`
//! (include/core_engine/net/Message.hpp), so the editor reuses its existing
//! `MessageBus` + `WsClientTransport` to talk to this server:
//!
//! ```text
//! event    {"v":1, "type":"op", "payload":{...}}
//! request  {"v":1, "type":"room.join", "id":42, "payload":{...}}
//! response {"v":1, "type":"room.join", "re":42, "ok":true,  "payload":{...}}
//!          {"v":1, "type":"room.join", "re":42, "ok":false, "error":"..."}
//! ```

use serde_json::{Map, Value, json};

pub const PROTOCOL_VERSION: i64 = 1;

#[derive(Debug, Clone, PartialEq)]
pub enum Kind {
    Event,
    Request(u64),
    Response(u64),
}

#[derive(Debug, Clone)]
pub struct Envelope {
    pub kind: Kind,
    pub ty: String,
    pub payload: Value,
}

pub fn decode(text: &str) -> Result<Envelope, String> {
    let v: Value = serde_json::from_str(text).map_err(|e| format!("bad JSON: {e}"))?;
    let obj = v.as_object().ok_or("message is not an object")?;
    match obj.get("v").and_then(Value::as_i64) {
        Some(PROTOCOL_VERSION) => {}
        Some(other) => return Err(format!("envelope version {other} not supported")),
        None => return Err("missing envelope version".into()),
    }
    let ty = obj
        .get("type")
        .and_then(Value::as_str)
        .filter(|s| !s.is_empty())
        .ok_or("missing type")?
        .to_string();
    let payload = match obj.get("payload") {
        None => Value::Object(Map::new()),
        Some(p @ Value::Object(_)) => p.clone(),
        Some(_) => return Err("payload must be an object".into()),
    };
    let id = obj.get("id");
    let re = obj.get("re");
    let positive = |v: &Value| v.as_u64().filter(|n| *n > 0);
    let kind = match (id, re) {
        (Some(_), Some(_)) => return Err("message has both id and re".into()),
        (Some(id), None) => {
            Kind::Request(positive(id).ok_or("request id must be a positive integer")?)
        }
        (None, Some(re)) => {
            Kind::Response(positive(re).ok_or("response re must be a positive integer")?)
        }
        (None, None) => Kind::Event,
    };
    Ok(Envelope { kind, ty, payload })
}

pub fn event(ty: &str, payload: Value) -> String {
    json!({"v": PROTOCOL_VERSION, "type": ty, "payload": payload}).to_string()
}

pub fn reply(ty: &str, id: u64, payload: Value) -> String {
    json!({"v": PROTOCOL_VERSION, "type": ty, "re": id, "ok": true, "payload": payload}).to_string()
}

pub fn fail(ty: &str, id: u64, error: &str) -> String {
    json!({"v": PROTOCOL_VERSION, "type": ty, "re": id, "ok": false, "error": error, "payload": {}})
        .to_string()
}

/// Constant-time comparison so the token cannot be guessed byte by byte.
pub fn tokens_equal(a: &str, b: &str) -> bool {
    let (a, b) = (a.as_bytes(), b.as_bytes());
    if a.len() != b.len() {
        return false;
    }
    a.iter().zip(b).fold(0u8, |acc, (x, y)| acc | (x ^ y)) == 0
}

/// Room names: 1..=64 of `[A-Za-z0-9_.-]`, so they are also safe file names.
pub fn valid_room(name: &str) -> bool {
    !name.is_empty()
        && name.len() <= 64
        && !name.starts_with('.')
        && name
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || matches!(b, b'_' | b'-' | b'.'))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn decodes_the_three_kinds() {
        let e = decode(r#"{"v":1,"type":"op","payload":{"a":1}}"#).unwrap();
        assert_eq!(e.kind, Kind::Event);
        assert_eq!(e.payload["a"], 1);
        let r = decode(r#"{"v":1,"type":"room.join","id":7}"#).unwrap();
        assert_eq!(r.kind, Kind::Request(7));
        assert!(r.payload.is_object());
        let s = decode(r#"{"v":1,"type":"x","re":3,"ok":true}"#).unwrap();
        assert_eq!(s.kind, Kind::Response(3));
    }

    #[test]
    fn rejects_malformed() {
        for bad in [
            "nope",
            "[]",
            r#"{"type":"x"}"#,
            r#"{"v":2,"type":"x"}"#,
            r#"{"v":1,"type":""}"#,
            r#"{"v":1,"type":"x","payload":[]}"#,
            r#"{"v":1,"type":"x","id":1,"re":1}"#,
            r#"{"v":1,"type":"x","id":0}"#,
        ] {
            assert!(decode(bad).is_err(), "{bad}");
        }
    }

    #[test]
    fn room_names() {
        assert!(valid_room("level-1_main.v2"));
        assert!(!valid_room(""));
        assert!(!valid_room("../etc"));
        assert!(!valid_room(".hidden"));
        assert!(!valid_room("a b"));
        assert!(!valid_room(&"x".repeat(65)));
    }

    #[test]
    fn token_compare() {
        assert!(tokens_equal("abc", "abc"));
        assert!(!tokens_equal("abc", "abd"));
        assert!(!tokens_equal("abc", "abcd"));
    }
}

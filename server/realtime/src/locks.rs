//! Edit locks with a FIFO wait queue, one per key (usually an object key).
//!
//! A peer asks for a key when it starts editing it (selecting the object in
//! the editor). If nobody holds it, the peer becomes the holder; otherwise it
//! joins the back of the queue. When the holder releases the key (or leaves
//! the room) the next peer in the queue becomes the holder. Locks live only in
//! memory: they belong to connected peers and are never persisted.
//!
//! Locks are advisory on the server — ops on a locked key are still accepted.
//! The editor enforces them in its UI (read-only inspector, no gizmo), so the
//! server never has to roll a client back.

use serde_json::{Value, json};
use std::collections::{HashMap, VecDeque};

pub const MAX_KEY_LEN: usize = 128;

#[derive(Debug, Default)]
struct Lock {
    holder: u32,
    queue: VecDeque<u32>,
}

#[derive(Debug, Default)]
pub struct Locks {
    map: HashMap<String, Lock>,
}

impl Locks {
    /// How many keys `peer` holds or waits for.
    fn count_for(&self, peer: u32) -> usize {
        self.map
            .values()
            .filter(|l| l.holder == peer || l.queue.contains(&peer))
            .count()
    }

    /// Holder or queue changed → Ok(true). Already holding/queued → Ok(false).
    pub fn acquire(&mut self, key: &str, peer: u32, max_per_peer: usize) -> Result<bool, String> {
        if key.is_empty() || key.len() > MAX_KEY_LEN {
            return Err(format!("lock key must be 1-{MAX_KEY_LEN} bytes"));
        }
        if let Some(l) = self.map.get(key)
            && (l.holder == peer || l.queue.contains(&peer))
        {
            return Ok(false);
        }
        if self.count_for(peer) >= max_per_peer {
            return Err(format!("too many locks (max {max_per_peer} per peer)"));
        }
        let l = self.map.entry(key.to_string()).or_default();
        if l.holder == 0 {
            l.holder = peer;
        } else {
            l.queue.push_back(peer);
        }
        Ok(true)
    }

    /// Gives up the key (holder) or leaves its queue. True if anything changed.
    pub fn release(&mut self, key: &str, peer: u32) -> bool {
        let Some(l) = self.map.get_mut(key) else {
            return false;
        };
        let changed = if l.holder == peer {
            l.holder = l.queue.pop_front().unwrap_or(0);
            true
        } else if let Some(pos) = l.queue.iter().position(|p| *p == peer) {
            l.queue.remove(pos);
            true
        } else {
            false
        };
        if l.holder == 0 && l.queue.is_empty() {
            self.map.remove(key);
        }
        changed
    }

    /// Drops everything `peer` holds or waits for; returns the changed keys.
    pub fn release_peer(&mut self, peer: u32) -> Vec<String> {
        let keys: Vec<String> = self
            .map
            .iter()
            .filter(|(_, l)| l.holder == peer || l.queue.contains(&peer))
            .map(|(k, _)| k.clone())
            .collect();
        for k in &keys {
            self.release(k, peer);
        }
        keys
    }

    /// `{key, holder, queue}`; holder 0 = free.
    pub fn describe(&self, key: &str) -> Value {
        match self.map.get(key) {
            Some(l) => json!({"key": key, "holder": l.holder, "queue": l.queue}),
            None => json!({"key": key, "holder": 0, "queue": []}),
        }
    }

    pub fn all(&self) -> Vec<Value> {
        let mut keys: Vec<&String> = self.map.keys().collect();
        keys.sort();
        keys.into_iter().map(|k| self.describe(k)).collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn holder(l: &Locks, key: &str) -> u64 {
        l.describe(key)["holder"].as_u64().unwrap()
    }
    fn queue(l: &Locks, key: &str) -> Vec<u64> {
        l.describe(key)["queue"]
            .as_array()
            .unwrap()
            .iter()
            .map(|v| v.as_u64().unwrap())
            .collect()
    }

    #[test]
    fn fifo_handoff() {
        let mut l = Locks::default();
        assert_eq!(l.acquire("o1", 1, 8), Ok(true));
        assert_eq!(l.acquire("o1", 1, 8), Ok(false)); // idempotent
        assert_eq!(l.acquire("o1", 2, 8), Ok(true));
        assert_eq!(l.acquire("o1", 3, 8), Ok(true));
        assert_eq!(l.acquire("o1", 2, 8), Ok(false)); // already queued
        assert_eq!(holder(&l, "o1"), 1);
        assert_eq!(queue(&l, "o1"), vec![2, 3]);
        assert!(l.release("o1", 1));
        assert_eq!(holder(&l, "o1"), 2);
        assert!(l.release("o1", 3)); // leave the queue
        assert_eq!(queue(&l, "o1"), Vec::<u64>::new());
        assert!(!l.release("o1", 9));
        assert!(l.release("o1", 2));
        assert_eq!(holder(&l, "o1"), 0);
        assert!(l.all().is_empty());
    }

    #[test]
    fn peer_leaving_frees_everything() {
        let mut l = Locks::default();
        l.acquire("a", 1, 8).unwrap();
        l.acquire("b", 2, 8).unwrap();
        l.acquire("b", 1, 8).unwrap();
        l.acquire("a", 2, 8).unwrap();
        let mut changed = l.release_peer(1);
        changed.sort();
        assert_eq!(changed, vec!["a", "b"]);
        assert_eq!(holder(&l, "a"), 2);
        assert_eq!(holder(&l, "b"), 2);
        assert_eq!(queue(&l, "b"), Vec::<u64>::new());
    }

    #[test]
    fn limits() {
        let mut l = Locks::default();
        assert!(l.acquire("", 1, 8).is_err());
        assert!(l.acquire(&"x".repeat(MAX_KEY_LEN + 1), 1, 8).is_err());
        l.acquire("a", 1, 2).unwrap();
        l.acquire("b", 1, 2).unwrap();
        assert!(l.acquire("c", 1, 2).is_err());
    }
}

//! Finding a realtime server on the same network (Hub "On this network").
//!
//! Two independent ways, because one WiFi can be several IPv4 subnets
//! (172.18.9.x and 172.18.10.x behind one router):
//!
//! 1. **UDP discovery** (`serve_discovery`, enabled with `RT_DISCOVERY_PORT`):
//!    a LAN server answers the probe `ILMEEE_DISCOVER 1` with a small JSON
//!    description. Clients broadcast the probe to every interface's real
//!    broadcast address, which reaches everyone on the same subnet however
//!    large it is (/16, /20, ...). Broadcasts never cross a router.
//!
//! 2. **Rendezvous** (`Directory`, on the public server): a LAN host
//!    announces `{id, name, port, addrs}` with `POST /lan/announce`; the
//!    server files it under the public IP the request came from. Devices on
//!    the same WiFi share that public IP (one NAT), so `GET /lan/nearby`
//!    returns the hosts behind it, and the client then connects **directly**
//!    to their LAN addresses. Only addresses pass through here, never data.
//!    This is what finds a host on another subnet of the same network.
//!
//! Clients check every address with `GET /health` before showing it, so a
//! network that blocks device-to-device traffic (AP client isolation) shows
//! nothing instead of servers that cannot be reached.

use serde::{Deserialize, Serialize};
use serde_json::json;
use std::collections::HashMap;
use std::net::{IpAddr, Ipv4Addr};
use std::sync::Mutex;
use std::time::{Duration, Instant};

pub const PROBE: &[u8] = b"ILMEEE_DISCOVER 1";
/// An announcement is forgotten after this long without a refresh (hosts
/// re-announce every minute).
pub const ANNOUNCE_TTL: Duration = Duration::from_secs(150);
const MAX_HOSTS_PER_IP: usize = 32;
const MAX_PUBLIC_IPS: usize = 4096;
const MAX_ADDRS: usize = 16;
const MAX_TEXT: usize = 64;

#[derive(Debug, Clone, Deserialize)]
pub struct Announce {
    pub id: String,
    #[serde(default)]
    pub name: String,
    pub port: u16,
    #[serde(default)]
    pub addrs: Vec<String>,
    /// The LAN server wants a token.
    #[serde(default)]
    pub auth: bool,
    /// true = the host stopped; drop it now instead of waiting for the TTL.
    #[serde(default)]
    pub gone: bool,
}

#[derive(Debug, Clone, Serialize)]
pub struct LanHost {
    pub id: String,
    pub name: String,
    pub port: u16,
    pub addrs: Vec<String>,
    pub auth: bool,
    pub age_secs: u64,
}

struct Entry {
    host: LanHost,
    seen: Instant,
}

/// Address ranges a LAN server can have. Anything else (public addresses,
/// loopback, multicast) is refused, so the directory cannot be used to point
/// people at arbitrary machines.
pub fn is_lan_addr(ip: &Ipv4Addr) -> bool {
    let o = ip.octets();
    ip.is_private() || ip.is_link_local() || (o[0] == 100 && (64..128).contains(&o[1])) // CGNAT 100.64/10 (hotspots)
}

pub fn valid_id(id: &str) -> bool {
    !id.is_empty()
        && id.len() <= MAX_TEXT
        && id
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || b == b'-' || b == b'_')
}

fn clean_name(name: &str) -> String {
    name.chars()
        .filter(|c| !c.is_control())
        .take(MAX_TEXT)
        .collect()
}

#[derive(Default)]
pub struct Directory {
    by_public_ip: Mutex<HashMap<IpAddr, Vec<Entry>>>,
}

impl Directory {
    /// Validates and stores (or with `gone`, removes) an announcement.
    pub fn announce(&self, public_ip: IpAddr, a: Announce) -> Result<(), &'static str> {
        if !valid_id(&a.id) {
            return Err("id must be 1-64 letters, digits, '-' or '_'");
        }
        let mut map = self.by_public_ip.lock().unwrap_or_else(|e| e.into_inner());
        if a.gone {
            if let Some(list) = map.get_mut(&public_ip) {
                list.retain(|e| e.host.id != a.id);
                if list.is_empty() {
                    map.remove(&public_ip);
                }
            }
            return Ok(());
        }
        if a.port == 0 {
            return Err("port must be 1-65535");
        }
        let mut addrs: Vec<String> = Vec::new();
        for s in a.addrs.iter().take(MAX_ADDRS) {
            match s.parse::<Ipv4Addr>() {
                Ok(ip) if is_lan_addr(&ip) => {
                    if !addrs.contains(&ip.to_string()) {
                        addrs.push(ip.to_string());
                    }
                }
                _ => return Err("addrs must be private IPv4 addresses"),
            }
        }
        if addrs.is_empty() {
            return Err("no LAN address given");
        }
        if !map.contains_key(&public_ip) && map.len() >= MAX_PUBLIC_IPS {
            Self::sweep_locked(&mut map);
            if map.len() >= MAX_PUBLIC_IPS {
                return Err("directory full, try again later");
            }
        }
        let list = map.entry(public_ip).or_default();
        let host = LanHost {
            id: a.id.clone(),
            name: clean_name(&a.name),
            port: a.port,
            addrs,
            auth: a.auth,
            age_secs: 0,
        };
        if let Some(e) = list.iter_mut().find(|e| e.host.id == a.id) {
            e.host = host;
            e.seen = Instant::now();
        } else {
            if list.len() >= MAX_HOSTS_PER_IP {
                // Replace the stalest one.
                list.sort_by_key(|e| std::cmp::Reverse(e.seen));
                list.truncate(MAX_HOSTS_PER_IP - 1);
            }
            list.push(Entry {
                host,
                seen: Instant::now(),
            });
        }
        Ok(())
    }

    /// Hosts announced from `public_ip` within the TTL, freshest first.
    pub fn nearby(&self, public_ip: IpAddr) -> Vec<LanHost> {
        let map = self.by_public_ip.lock().unwrap_or_else(|e| e.into_inner());
        let mut out: Vec<LanHost> = map
            .get(&public_ip)
            .map(|list| {
                list.iter()
                    .filter(|e| e.seen.elapsed() < ANNOUNCE_TTL)
                    .map(|e| {
                        let mut h = e.host.clone();
                        h.age_secs = e.seen.elapsed().as_secs();
                        h
                    })
                    .collect()
            })
            .unwrap_or_default();
        out.sort_by_key(|h| h.age_secs);
        out
    }

    pub fn sweep(&self) {
        let mut map = self.by_public_ip.lock().unwrap_or_else(|e| e.into_inner());
        Self::sweep_locked(&mut map);
    }

    fn sweep_locked(map: &mut HashMap<IpAddr, Vec<Entry>>) {
        map.retain(|_, list| {
            list.retain(|e| e.seen.elapsed() < ANNOUNCE_TTL);
            !list.is_empty()
        });
    }
}

/// What a LAN server says about itself (UDP answer).
#[derive(Debug, Clone)]
pub struct ServerInfo {
    pub id: String,
    pub name: String,
    pub port: u16,
    pub auth: bool,
}

/// Answers discovery probes on `udp_port` until the task is dropped.
pub async fn serve_discovery(
    udp_port: u16,
    info: ServerInfo,
    rooms: impl Fn() -> usize + Send + 'static,
) -> std::io::Result<()> {
    let sock = tokio::net::UdpSocket::bind(("0.0.0.0", udp_port)).await?;
    tracing::info!(udp_port, id = %info.id, "LAN discovery responder listening");
    let mut buf = [0u8; 256];
    loop {
        let (n, from) = match sock.recv_from(&mut buf).await {
            Ok(v) => v,
            Err(e) => {
                // Windows reports ICMP "port unreachable" from an earlier
                // send as a recv error; it says nothing about this socket.
                tracing::debug!(error = %e, "discovery recv");
                continue;
            }
        };
        if !buf[..n].starts_with(PROBE) {
            continue;
        }
        let reply = json!({
            "ilmeee": "realtime",
            "v": 1,
            "id": info.id,
            "name": info.name,
            "port": info.port,
            "auth": info.auth,
            "version": crate::server::VERSION,
            "rooms": rooms(),
        })
        .to_string();
        if let Err(e) = sock.send_to(reply.as_bytes(), from).await {
            tracing::debug!(error = %e, %from, "discovery reply");
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ann(id: &str, addrs: &[&str]) -> Announce {
        Announce {
            id: id.into(),
            name: "Laptop A".into(),
            port: 9230,
            addrs: addrs.iter().map(|s| s.to_string()).collect(),
            auth: false,
            gone: false,
        }
    }

    #[test]
    fn lan_ranges() {
        for ok in [
            "10.1.2.3",
            "172.18.9.42",
            "192.168.1.5",
            "169.254.3.4",
            "100.72.0.9",
        ] {
            assert!(is_lan_addr(&ok.parse().unwrap()), "{ok}");
        }
        for bad in [
            "8.8.8.8",
            "127.0.0.1",
            "172.32.0.1",
            "224.0.0.1",
            "100.128.0.1",
        ] {
            assert!(!is_lan_addr(&bad.parse().unwrap()), "{bad}");
        }
    }

    #[test]
    fn grouped_by_public_ip() {
        let d = Directory::default();
        let wifi: IpAddr = "203.0.113.7".parse().unwrap();
        let other: IpAddr = "198.51.100.2".parse().unwrap();
        d.announce(wifi, ann("host-a", &["172.18.9.42"])).unwrap();
        d.announce(wifi, ann("host-b", &["172.18.10.4", "10.0.0.2"]))
            .unwrap();
        d.announce(other, ann("host-c", &["192.168.0.2"])).unwrap();
        let near = d.nearby(wifi);
        assert_eq!(near.len(), 2);
        assert!(near.iter().any(|h| h.id == "host-b" && h.addrs.len() == 2));
        assert_eq!(d.nearby(other).len(), 1);
        // Re-announce replaces, gone removes.
        d.announce(wifi, ann("host-a", &["172.18.9.50"])).unwrap();
        assert_eq!(d.nearby(wifi).len(), 2);
        assert!(
            d.nearby(wifi)
                .iter()
                .any(|h| h.addrs == vec!["172.18.9.50"])
        );
        let mut gone = ann("host-a", &[]);
        gone.gone = true;
        d.announce(wifi, gone).unwrap();
        assert_eq!(d.nearby(wifi).len(), 1);
    }

    #[test]
    fn rejects_bad_announcements() {
        let d = Directory::default();
        let ip: IpAddr = "203.0.113.7".parse().unwrap();
        assert!(d.announce(ip, ann("a b", &["10.0.0.1"])).is_err());
        assert!(d.announce(ip, ann("ok", &["8.8.8.8"])).is_err());
        assert!(d.announce(ip, ann("ok", &[])).is_err());
        let mut p = ann("ok", &["10.0.0.1"]);
        p.port = 0;
        assert!(d.announce(ip, p).is_err());
    }
}

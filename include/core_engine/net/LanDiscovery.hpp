// Finding realtime collab servers on the local network (Hub: "On this
// network"). Server side: server/realtime/src/lan.rs.
//
// One WiFi can hand out addresses from several subnets (172.18.9.42 and
// 172.18.10.4), so two ways are combined:
//   1. UDP broadcast of a probe to every interface's *real* broadcast address
//      (from its netmask: 172.18.255.255 on a /16, not a guessed x.x.x.255),
//      plus 255.255.255.255. Finds servers on the same subnet, no internet.
//   2. Rendezvous on the online server: LAN hosts announce their addresses,
//      filed under the public IP they come from; GET /lan/nearby returns the
//      hosts behind the caller's own public IP (same WiFi = same NAT). Finds
//      hosts on another subnet of the same network.
// Every candidate address is then checked with GET /health, so only servers
// this device can really reach are reported (AP client isolation = none).
//
// All calls block; run them on a worker thread.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ilmeee::net {

inline constexpr std::uint16_t kLanServerPort = 9230;    // WebSocket + HTTP
inline constexpr std::uint16_t kLanDiscoveryPort = 9231; // UDP probe/answer

struct NetInterface {
  std::string name;
  std::uint32_t addr = 0; // IPv4, host byte order
  std::uint32_t mask = 0;
  std::uint32_t Broadcast() const { return (addr & mask) | ~mask; }
  int PrefixLength() const;
};

// Up, non-loopback IPv4 interfaces.
std::vector<NetInterface> ListIpv4Interfaces();
std::string Ipv4ToString(std::uint32_t hostOrder);
bool ParseIpv4(const std::string &s, std::uint32_t &hostOrder);
// Private / link-local / CGNAT: what a LAN server can have (same rule as lan.rs).
bool IsLanAddress(std::uint32_t hostOrder);
// The addresses a LAN host should announce (LAN addresses of its interfaces).
std::vector<std::string> LocalLanAddresses();

struct LanServer {
  std::string id;
  std::string name;
  std::string host; // the address that answered /health
  std::uint16_t port = kLanServerPort;
  bool auth = false; // the server wants a token
  std::size_t rooms = 0;
  bool viaBroadcast = false;
  bool viaRendezvous = false;
  std::string WsUrl() const { return "ws://" + host + ":" + std::to_string(port) + "/ws"; }
};

// UDP broadcast on every interface; answers collected for `timeoutMs`.
// Addresses are as seen by the reply (not yet checked with /health).
std::vector<LanServer> BroadcastDiscover(int timeoutMs = 800,
                                         std::uint16_t discoveryPort = kLanDiscoveryPort);

struct LanAnnouncement {
  std::string id;
  std::string name;
  std::uint16_t port = kLanServerPort;
  bool auth = false;
  std::vector<std::string> addrs; // LocalLanAddresses()
};

// POST /lan/announce on the online server (`onlineWsUrl` as in collab
// settings). gone = the host stopped. False + `error` on failure.
bool AnnounceLan(const std::string &onlineWsUrl, const std::string &token,
                 const LanAnnouncement &a, bool gone, std::string &error);

// GET /lan/nearby: hosts announced from this network. `addrs` of each are
// returned in LanServer::host as a ';'-joined list (resolved by Discover).
std::vector<LanServer> FetchNearby(const std::string &onlineWsUrl, const std::string &token,
                                   std::string &error);

// GET http://host:port/health answered with status ok.
bool ProbeLanServer(const std::string &host, std::uint16_t port, int timeoutSecs = 2);

struct LanDiscoveryResult {
  std::vector<LanServer> servers; // reachable, one per server id
  std::string rendezvousError;    // empty when the online lookup worked (or was skipped)
};

// Broadcast + rendezvous (when onlineWsUrl is set) + /health check.
LanDiscoveryResult DiscoverLanServers(const std::string &onlineWsUrl, const std::string &token,
                                      int broadcastMs = 800);

} // namespace ilmeee::net

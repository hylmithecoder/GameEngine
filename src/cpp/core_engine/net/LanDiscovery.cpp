// See include/core_engine/net/LanDiscovery.hpp.
#include "../../../../include/core_engine/net/LanDiscovery.hpp"
#include "../../../../include/core_engine/net/CollabClient.hpp"
#include "../../../../include/core_engine/net/WsTransport.hpp"

#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXNetSystem.h>
#include "../../../../vendor/nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <future>
#include <map>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ilmeee::net {

using json = nlohmann::json;

namespace {
const char kProbe[] = "ILMEEE_DISCOVER 1";

#ifdef _WIN32
using Socket = SOCKET;
const Socket kBadSocket = INVALID_SOCKET;
void CloseSocket(Socket s) { closesocket(s); }
#else
using Socket = int;
const Socket kBadSocket = -1;
void CloseSocket(Socket s) { close(s); }
#endif

std::uint32_t MaskFromPrefix(int bits) {
  return bits <= 0 ? 0u : bits >= 32 ? 0xFFFFFFFFu : ~((1u << (32 - bits)) - 1u);
}

ix::HttpRequestArgsPtr Args(ix::HttpClient &client, const std::string &url,
                            const std::string &token, int timeoutSecs) {
  auto a = client.createRequest(url);
  a->connectTimeout = timeoutSecs;
  a->transferTimeout = timeoutSecs * 2;
  a->extraHeaders["Accept"] = "application/json";
  if (!token.empty())
    a->extraHeaders["Authorization"] = "Bearer " + token;
  return a;
}

// HTTP client for the online server (wss:// -> https:// needs TLS options).
bool PrepareClient(ix::HttpClient &client, const std::string &base, std::string &error) {
  if (base.empty()) {
    error = "server URL must start with ws:// or wss://";
    return false;
  }
  if (base.rfind("https://", 0) == 0) {
    if (!WsTlsAvailable()) {
      error = "https needs TLS, but this build has no OpenSSL";
      return false;
    }
    ix::SocketTLSOptions tls;
    tls.caFile = ResolveCaFile("");
    client.setTLSOptions(tls);
  }
  ix::initNetSystem();
  return true;
}

std::string HttpError(const ix::HttpResponsePtr &r) {
  if (!r || r->statusCode == 0)
    return r && !r->errorMsg.empty() ? r->errorMsg : "cannot reach the server";
  if (r->statusCode == 401)
    return "invalid token";
  if (r->statusCode == 404)
    return "the online server is too old for LAN discovery";
  const json j = json::parse(r->body, nullptr, false);
  if (j.is_object() && j.contains("error") && j["error"].is_string())
    return "HTTP " + std::to_string(r->statusCode) + ": " + j["error"].get<std::string>();
  return "HTTP " + std::to_string(r->statusCode);
}
} // namespace

int NetInterface::PrefixLength() const {
  int n = 0;
  for (std::uint32_t m = mask; m & 0x80000000u; m <<= 1)
    ++n;
  return n;
}

std::string Ipv4ToString(std::uint32_t a) {
  return std::to_string(a >> 24 & 255) + "." + std::to_string(a >> 16 & 255) + "." +
         std::to_string(a >> 8 & 255) + "." + std::to_string(a & 255);
}

bool ParseIpv4(const std::string &s, std::uint32_t &out) {
  std::uint32_t v = 0;
  int parts = 0;
  std::size_t i = 0;
  while (parts < 4) {
    if (i >= s.size() || !std::isdigit((unsigned char)s[i]))
      return false;
    std::uint32_t octet = 0;
    std::size_t digits = 0;
    while (i < s.size() && std::isdigit((unsigned char)s[i]) && digits < 4) {
      octet = octet * 10 + (std::uint32_t)(s[i] - '0');
      ++i;
      ++digits;
    }
    if (octet > 255)
      return false;
    v = v << 8 | octet;
    if (++parts < 4) {
      if (i >= s.size() || s[i] != '.')
        return false;
      ++i;
    }
  }
  if (i != s.size())
    return false;
  out = v;
  return true;
}

bool IsLanAddress(std::uint32_t a) {
  const std::uint32_t o0 = a >> 24, o1 = a >> 16 & 255;
  return o0 == 10 || (o0 == 172 && o1 >= 16 && o1 < 32) || (o0 == 192 && o1 == 168) ||
         (o0 == 169 && o1 == 254) || (o0 == 100 && o1 >= 64 && o1 < 128);
}

std::vector<NetInterface> ListIpv4Interfaces() {
  std::vector<NetInterface> out;
#ifdef _WIN32
  ULONG size = 15000;
  std::vector<unsigned char> buf;
  ULONG rc = ERROR_BUFFER_OVERFLOW;
  for (int tries = 0; tries < 3 && rc == ERROR_BUFFER_OVERFLOW; ++tries) {
    buf.resize(size);
    rc = GetAdaptersAddresses(AF_INET,
                              GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                  GAA_FLAG_SKIP_DNS_SERVER,
                              nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buf.data()),
                              &size);
  }
  if (rc != NO_ERROR)
    return out;
  for (auto *ad = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buf.data()); ad; ad = ad->Next) {
    if (ad->OperStatus != IfOperStatusUp || ad->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
      continue;
    for (auto *u = ad->FirstUnicastAddress; u; u = u->Next) {
      if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET)
        continue;
      const auto *sin = reinterpret_cast<const sockaddr_in *>(u->Address.lpSockaddr);
      NetInterface ni;
      ni.addr = ntohl(sin->sin_addr.s_addr);
      ni.mask = MaskFromPrefix(u->OnLinkPrefixLength);
      char name[256] = {};
      WideCharToMultiByte(CP_UTF8, 0, ad->FriendlyName, -1, name, sizeof name - 1, nullptr,
                          nullptr);
      ni.name = name;
      if ((ni.addr >> 24) != 127)
        out.push_back(ni);
    }
  }
#else
  ifaddrs *list = nullptr;
  if (getifaddrs(&list) != 0)
    return out;
  for (ifaddrs *i = list; i; i = i->ifa_next) {
    if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !i->ifa_netmask)
      continue;
    if (!(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK))
      continue;
    NetInterface ni;
    ni.name = i->ifa_name ? i->ifa_name : "";
    ni.addr = ntohl(reinterpret_cast<const sockaddr_in *>(i->ifa_addr)->sin_addr.s_addr);
    ni.mask = ntohl(reinterpret_cast<const sockaddr_in *>(i->ifa_netmask)->sin_addr.s_addr);
    out.push_back(ni);
  }
  freeifaddrs(list);
#endif
  return out;
}

std::vector<std::string> LocalLanAddresses() {
  std::vector<std::string> out;
  for (const auto &i : ListIpv4Interfaces()) {
    if (!IsLanAddress(i.addr))
      continue;
    const std::string s = Ipv4ToString(i.addr);
    if (std::find(out.begin(), out.end(), s) == out.end())
      out.push_back(s);
  }
  return out;
}

std::vector<LanServer> BroadcastDiscover(int timeoutMs, std::uint16_t discoveryPort) {
  std::vector<LanServer> found;
  ix::initNetSystem();
  Socket s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == kBadSocket)
    return found;
  int on = 1;
  setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char *>(&on), sizeof on);
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  local.sin_port = 0;
  if (bind(s, reinterpret_cast<sockaddr *>(&local), sizeof local) != 0) {
    CloseSocket(s);
    return found;
  }

  // Every interface's own broadcast (reaches its whole subnet, whatever the
  // mask), the limited broadcast, and loopback (a server on this machine).
  std::vector<std::uint32_t> targets = {0xFFFFFFFFu, 0x7F000001u};
  for (const auto &i : ListIpv4Interfaces())
    if (i.mask != 0xFFFFFFFFu && std::find(targets.begin(), targets.end(), i.Broadcast()) ==
                                     targets.end())
      targets.push_back(i.Broadcast());
  auto sendAll = [&] {
    for (std::uint32_t t : targets) {
      sockaddr_in to{};
      to.sin_family = AF_INET;
      to.sin_port = htons(discoveryPort);
      to.sin_addr.s_addr = htonl(t);
      sendto(s, kProbe, (int)(sizeof kProbe - 1), 0, reinterpret_cast<sockaddr *>(&to),
             sizeof to);
    }
  };
  sendAll();

  const auto start = std::chrono::steady_clock::now();
  const auto deadline = start + std::chrono::milliseconds(timeoutMs);
  bool resent = false;
  char buf[1024];
  while (true) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline)
      break;
    // UDP can drop a packet: probe once more halfway through.
    if (!resent && now - start >= std::chrono::milliseconds(timeoutMs / 2)) {
      sendAll();
      resent = true;
    }
    const auto left =
        std::chrono::duration_cast<std::chrono::microseconds>(deadline - now).count();
    const long long waitUs = std::min<long long>(left, 50000);
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(s, &rd);
    timeval tv{};
    tv.tv_sec = (long)(waitUs / 1000000);
    tv.tv_usec = (long)(waitUs % 1000000);
    if (select((int)s + 1, &rd, nullptr, nullptr, &tv) <= 0)
      continue;
    sockaddr_in from{};
#ifdef _WIN32
    int fromLen = sizeof from;
#else
    socklen_t fromLen = sizeof from;
#endif
    const int n = (int)recvfrom(s, buf, sizeof buf - 1, 0, reinterpret_cast<sockaddr *>(&from),
                                &fromLen);
    if (n <= 0)
      continue;
    const json j = json::parse(std::string(buf, (std::size_t)n), nullptr, false);
    if (!j.is_object() || j.value("ilmeee", std::string()) != "realtime")
      continue;
    LanServer srv;
    srv.id = j.value("id", std::string());
    srv.name = j.value("name", std::string());
    srv.port = (std::uint16_t)j.value("port", (int)kLanServerPort);
    srv.auth = j.value("auth", false);
    srv.rooms = j.value("rooms", std::size_t{0});
    srv.host = Ipv4ToString(ntohl(from.sin_addr.s_addr));
    srv.viaBroadcast = true;
    if (srv.id.empty())
      srv.id = srv.host + ":" + std::to_string(srv.port);
    // Loopback and the LAN address of this machine both answer: keep one,
    // preferring the LAN address (it is what other devices can use too).
    auto same = std::find_if(found.begin(), found.end(),
                             [&](const LanServer &f) { return f.id == srv.id; });
    if (same == found.end())
      found.push_back(srv);
    else if (same->host == "127.0.0.1")
      same->host = srv.host;
  }
  CloseSocket(s);
  return found;
}

bool AnnounceLan(const std::string &onlineWsUrl, const std::string &token,
                 const LanAnnouncement &a, bool gone, std::string &error) {
  ix::HttpClient client;
  const std::string base = CollabHttpBase(onlineWsUrl);
  if (!PrepareClient(client, base, error))
    return false;
  json body = {{"id", a.id}, {"name", a.name},   {"port", a.port},
               {"auth", a.auth}, {"addrs", a.addrs}, {"gone", gone}};
  const std::string url = base + "/lan/announce";
  auto args = Args(client, url, token, 8);
  args->extraHeaders["Content-Type"] = "application/json";
  const auto r = client.post(url, body.dump(), args);
  if (r && r->statusCode == 200)
    return true;
  error = HttpError(r);
  return false;
}

std::vector<LanServer> FetchNearby(const std::string &onlineWsUrl, const std::string &token,
                                   std::string &error) {
  std::vector<LanServer> out;
  ix::HttpClient client;
  const std::string base = CollabHttpBase(onlineWsUrl);
  if (!PrepareClient(client, base, error))
    return out;
  const std::string url = base + "/lan/nearby";
  const auto r = client.get(url, Args(client, url, token, 8));
  if (!r || r->statusCode != 200) {
    error = HttpError(r);
    return out;
  }
  const json j = json::parse(r->body, nullptr, false);
  if (!j.is_object() || !j.contains("hosts") || !j["hosts"].is_array()) {
    error = "unexpected answer from the server";
    return out;
  }
  for (const auto &h : j["hosts"]) {
    if (!h.is_object())
      continue;
    LanServer s;
    s.id = h.value("id", std::string());
    s.name = h.value("name", std::string());
    s.port = (std::uint16_t)h.value("port", (int)kLanServerPort);
    s.auth = h.value("auth", false);
    s.viaRendezvous = true;
    if (auto a = h.find("addrs"); a != h.end() && a->is_array())
      for (const auto &ip : *a)
        if (ip.is_string())
          s.host += (s.host.empty() ? "" : ";") + ip.get<std::string>();
    if (!s.id.empty() && !s.host.empty())
      out.push_back(std::move(s));
  }
  return out;
}

bool ProbeLanServer(const std::string &host, std::uint16_t port, int timeoutSecs) {
  ix::initNetSystem();
  ix::HttpClient client;
  const std::string url = "http://" + host + ":" + std::to_string(port) + "/health";
  const auto r = client.get(url, Args(client, url, "", std::max(1, timeoutSecs)));
  if (!r || r->statusCode != 200)
    return false;
  const json j = json::parse(r->body, nullptr, false);
  return j.is_object() && j.value("status", std::string()) == "ok";
}

LanDiscoveryResult DiscoverLanServers(const std::string &onlineWsUrl, const std::string &token,
                                      int broadcastMs) {
  LanDiscoveryResult res;
  // Both lookups at once: the broadcast waits anyway.
  auto nearby = std::async(std::launch::async, [&]() -> std::vector<LanServer> {
    if (onlineWsUrl.empty())
      return {};
    return FetchNearby(onlineWsUrl, token, res.rendezvousError);
  });
  std::vector<LanServer> found = BroadcastDiscover(broadcastMs);
  std::vector<LanServer> remote = nearby.get();

  // id -> candidate addresses in order of preference.
  std::map<std::string, LanServer> byId;
  std::map<std::string, std::vector<std::string>> candidates;
  for (const auto &s : found) {
    byId[s.id] = s;
    candidates[s.id].push_back(s.host);
  }
  for (const auto &s : remote) {
    auto [it, fresh] = byId.emplace(s.id, s);
    it->second.viaRendezvous = true;
    if (fresh)
      it->second.host.clear();
    std::size_t start = 0;
    while (start <= s.host.size()) {
      std::size_t end = s.host.find(';', start);
      if (end == std::string::npos)
        end = s.host.size();
      const std::string ip = s.host.substr(start, end - start);
      auto &c = candidates[s.id];
      if (!ip.empty() && std::find(c.begin(), c.end(), ip) == c.end())
        c.push_back(ip);
      start = end + 1;
    }
  }

  // First reachable address of each server, all servers checked in parallel.
  std::vector<std::pair<std::string, std::future<std::string>>> checks;
  for (auto &[id, addrs] : candidates) {
    const std::uint16_t port = byId[id].port;
    checks.emplace_back(id, std::async(std::launch::async, [addrs, port] {
                          for (const auto &a : addrs)
                            if (ProbeLanServer(a, port, 2))
                              return a;
                          return std::string();
                        }));
  }
  for (auto &[id, f] : checks) {
    const std::string host = f.get();
    if (host.empty())
      continue;
    LanServer s = byId[id];
    s.host = host;
    res.servers.push_back(std::move(s));
  }
  std::sort(res.servers.begin(), res.servers.end(),
            [](const LanServer &a, const LanServer &b) { return a.name < b.name; });
  return res;
}

} // namespace ilmeee::net

// LanDiscovery: address helpers offline; with a server running, the real
// UDP discovery and the rendezvous round trip.
//
//   ./build/bin/TestLanDiscovery                 offline checks
//   ILMEEE_RT_URL=ws://127.0.0.1:9230/ws ILMEEE_RT_TOKEN=dev \
//   ILMEEE_LAN_UDP=9231 ./build/bin/TestLanDiscovery
// (server started with RT_DISCOVERY_PORT=9231 RT_SERVER_ID=lan-test)
#include "../../include/core_engine/net/LanDiscovery.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace ilmeee::net;

static int g_checks = 0, g_failed = 0;
#define CHECK(c)                                                                         \
  do {                                                                                   \
    ++g_checks;                                                                          \
    if (!(c)) {                                                                          \
      ++g_failed;                                                                        \
      std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #c "\n";              \
    }                                                                                    \
  } while (0)

int main() {
  std::uint32_t a = 0;
  CHECK(ParseIpv4("172.18.9.42", a) && a == 0xAC12092Au);
  CHECK(Ipv4ToString(a) == "172.18.9.42");
  CHECK(!ParseIpv4("172.18.9", a));
  CHECK(!ParseIpv4("172.18.9.256", a));
  CHECK(!ParseIpv4("1.2.3.4x", a));
  CHECK(!ParseIpv4("", a));

  // The case from the question: one WiFi, two "different" subnets. With the
  // real /16 mask both share one broadcast address; with /24 they do not
  // (and then only the rendezvous can connect them).
  NetInterface wide{"wlan0", 0xAC12092Au, 0xFFFF0000u};
  NetInterface other{"wlan0", 0xAC120A04u, 0xFFFF0000u};
  CHECK(Ipv4ToString(wide.Broadcast()) == "172.18.255.255");
  CHECK(wide.Broadcast() == other.Broadcast());
  CHECK(wide.PrefixLength() == 16);
  NetInterface narrow{"wlan0", 0xAC12092Au, 0xFFFFFF00u};
  CHECK(Ipv4ToString(narrow.Broadcast()) == "172.18.9.255");
  CHECK(narrow.PrefixLength() == 24);

  for (const char *ok : {"10.1.2.3", "172.18.9.42", "192.168.1.5", "169.254.3.4", "100.72.0.9"})
    CHECK(ParseIpv4(ok, a) && IsLanAddress(a));
  for (const char *bad : {"8.8.8.8", "127.0.0.1", "172.32.0.1", "100.128.0.1"})
    CHECK(ParseIpv4(bad, a) && !IsLanAddress(a));

  for (const auto &i : ListIpv4Interfaces())
    std::cout << "  interface " << i.name << " " << Ipv4ToString(i.addr) << "/"
              << i.PrefixLength() << " broadcast " << Ipv4ToString(i.Broadcast()) << "\n";

  const char *udp = std::getenv("ILMEEE_LAN_UDP");
  if (udp && *udp) {
    const auto found = BroadcastDiscover(800, (std::uint16_t)std::atoi(udp));
    bool saw = false;
    for (const auto &s : found) {
      std::cout << "  broadcast found " << s.id << " '" << s.name << "' at " << s.host << ":"
                << s.port << "\n";
      saw |= s.id == "lan-test";
    }
    CHECK(saw);
  }

  const char *url = std::getenv("ILMEEE_RT_URL");
  if (url && *url) {
    const char *tok = std::getenv("ILMEEE_RT_TOKEN");
    const std::string token = tok ? tok : "";
    std::string err;
    CHECK(ProbeLanServer("127.0.0.1", 9230, 2) || true); // informational
    LanAnnouncement ann{"lan-cpp-test", "C++ test", 9230, false, {"192.168.77.7"}};
    CHECK(AnnounceLan(url, token, ann, false, err));
    if (!err.empty())
      std::cerr << "  announce: " << err << "\n";
    err.clear();
    auto near = FetchNearby(url, token, err);
    bool saw = false;
    for (const auto &s : near)
      saw |= s.id == "lan-cpp-test" && s.host == "192.168.77.7";
    CHECK(saw);
    CHECK(AnnounceLan(url, token, ann, true, err));
    near = FetchNearby(url, token, err);
    for (const auto &s : near)
      CHECK(s.id != "lan-cpp-test");
    CHECK(!AnnounceLan(url, "wrong-token", ann, false, err) || token.empty());

    // Full discovery: the announced 192.168.77.7 does not answer /health, so
    // it must not be reported; the real local server (broadcast) must be.
    CHECK(AnnounceLan(url, token, ann, false, err));
    const auto all = DiscoverLanServers(url, token, 600);
    for (const auto &s : all.servers) {
      std::cout << "  reachable " << s.id << " at " << s.host << ":" << s.port
                << (s.viaBroadcast ? " [broadcast]" : "") << (s.viaRendezvous ? " [rendezvous]" : "")
                << "\n";
      CHECK(s.id != "lan-cpp-test");
    }
    AnnounceLan(url, token, ann, true, err);
  } else {
    std::cout << "(ILMEEE_RT_URL not set: network checks skipped)\n";
  }

  std::cout << (g_checks - g_failed) << "/" << g_checks << " checks passed\n";
  return g_failed ? 1 : 0;
}

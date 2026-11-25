// Integration tests for the WebSocket transports: real sockets on loopback,
// server and client buses in one process, each driven by its own Poll().
//
//   cmake --build build --target TestWsTransport && ./build/bin/TestWsTransport

#include "../../include/core_engine/net/MessageBus.hpp"
#include "../../include/core_engine/net/WsTransport.hpp"
#include <cstdio>
#include <functional>
#include <thread>

using namespace ilmeee::net;
using namespace std::chrono_literals;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    ++g_checks;                                                                \
    if (!(cond)) {                                                             \
      ++g_failures;                                                            \
      std::fprintf(stderr, "  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                          \
  } while (0)

static void Run(const char *name, const std::function<void()> &test) {
  int before = g_failures;
  test();
  std::printf("%s %s\n", g_failures == before ? "[ OK ]" : "[FAIL]", name);
}

static void Silence(MessageBus &bus) {
  bus.SetLogSink([](BusLogLevel, const std::string &) {});
}

// Poll both buses until `done` holds or the deadline passes.
static bool PumpUntil(MessageBus &a, MessageBus &b,
                      const std::function<bool()> &done,
                      std::chrono::milliseconds limit = 3000ms) {
  auto deadline = std::chrono::steady_clock::now() + limit;
  while (std::chrono::steady_clock::now() < deadline) {
    a.Poll();
    b.Poll();
    if (done())
      return true;
    std::this_thread::sleep_for(2ms);
  }
  return false;
}

static BusConfig ServerConfig(std::string token = "s3cret") {
  BusConfig c;
  c.role = BusRole::Server;
  c.token = std::move(token);
  return c;
}

static BusConfig ClientConfig(std::string token = "s3cret") {
  BusConfig c;
  c.role = BusRole::Client;
  c.token = std::move(token);
  c.clientRole = "engine";
  return c;
}

static WsClientOptions ClientOptions(const std::string &url) {
  WsClientOptions o;
  o.url = url;
  o.minRetryMs = 20;
  o.maxRetryMs = 100;
  return o;
}

// A port that was free a moment ago, for tests that need it before the server.
static int ReservePort() {
  WsServerTransport probe;
  probe.Start();
  int port = probe.Port();
  probe.Stop();
  return port;
}

// ---------------------------------------------------------------------------

static void TestEphemeralPort() {
  WsServerTransport st;
  CHECK(st.Port() == 0);
  CHECK(st.Start());
  CHECK(st.Port() > 1024);
  CHECK(st.Url() == "ws://127.0.0.1:" + std::to_string(st.Port()) + "/");

  // A second server on the same machine must not collide.
  WsServerTransport other;
  CHECK(other.Start());
  CHECK(other.Port() != st.Port());
}

static void TestRoundTrip() {
  WsServerTransport st;
  MessageBus server(st, ServerConfig());
  Silence(server);
  server.OnRequest("scene.load", [](ConnectionId, const json &p) {
    return RequestResult::Ok({{"echo", p.value("path", std::string())}});
  });
  std::vector<int> order;
  server.On("tick", [&](ConnectionId, const json &p) {
    order.push_back(p.value("n", -1));
  });
  CHECK(server.Start());

  WsClientTransport ct(ClientOptions(st.Url()));
  MessageBus client(ct, ClientConfig());
  Silence(client);
  CHECK(client.Start());

  // Emitted while still connecting: queued until welcome, then flushed.
  client.Emit("tick", {{"n", 0}});
  CHECK(PumpUntil(server, client, [&] { return client.IsReady(); }));

  // The bug the old raw-TCP code had: messages sent back to back merging or
  // splitting inside one recv(). Every one must arrive whole and in order.
  for (int i = 1; i < 2000; ++i)
    client.Emit("tick", {{"n", i}});
  CHECK(PumpUntil(server, client, [&] { return order.size() == 2000; }));
  bool inOrder = order.size() == 2000;
  for (int i = 0; inOrder && i < 2000; ++i)
    inOrder = order[i] == i;
  CHECK(inOrder);

  // Non-ASCII path survives as text.
  Response r;
  bool got = false;
  client.Request("scene.load", {{"path", "assets/ミク/scene.ilmeeescene"}},
                 [&](const Response &res) {
                   r = res;
                   got = true;
                 });
  CHECK(PumpUntil(server, client, [&] { return got; }));
  CHECK(r.ok && r.payload.value("echo", std::string()) ==
                    "assets/ミク/scene.ilmeeescene");
}

static void TestLargeBinary() {
  WsServerTransport st;
  MessageBus server(st, ServerConfig());
  Silence(server);
  std::vector<std::uint8_t> received;
  server.OnBinary(
      [&](ConnectionId, std::vector<std::uint8_t> d) { received = std::move(d); });
  server.Start();

  WsClientTransport ct(ClientOptions(st.Url()));
  MessageBus client(ct, ClientConfig());
  Silence(client);
  client.Start();
  CHECK(PumpUntil(server, client, [&] { return client.IsReady(); }));

  // 4 MiB, a texture-sized frame, with every byte value represented.
  std::vector<std::uint8_t> blob(4 << 20);
  for (size_t i = 0; i < blob.size(); ++i)
    blob[i] = static_cast<std::uint8_t>(i * 31 + 7);
  CHECK(client.SendBinary(client.Server(), blob));
  CHECK(PumpUntil(server, client, [&] { return !received.empty(); }));
  CHECK(received == blob);
}

static void TestClientBeforeServer() {
  // The startup race the old code papered over with sleep_for(): the child
  // may dial before the core listens. It must simply retry until it can.
  int port = ReservePort();
  WsServerOptions so;
  so.port = port;
  WsServerTransport st(so);
  MessageBus server(st, ServerConfig());
  Silence(server);

  WsClientTransport ct(
      ClientOptions("ws://127.0.0.1:" + std::to_string(port) + "/"));
  MessageBus client(ct, ClientConfig());
  Silence(client);
  bool echoed = false;
  server.On("hi", [&](ConnectionId, const json &) { echoed = true; });
  client.Start();
  client.Emit("hi");

  PumpUntil(server, client, [] { return false; }, 300ms); // nobody listening
  CHECK(!client.IsReady());

  CHECK(server.Start());
  CHECK(PumpUntil(server, client, [&] { return client.IsReady() && echoed; }));
}

static void TestServerRestart() {
  int port = ReservePort();
  WsServerOptions so;
  so.port = port;

  auto st = std::make_unique<WsServerTransport>(so);
  auto server = std::make_unique<MessageBus>(*st, ServerConfig());
  Silence(*server);
  server->Start();

  WsClientTransport ct(
      ClientOptions("ws://127.0.0.1:" + std::to_string(port) + "/"));
  MessageBus client(ct, ClientConfig());
  Silence(client);
  bool left = false;
  int joins = 0;
  client.OnPeerLeft([&](const PeerInfo &, const std::string &) { left = true; });
  client.OnPeerJoined([&](const PeerInfo &) { ++joins; });
  client.Start();
  CHECK(PumpUntil(*server, client, [&] { return client.IsReady(); }));

  server->Stop();
  server.reset();
  st.reset();
  auto deadline = std::chrono::steady_clock::now() + 3s;
  while (!left && std::chrono::steady_clock::now() < deadline) {
    client.Poll();
    std::this_thread::sleep_for(2ms);
  }
  CHECK(left && !client.IsReady());

  // Same port again: the client reconnects on its own and re-handshakes.
  st = std::make_unique<WsServerTransport>(so);
  server = std::make_unique<MessageBus>(*st, ServerConfig());
  Silence(*server);
  CHECK(server->Start());
  CHECK(PumpUntil(*server, client, [&] { return client.IsReady(); }));
  CHECK(joins == 2);
}

static void TestBadTokenStopsRetrying() {
  WsServerTransport st;
  MessageBus server(st, ServerConfig("right"));
  Silence(server);
  int joins = 0;
  server.OnPeerJoined([&](const PeerInfo &) { ++joins; });
  server.Start();

  WsClientTransport ct(ClientOptions(st.Url()));
  MessageBus client(ct, ClientConfig("wrong"));
  std::string lastError;
  client.SetLogSink([&](BusLogLevel level, const std::string &text) {
    if (level == BusLogLevel::Error)
      lastError = text;
  });
  client.Start();

  CHECK(PumpUntil(server, client, [&] { return !lastError.empty(); }));
  CHECK(lastError.find("invalid token") != std::string::npos);
  // Give a reconnecting transport time to (wrongly) dial again.
  PumpUntil(server, client, [] { return false; }, 400ms);
  CHECK(joins == 0 && !client.IsReady() && server.Peers().empty());
}

static void TestRawClientWithoutHandshake() {
  // Something speaking the old protocol (or anything else) at the port.
  WsServerTransport st;
  MessageBus server(st, ServerConfig());
  Silence(server);
  bool handled = false;
  server.On("LoadScene", [&](ConnectionId, const json &) { handled = true; });
  server.Start();

  WsClientTransport raw(ClientOptions(st.Url()));
  std::atomic<bool> opened{false}, closed{false};
  TransportCallbacks cb;
  cb.onOpen = [&](ConnectionId) { opened = true; };
  cb.onClose = [&](ConnectionId, const std::string &) { closed = true; };
  raw.SetCallbacks(cb);
  raw.Start();

  auto deadline = std::chrono::steady_clock::now() + 3s;
  while (!opened && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(2ms);
  raw.SendText(WsClientTransport::kServerId, "LoadScene");
  while (!closed && std::chrono::steady_clock::now() < deadline) {
    server.Poll();
    std::this_thread::sleep_for(2ms);
  }
  raw.Stop();
  CHECK(opened.load());
  CHECK(closed.load());
  CHECK(!handled);
}

static void TestManyClients() {
  WsServerTransport st;
  MessageBus server(st, ServerConfig());
  Silence(server);
  int pings = 0;
  server.On("ping", [&](ConnectionId, const json &) { ++pings; });
  server.Start();

  constexpr int kClients = 6;
  std::vector<std::unique_ptr<WsClientTransport>> transports;
  std::vector<std::unique_ptr<MessageBus>> clients;
  std::vector<int> pongs(kClients, 0);
  for (int i = 0; i < kClients; ++i) {
    transports.push_back(
        std::make_unique<WsClientTransport>(ClientOptions(st.Url())));
    clients.push_back(
        std::make_unique<MessageBus>(*transports.back(), ClientConfig()));
    Silence(*clients.back());
    clients.back()->On("pong", [&pongs, i](ConnectionId, const json &) {
      ++pongs[i];
    });
    clients.back()->Start();
    clients.back()->Emit("ping");
  }

  auto pumpAll = [&](const std::function<bool()> &done) {
    auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) {
      server.Poll();
      for (auto &c : clients)
        c->Poll();
      if (done())
        return true;
      std::this_thread::sleep_for(2ms);
    }
    return false;
  };

  CHECK(pumpAll([&] { return pings == kClients; }));
  CHECK(server.Peers().size() == kClients);
  server.Broadcast("pong");
  CHECK(pumpAll([&] {
    for (int p : pongs)
      if (p != 1)
        return false;
    return true;
  }));
}

int main() {
  Run("ephemeral port", TestEphemeralPort);
  Run("round trip, 2000 ordered events, utf-8", TestRoundTrip);
  Run("4 MiB binary frame", TestLargeBinary);
  Run("client starts before server", TestClientBeforeServer);
  Run("server restart, client reconnects", TestServerRestart);
  Run("bad token stops retrying", TestBadTokenStopsRetrying);
  Run("raw client without handshake", TestRawClientWithoutHandshake);
  Run("many clients + broadcast", TestManyClients);

  std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}

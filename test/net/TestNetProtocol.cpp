// Protocol tests for ilmeee_net: envelope encode/decode and MessageBus
// behaviour over the in-process loopback transport. No GPU, no sockets.
//
//   cmake --build build --target TestNetProtocol && ./build/bin/TestNetProtocol

#include "../../include/core_engine/net/LoopbackTransport.hpp"
#include "../../include/core_engine/net/MessageBus.hpp"
#include <cstdio>
#include <functional>
#include <thread>

using namespace ilmeee::net;

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

// Quiet buses: the expected warnings would drown the test output.
static void Silence(MessageBus &bus) {
  bus.SetLogSink([](BusLogLevel, const std::string &) {});
}

static void PollAll(MessageBus &a, MessageBus &b) {
  // Two rounds: a request needs one poll to be handled, one to be answered.
  for (int i = 0; i < 3; ++i) {
    a.Poll();
    b.Poll();
  }
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

// ---- envelope ------------------------------------------------------------

static void TestEnvelopeRoundTrip() {
  Message ev = Message::Event("scene.changed", {{"path", "a.ilmeeescene"}});
  auto d = Decode(Encode(ev), nullptr);
  CHECK(d && d->kind == MessageKind::Event);
  CHECK(d && d->type == "scene.changed");
  CHECK(d && d->payload["path"] == "a.ilmeeescene");

  Message req = Message::Request("scene.load", 42, {{"path", "x"}});
  d = Decode(Encode(req), nullptr);
  CHECK(d && d->kind == MessageKind::Request && d->id == 42);

  Message ok = Message::Reply(req, {{"meshes", 3}});
  d = Decode(Encode(ok), nullptr);
  CHECK(d && d->kind == MessageKind::Response && d->id == 42 && d->ok);
  CHECK(d && d->payload["meshes"] == 3 && d->type == "scene.load");

  Message bad = Message::Fail(req, "file not found");
  d = Decode(Encode(bad), nullptr);
  CHECK(d && !d->ok && d->error == "file not found");
}

static void TestEnvelopeRejectsGarbage() {
  std::string err;
  CHECK(!Decode("LoadScene", &err) && !err.empty()); // the old raw protocol
  CHECK(!Decode("[1,2]", &err));
  CHECK(!Decode(R"({"type":"x"})", &err));             // no version
  CHECK(!Decode(R"({"v":99,"type":"x"})", &err));      // wrong version
  CHECK(!Decode(R"({"v":1,"type":""})", &err));        // empty type
  CHECK(!Decode(R"({"v":1,"type":"x","id":0})", &err));
  CHECK(!Decode(R"({"v":1,"type":"x","id":1,"re":1})", &err));
  CHECK(!Decode(R"({"v":1,"type":"x","re":1})", &err)); // response w/o ok
  CHECK(!Decode(R"({"v":1,"type":"x","payload":[1]})", &err));
}

// ---- bus -----------------------------------------------------------------

static void TestHandshakeAndEvents() {
  LoopbackServer st;
  LoopbackClient ct(st);
  MessageBus server(st, ServerConfig());
  MessageBus client(ct, ClientConfig());
  Silence(server);
  Silence(client);

  PeerInfo joined;
  server.OnPeerJoined([&](const PeerInfo &p) { joined = p; });
  std::string got;
  server.On("engine.log", [&](ConnectionId, const json &p) {
    got = p.value("text", std::string());
  });
  int clientGot = 0;
  client.On("scene.changed", [&](ConnectionId, const json &) { ++clientGot; });

  CHECK(server.Start());
  CHECK(client.Start());
  // Emitted before the welcome: must be queued, not lost or rejected.
  CHECK(client.Emit("engine.log", {{"text", "early"}}));
  PollAll(server, client);

  CHECK(client.IsReady());
  CHECK(joined.role == "engine" && joined.pid > 0);
  CHECK(server.Peers().size() == 1);
  CHECK(server.PeerByRole("engine") == joined.connection);
  CHECK(server.PeerByRole("hub") == kInvalidConnection);
  CHECK(client.PeerByRole("core") == client.Server());
  CHECK(got == "early");

  server.Broadcast("scene.changed");
  PollAll(server, client);
  CHECK(clientGot == 1);
}

static void TestBadTokenRefused() {
  LoopbackServer st;
  LoopbackClient ct(st);
  MessageBus server(st, ServerConfig("right"));
  MessageBus client(ct, ClientConfig("wrong"));
  Silence(server);
  Silence(client);

  bool joined = false;
  server.OnPeerJoined([&](const PeerInfo &) { joined = true; });
  server.Start();
  client.Start();
  PollAll(server, client);

  CHECK(!joined);
  CHECK(!client.IsReady());
  CHECK(server.Peers().empty());
}

static void TestUnauthenticatedTrafficDropped() {
  // A raw client that skips the hello, like the old TCP code would.
  LoopbackServer st;
  LoopbackClient raw(st);
  MessageBus server(st, ServerConfig());
  Silence(server);
  bool handled = false;
  server.On("engine.stop", [&](ConnectionId, const json &) { handled = true; });
  bool closed = false;
  TransportCallbacks cb;
  cb.onClose = [&](ConnectionId, const std::string &) { closed = true; };
  raw.SetCallbacks(cb);

  server.Start();
  raw.Start();
  raw.SendText(LoopbackClient::kServerId,
               Encode(Message::Event("engine.stop")));
  server.Poll();

  CHECK(!handled);
  CHECK(closed);
}

static void TestRequestResponse() {
  LoopbackServer st;
  LoopbackClient ct(st);
  MessageBus server(st, ServerConfig());
  MessageBus client(ct, ClientConfig());
  Silence(server);
  Silence(client);

  server.OnRequest("scene.load", [](ConnectionId, const json &p) {
    if (p.value("path", std::string()).empty())
      return RequestResult::Fail("path required");
    return RequestResult::Ok({{"meshes", 7}});
  });
  server.OnRequest("boom", [](ConnectionId, const json &) -> RequestResult {
    throw std::runtime_error("handler exploded");
  });

  server.Start();
  client.Start();

  Response r1, r2, r3, r4;
  // Issued before the welcome: queued, then bound to the server on flush.
  client.Request("scene.load", {{"path", "a"}},
                 [&](const Response &r) { r1 = r; });
  PollAll(server, client);
  client.Request("scene.load", json::object(),
                 [&](const Response &r) { r2 = r; });
  client.Request("does.not.exist", json::object(),
                 [&](const Response &r) { r3 = r; });
  client.Request("boom", json::object(), [&](const Response &r) { r4 = r; });
  PollAll(server, client);

  CHECK(r1.ok && r1.payload.value("meshes", 0) == 7);
  CHECK(!r2.ok && r2.error == "path required");
  CHECK(!r3.ok && r3.error.find("unknown request") != std::string::npos);
  CHECK(!r4.ok && r4.error == "handler exploded");

  // Server can ask the client too.
  client.OnRequest("engine.status",
                   [](ConnectionId, const json &) {
                     return RequestResult::Ok({{"fps", 60}});
                   });
  Response r5;
  server.Request(server.Peers()[0].connection, "engine.status",
                 json::object(), [&](const Response &r) { r5 = r; });
  PollAll(server, client);
  CHECK(r5.ok && r5.payload.value("fps", 0) == 60);
}

static void TestTimeoutAndDisconnect() {
  LoopbackServer st;
  LoopbackClient ct(st);
  MessageBus server(st, ServerConfig());
  MessageBus client(ct, ClientConfig());
  Silence(server);
  Silence(client);
  // The server is not polled until after the deadline, so nothing can answer
  // "slow" in time.
  server.Start();
  client.Start();
  PollAll(server, client);

  Response timedOut;
  bool fired = false;
  client.Request("slow", json::object(),
                 [&](const Response &r) {
                   timedOut = r;
                   fired = true;
                 },
                 std::chrono::milliseconds(10));
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  client.Poll();
  CHECK(fired && !timedOut.ok && timedOut.error == "timeout");

  // The late answer must be dropped, not delivered to someone else.
  server.Poll();
  client.Poll();

  Response dropped;
  client.Request("slow", json::object(),
                 [&](const Response &r) { dropped = r; });
  std::string leftReason;
  client.OnPeerLeft(
      [&](const PeerInfo &, const std::string &why) { leftReason = why; });
  st.Stop();
  client.Poll();
  CHECK(!dropped.ok && dropped.error == "peer disconnected");
  CHECK(!client.IsReady());
  CHECK(leftReason == "server stopped");
}

static void TestReconnect() {
  LoopbackServer st;
  LoopbackClient ct(st);
  MessageBus server(st, ServerConfig());
  MessageBus client(ct, ClientConfig());
  Silence(server);
  Silence(client);
  int joins = 0;
  server.OnPeerJoined([&](const PeerInfo &) { ++joins; });

  server.Start();
  client.Start();
  PollAll(server, client);
  ct.Stop(); // drop the connection under the bus, as a network blip would
  PollAll(server, client);
  CHECK(!client.IsReady() && server.Peers().empty());

  ct.Start(); // what a reconnecting transport does
  PollAll(server, client);
  CHECK(client.IsReady() && joins == 2 && server.Peers().size() == 1);
}

static void TestReservedTypes() {
  LoopbackServer st;
  MessageBus server(st, ServerConfig());
  Silence(server);
  bool called = false;
  server.On("hello", [&](ConnectionId, const json &) { called = true; });
  server.Start();
  // A client claiming hello twice must not reach a user handler.
  LoopbackClient ct(st);
  MessageBus client(ct, ClientConfig());
  Silence(client);
  client.Start();
  PollAll(server, client);
  ct.SendText(LoopbackClient::kServerId,
              Encode(Message::Event("hello", {{"protocol", 1}})));
  server.Poll();
  CHECK(!called);
  CHECK(!client.Emit("welcome"));
}

static void TestBinary() {
  LoopbackServer st;
  LoopbackClient ct(st);
  MessageBus server(st, ServerConfig());
  MessageBus client(ct, ClientConfig());
  Silence(server);
  Silence(client);
  std::vector<std::uint8_t> got;
  client.OnBinary([&](ConnectionId, std::vector<std::uint8_t> d) { got = d; });
  server.Start();
  client.Start();
  PollAll(server, client);

  std::vector<std::uint8_t> blob{0, 1, 2, 255};
  CHECK(server.SendBinary(server.Peers()[0].connection, blob));
  client.Poll();
  CHECK(got == blob);
}

int main() {
  Run("envelope round trip", TestEnvelopeRoundTrip);
  Run("envelope rejects garbage", TestEnvelopeRejectsGarbage);
  Run("handshake and events", TestHandshakeAndEvents);
  Run("bad token refused", TestBadTokenRefused);
  Run("unauthenticated traffic dropped", TestUnauthenticatedTrafficDropped);
  Run("request / response", TestRequestResponse);
  Run("timeout and disconnect", TestTimeoutAndDisconnect);
  Run("reconnect", TestReconnect);
  Run("reserved types", TestReservedTypes);
  Run("binary frames", TestBinary);

  std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}

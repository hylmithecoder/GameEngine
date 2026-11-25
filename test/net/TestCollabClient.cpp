// Realtime collaboration: entity codec + conflict rules (always), and two real
// clients against a real server (only when ILMEEE_RT_URL is set).
//
//   cmake --build build --target TestCollabClient && ./build/bin/TestCollabClient
//
//   # online part, server from this repo:
//   (cd server/realtime && ILMEEE_RT_TOKEN=dev RT_DATA_DIR= PORT=9230 cargo run) &
//   ILMEEE_RT_URL=ws://127.0.0.1:9230/ws ILMEEE_RT_TOKEN=dev ./build/bin/TestCollabClient
//   # or against production:
//   ILMEEE_RT_URL=wss://realtimeprotokol.ilmeee.com/ws ILMEEE_RT_TOKEN=... ./build/bin/TestCollabClient

#include "../../include/core_engine/CollabCodec.hpp"
#include "../../include/core_engine/net/CollabClient.hpp"
#include "../../include/core_engine/net/WsTransport.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>
#include <unistd.h>

using namespace ilmeee;
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

// ---- offline ----------------------------------------------------------------

static void TestBase64() {
  for (std::string s : {std::string(), std::string("f"), std::string("fo"),
                        std::string("foo"), std::string("foob"),
                        std::string("\0\xff\x10 binary\x80", 12)}) {
    std::string back;
    CHECK(collab::Base64Decode(collab::Base64Encode(s), back));
    CHECK(back == s);
  }
  CHECK(collab::Base64Encode("foobar") == "Zm9vYmFy");
  CHECK(collab::Base64Encode("fo") == "Zm8=");
  std::string out;
  CHECK(!collab::Base64Decode("abc", out));   // length
  CHECK(!collab::Base64Decode("a=bc", out));  // pad in the middle
  CHECK(!collab::Base64Decode("ab!c", out));  // alphabet
}

static void TestKeys() {
  const uint64_t id = 0x0123456789abcdefULL;
  CHECK(collab::ObjectKey(id) == "o0123456789abcdef");
  CHECK(collab::ParseObjectKey(collab::ObjectKey(id)) == id);
  CHECK(collab::ObjectKey(0).empty());
  CHECK(collab::ParseObjectKey("env") == 0);
  CHECK(collab::ParseObjectKey("o0123456789abcdeg") == 0);
  CHECK(collab::ParseObjectKey("") == 0);
}

static void TestEntityCodec() {
  SceneEntity e;
  e.objectId = 0xfeedULL;
  e.name = "Key Light";
  e.kind = PrimitiveKind::Light;
  e.lightColor = {0.2f, 0.4f, 0.8f};
  e.lightIntensity = 3.5f;
  e.position = {1, 2, 3};
  e.scale = {2, 2, 2};
  e.parent = 4;
  e.movementRig.camera = 2;
  NativeScriptBinding b;
  b.source = "scripts/Spin.cpp";
  b.className = "Spin";
  ScriptValue v;
  v.name = "target";
  v.type = ScriptValueType::Object;
  v.object = 0xbeefULL;
  b.values.push_back(v);
  e.scripts.push_back(b);

  json data = collab::EncodeEntityData(e, 0xaaULL, 0xbbULL);
  CHECK(data["parent"] == collab::ObjectKey(0xaa));
  CHECK(data["rigCamera"] == collab::ObjectKey(0xbb));
  CHECK(data["name"] == "Key Light");

  collab::EntityData back;
  CHECK(collab::DecodeEntityData(data, back));
  CHECK(back.parentId == 0xaa);
  CHECK(back.rigCameraId == 0xbb);
  CHECK(back.entity.objectId == 0xfeed);
  CHECK(back.entity.name == "Key Light");
  CHECK(back.entity.kind == PrimitiveKind::Light);
  CHECK(back.entity.lightIntensity == 3.5f);
  CHECK(back.entity.lightColor.z == 0.8f);
  // transform / links are not part of the data slot
  CHECK(back.entity.position == glm::vec3(0.0f));
  CHECK(back.entity.scale == glm::vec3(1.0f));
  CHECK(back.entity.parent == -1);
  CHECK(back.entity.movementRig.camera == -1);
  CHECK(back.entity.scripts.size() == 1);
  CHECK(back.entity.scripts[0].values.size() == 1 &&
        back.entity.scripts[0].values[0].object == 0xbeef);

  // same object, moved: the data slot must not change (drag = xform only)
  SceneEntity moved = e;
  moved.position = {9, 9, 9};
  CHECK(collab::EncodeEntityData(moved, 0xaa, 0xbb) == data);

  collab::EntityData junk;
  CHECK(!collab::DecodeEntityData(json{{"blob", "!!!"}}, junk));
  CHECK(!collab::DecodeEntityData(json{{"blob", collab::Base64Encode("ILMSxx")}}, junk));
  CHECK(!collab::DecodeEntityData(json::array(), junk));
}

static void TestXformAndEnvironment() {
  collab::Xform x{{1, 2, 3}, {0, 90, 0}, {1, 1, 2}};
  collab::Xform back;
  CHECK(collab::DecodeXform(collab::EncodeXform(x), back));
  CHECK(back == x);
  CHECK(!collab::DecodeXform(json{{"p", {1, 2}}}, back));
  CHECK(!collab::DecodeXform(json{{"p", {1, 2, "x"}}, {"r", {0, 0, 0}}, {"s", {1, 1, 1}}}, back));

  IlmeeeScene s;
  s.backgroundColor = {0.1f, 0.2f, 0.3f, 1.0f};
  s.bloom.intensity = 2.25f;
  IlmeeeScene out;
  CHECK(collab::DecodeEnvironment(collab::EncodeEnvironment(s), out));
  CHECK(out.backgroundColor == s.backgroundColor);
  CHECK(out.bloom.intensity == 2.25f);
}

static CollabOp Op(uint32_t from, std::string key, std::string slot, bool del = false) {
  CollabOp op;
  op.from = from;
  op.key = std::move(key);
  op.slot = std::move(slot);
  op.del = del;
  return op;
}

static void TestLww() {
  using V = CollabLww::Verdict;
  CollabLww lww;
  const uint32_t me = 7, other = 9;

  CHECK(lww.Judge(Op(other, "a", "data"), me) == V::Apply);

  // our write in flight: an earlier-sequenced remote write to that slot loses
  lww.NoteSent("a", "xform");
  CHECK(lww.HasPending("a"));
  CHECK(lww.Judge(Op(other, "a", "xform"), me) == V::Skip);
  // ...but another slot of the same key still applies
  CHECK(lww.Judge(Op(other, "a", "data"), me) == V::Apply);
  // our echo releases it; later remote writes win again
  CHECK(lww.Judge(Op(me, "a", "xform"), me) == V::OwnEcho);
  CHECK(!lww.HasPending("a"));
  CHECK(lww.Judge(Op(other, "a", "xform"), me) == V::Apply);

  // two writes in flight need two echoes
  lww.NoteSent("b", "xform");
  lww.NoteSent("b", "xform");
  CHECK(lww.Judge(Op(me, "b", "xform"), me) == V::OwnEcho);
  CHECK(lww.Judge(Op(other, "b", "xform"), me) == V::Skip);
  CHECK(lww.Judge(Op(me, "b", "xform"), me) == V::OwnEcho);
  CHECK(lww.Judge(Op(other, "b", "xform"), me) == V::Apply);

  // remote delete vs our in-flight put: resend the whole object
  lww.NoteSent("c", "data");
  CHECK(lww.Judge(Op(other, "c", "", true), me) == V::SkipResend);
  CHECK(lww.Judge(Op(me, "c", "data"), me) == V::OwnEcho);
  CHECK(lww.Judge(Op(other, "c", "", true), me) == V::Apply);

  // our delete in flight: remote puts and deletes before it are moot
  lww.NoteSent("d", "");
  CHECK(lww.Judge(Op(other, "d", "data"), me) == V::Skip);
  CHECK(lww.Judge(Op(other, "d", "", true), me) == V::Skip);
  CHECK(lww.Judge(Op(me, "d", "", true), me) == V::OwnEcho);
  CHECK(!lww.HasPending("d"));

  lww.NoteSent("e", "data");
  lww.Reset();
  CHECK(!lww.HasPending("e"));
}

// ---- online -----------------------------------------------------------------

struct Peer {
  CollabClient client;
  std::vector<CollabOp> applied;
  std::vector<CollabOp> joinState;
  bool joined = false;
  bool fresh = false;
  int resends = 0;

  explicit Peer(const std::string &name, const std::string &room) {
    client.onJoined = [this](const std::vector<CollabOp> &s, bool f) {
      joinState = s;
      fresh = f;
      joined = true;
    };
    client.onOp = [this](const CollabOp &op) { applied.push_back(op); };
    client.onResend = [this](const std::string &) { ++resends; };
    client.onLog = [](const std::string &, bool) {};
    CollabOptions o;
    o.url = std::getenv("ILMEEE_RT_URL");
    if (const char *t = std::getenv("ILMEEE_RT_TOKEN"))
      o.token = t;
    o.room = room;
    o.name = name;
    client.Start(o);
  }
};

template <class... P>
static bool Pump(const std::function<bool()> &done, P &...peers) {
  const auto deadline = std::chrono::steady_clock::now() + 8s;
  while (std::chrono::steady_clock::now() < deadline) {
    (peers.client.Poll(), ...);
    if (done())
      return true;
    std::this_thread::sleep_for(5ms);
  }
  return false;
}

static void TestHttpBase() {
  CHECK(CollabHttpBase("wss://realtimeprotokol.ilmeee.com/ws") ==
        "https://realtimeprotokol.ilmeee.com");
  CHECK(CollabHttpBase("ws://127.0.0.1:9230/ws") == "http://127.0.0.1:9230");
  CHECK(CollabHttpBase("ws://host") == "http://host");
  CHECK(CollabHttpBase("wss://host:8443/a/b?x=1") == "https://host:8443");
  CHECK(CollabHttpBase("https://host/ws").empty());
  CHECK(CollabHttpBase("wss:///ws").empty());
  const auto bad = FetchCollabRooms("ftp://x", "");
  CHECK(!bad.ok && !bad.error.empty());
}

static void TestOnline() {
  const std::string room = "cpp-test-" + std::to_string(getpid()) + "-" +
                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 100000);
  Peer a("Alice", room);
  CHECK(Pump([&] { return a.joined; }, a));
  CHECK(a.client.State() == CollabState::Joined);
  CHECK(a.fresh);
  CHECK(a.client.Self() != 0);

  Peer b("Bob", room);
  CHECK(Pump([&] { return b.joined && a.client.Peers().size() == 1; }, a, b));
  CHECK(b.client.Peers().size() == 1 && b.client.Peers()[0].name == "Alice");

  // A writes; B applies it, A only gets its echo (not applied twice)
  SceneEntity cube;
  cube.objectId = 0x1234;
  cube.name = "Cube";
  const std::string key = collab::ObjectKey(cube.objectId);
  CHECK(a.client.Put(key, collab::kSlotData, collab::EncodeEntityData(cube, 0, 0)));
  CHECK(a.client.HasPending(key));
  CHECK(Pump([&] { return !b.applied.empty() && !a.client.HasPending(key); }, a, b));
  CHECK(a.applied.empty());
  collab::EntityData got;
  CHECK(!b.applied.empty() && collab::DecodeEntityData(b.applied[0].data, got) &&
        got.entity.name == "Cube");

  // concurrent writes to one slot: both must converge to the server's last
  a.applied.clear();
  b.applied.clear();
  collab::Xform xa{{1, 0, 0}, {}, {1, 1, 1}}, xb{{0, 2, 0}, {}, {1, 1, 1}};
  CHECK(a.client.Put(key, collab::kSlotXform, collab::EncodeXform(xa)));
  CHECK(b.client.Put(key, collab::kSlotXform, collab::EncodeXform(xb)));
  CHECK(Pump([&] { return !a.client.HasPending(key) && !b.client.HasPending(key); }, a, b));
  // Each side's final value = its own unless it applied the other's.
  collab::Xform finalA = xa, finalB = xb, tmp;
  for (auto &op : a.applied)
    if (collab::DecodeXform(op.data, tmp)) finalA = tmp;
  for (auto &op : b.applied)
    if (collab::DecodeXform(op.data, tmp)) finalB = tmp;
  CHECK(finalA == finalB);
  // exactly one side yielded
  CHECK(a.applied.size() + b.applied.size() == 1);

  // presence
  CHECK(a.client.SetPresence({{"selection", key}}));
  CHECK(Pump([&] {
    return !b.client.Peers().empty() && b.client.Peers()[0].presence.is_object() &&
           b.client.Peers()[0].presence.value("selection", std::string()) == key;
  }, a, b));

  // late joiner gets the stored state, data before xform
  Peer c("Carol", room);
  CHECK(Pump([&] { return c.joined; }, a, b, c));
  CHECK(!c.fresh);
  CHECK(c.joinState.size() == 2);
  CHECK(c.client.Peers().size() == 2);

  // delete
  b.applied.clear();
  c.applied.clear();
  CHECK(a.client.Delete(key));
  CHECK(Pump([&] { return !b.applied.empty() && !c.applied.empty(); }, a, b, c));
  CHECK(!b.applied.empty() && b.applied[0].del);

  // edit locks: A holds, B queues, A releases -> B, B leaves -> free
  {
    const std::string lk = "o-lock-test";
    CHECK(a.client.AcquireLock(lk));
    CHECK(Pump([&] { return b.client.LockHolder(lk) == a.client.Self(); }, a, b, c));
    CHECK(a.client.LockHolder(lk) == a.client.Self());
    CHECK(b.client.AcquireLock(lk));
    CHECK(Pump([&] { return b.client.QueuePosition(lk) == 1; }, a, b, c));
    CHECK(c.client.Locks().count(lk) == 1 && c.client.Locks().at(lk).queue.size() == 1);
    CHECK(a.client.ReleaseLock(lk));
    CHECK(Pump([&] {
      return c.client.LockHolder(lk) == b.client.Self() &&
             b.client.LockHolder(lk) == b.client.Self();
    }, a, b, c));
    CHECK(b.client.QueuePosition(lk) == 0);
    CHECK(b.client.ReleaseLock(lk));
    CHECK(Pump([&] { return a.client.Locks().count(lk) == 0; }, a, b, c));
  }

  // room directory (Hub): our room is listed with its three people
  {
    const auto list = FetchCollabRooms(std::getenv("ILMEEE_RT_URL"),
                                       std::getenv("ILMEEE_RT_TOKEN") ? std::getenv("ILMEEE_RT_TOKEN") : "");
    CHECK(list.ok);
    const CollabRoomInfo *mine = nullptr;
    for (const auto &r : list.rooms)
      if (r.name == room)
        mine = &r;
    CHECK(mine && mine->peers.size() == 3);
    if (std::getenv("ILMEEE_RT_TOKEN") && *std::getenv("ILMEEE_RT_TOKEN")) {
      const auto denied = FetchCollabRooms(std::getenv("ILMEEE_RT_URL"), "definitely-wrong");
      CHECK(!denied.ok && denied.status == 401);
    }
  }

  // leaving
  c.client.Stop();
  CHECK(c.client.State() == CollabState::Offline);
  CHECK(Pump([&] { return a.client.Peers().size() == 1; }, a, b));

  // bad token is a hard failure, not an endless reconnect
  if (std::getenv("ILMEEE_RT_TOKEN") && *std::getenv("ILMEEE_RT_TOKEN")) {
    CollabClient bad;
    bad.onLog = [](const std::string &, bool) {};
    CollabOptions o;
    o.url = std::getenv("ILMEEE_RT_URL");
    o.token = "definitely-wrong";
    o.room = room;
    o.name = "Mallory";
    bad.Start(o);
    const auto deadline = std::chrono::steady_clock::now() + 8s;
    while (bad.State() != CollabState::Failed && std::chrono::steady_clock::now() < deadline) {
      bad.Poll();
      std::this_thread::sleep_for(5ms);
    }
    CHECK(bad.State() == CollabState::Failed);
    CHECK(bad.LastError().find("token") != std::string::npos);
  }

  // invalid room name
  Peer bad("X", "../nope");
  CHECK(Pump([&] { return bad.client.State() == CollabState::Failed; }, bad));
}

int main() {
  Run("base64", TestBase64);
  Run("object keys", TestKeys);
  Run("entity data codec", TestEntityCodec);
  Run("xform + environment codec", TestXformAndEnvironment);
  Run("last-writer-wins verdicts", TestLww);
  Run("http base + room list errors", TestHttpBase);
  if (std::getenv("ILMEEE_RT_URL")) {
    std::printf("online: %s (TLS %s)\n", std::getenv("ILMEEE_RT_URL"),
                WsTlsAvailable() ? "available" : "not built");
    Run("online: join, ops, conflicts, presence, replay, delete", TestOnline);
  } else {
    std::printf("[SKIP] online tests (set ILMEEE_RT_URL)\n");
  }
  std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}

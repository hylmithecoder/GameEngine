// End-to-end: plays the core's side exactly as ApplicationManager does
// (server bus, token, --ipc-url + env) against the real HandlerIlmeeeEngine
// binary next to this one. Needs a display for the handler's GTK splash; run
// it headless with
//
//   env -u WAYLAND_DISPLAY xvfb-run -a ./build/bin/TestEngineIpc

#include "../../include/core_engine/UserDataDir.hpp"
#include "../../include/core_engine/net/MessageBus.hpp"
#include "../../include/core_engine/net/Protocol.hpp"
#include "../../include/core_engine/net/WsTransport.hpp"
#include <csignal>
#include <cstdio>
#include <cstring>
#include <functional>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char **environ;

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

// The core under test: same wiring as ApplicationManager::StartIpcServer.
struct FakeCore {
  WsServerTransport transport;
  std::string token = proto::GenerateToken();
  MessageBus bus{transport, [this] {
                   BusConfig c;
                   c.role = BusRole::Server;
                   c.token = token;
                   return c;
                 }()};
  PeerInfo engine;
  std::string leftReason;
  bool left = false;

  FakeCore() {
    bus.SetLogSink([](BusLogLevel, const std::string &) {});
    bus.OnPeerJoined([this](const PeerInfo &p) {
      if (p.role != proto::kRoleEngine)
        return;
      engine = p;
      bus.Emit(p.connection, proto::kSessionInit, {{"project", ""}});
    });
    bus.OnPeerLeft([this](const PeerInfo &, const std::string &why) {
      left = true;
      leftReason = why;
    });
  }

  bool PollUntil(const std::function<bool()> &done,
                 std::chrono::milliseconds limit) {
    auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
      bus.Poll();
      if (done())
        return true;
      std::this_thread::sleep_for(10ms);
    }
    return false;
  }
};

// Same argv/env handover as ApplicationManager::LaunchEngine.
static pid_t SpawnHandler(const std::string &url, const std::string &token) {
  std::string exe = ilmeee::SiblingExecutable("HandlerIlmeeeEngine");
  std::string name = "HandlerIlmeeeEngine", arg = proto::kIpcUrlArg, u = url;
  std::vector<char *> argv = {name.data(), arg.data(), u.data(), nullptr};
  std::vector<std::string> envs;
  for (char **e = environ; e && *e; ++e)
    envs.emplace_back(*e);
  envs.push_back(std::string(proto::kIpcTokenEnv) + "=" + token);
  std::vector<char *> envp;
  for (auto &s : envs)
    envp.push_back(s.data());
  envp.push_back(nullptr);

  pid_t pid = fork();
  if (pid == 0) {
    execve(exe.c_str(), argv.data(), envp.data());
    _exit(127);
  }
  return pid;
}

// -1 if still running after `limit`, else the exit code (or 128+signal).
static int WaitExit(pid_t pid, std::chrono::milliseconds limit) {
  auto deadline = std::chrono::steady_clock::now() + limit;
  while (std::chrono::steady_clock::now() < deadline) {
    int status = 0;
    if (waitpid(pid, &status, WNOHANG) == pid)
      return WIFEXITED(status) ? WEXITSTATUS(status)
                               : 128 + WTERMSIG(status);
    std::this_thread::sleep_for(20ms);
  }
  return -1;
}

static void Kill(pid_t pid) {
  kill(pid, SIGKILL);
  waitpid(pid, nullptr, 0);
}

// ---------------------------------------------------------------------------

static void TestJoinAndStop() {
  FakeCore core;
  CHECK(core.bus.Start());
  pid_t pid = SpawnHandler(core.transport.Url(), core.token);

  // The splash steps take ~3.5 s by design before the handler dials in.
  bool joined = core.PollUntil(
      [&] { return core.engine.connection != kInvalidConnection; }, 20s);
  CHECK(joined);
  CHECK(core.engine.role == proto::kRoleEngine);
  CHECK(core.engine.pid == pid);

  CHECK(core.bus.Emit(core.engine.connection, proto::kEngineStop));
  int code = WaitExit(pid, 3s);
  CHECK(code == 0);
  if (code == -1)
    Kill(pid);
  core.PollUntil([&] { return core.left; }, 2s);
  CHECK(core.left);
}

static void TestHandlerExitsWhenCoreGoesAway() {
  auto core = std::make_unique<FakeCore>();
  CHECK(core->bus.Start());
  pid_t pid = SpawnHandler(core->transport.Url(), core->token);
  CHECK(core->PollUntil(
      [&] { return core->engine.connection != kInvalidConnection; }, 20s));

  core.reset(); // the editor crashed / was killed
  int code = WaitExit(pid, 3s);
  CHECK(code == 0);
  if (code == -1)
    Kill(pid);
}

static void TestWrongTokenIsRefused() {
  FakeCore core;
  CHECK(core.bus.Start());
  pid_t pid = SpawnHandler(core.transport.Url(), "not-the-token");
  // Give it the full startup time; it must never be admitted.
  core.PollUntil([] { return false; }, 6s);
  CHECK(core.engine.connection == kInvalidConnection);
  CHECK(core.bus.Peers().empty());
  Kill(pid);
}

int main() {
  Run("handler joins as engine, engine.stop exits cleanly", TestJoinAndStop);
  Run("handler exits when the core goes away",
      TestHandlerExitsWhenCoreGoesAway);
  Run("handler with a wrong token is refused", TestWrongTokenIsRefused);

  std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}

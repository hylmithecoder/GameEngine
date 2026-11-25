// ilmeee::Process on the current platform. The test spawns itself in child
// modes, so it needs no shell and runs the same on Linux and Windows (CI).
#include "../include/core_engine/Process.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                            \
  do {                                                                         \
    ++g_checks;                                                                \
    if (!(cond)) {                                                             \
      ++g_failed;                                                              \
      std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; \
    }                                                                          \
  } while (0)

static std::string ReadAll(ilmeee::Process &p) {
  std::string out;
  char buf[256];
  long n;
  while ((n = p.Read(buf, sizeof buf)) > 0)
    out.append(buf, (size_t)n);
  return out;
}

int main(int argc, char **argv) {
  // ---- child modes ----
  if (argc >= 2 && std::strcmp(argv[1], "--echo") == 0) {
    for (int i = 2; i < argc; ++i)
      std::printf("[%s]", argv[i]);
    const char *v = std::getenv("ILMEEE_PROCESS_TEST");
    std::printf(" env=%s\n", v ? v : "(unset)");
    std::fflush(stdout);
    std::fprintf(stderr, "to-stderr\n");
    return 7;
  }
  if (argc >= 2 && std::strcmp(argv[1], "--sleep") == 0) {
    std::this_thread::sleep_for(std::chrono::seconds(30));
    return 0;
  }

  // ---- quoting (Windows rules, testable anywhere) ----
  using ilmeee::QuoteWindowsArg;
  CHECK(QuoteWindowsArg("plain") == "plain");
  CHECK(QuoteWindowsArg("") == "\"\"");
  CHECK(QuoteWindowsArg("a b") == "\"a b\"");
  CHECK(QuoteWindowsArg("say \"hi\"") == "\"say \\\"hi\\\"\"");
  CHECK(QuoteWindowsArg("C:\\dir with space\\") == "\"C:\\dir with space\\\\\"");
  CHECK(QuoteWindowsArg("C:\\no\\space\\") == "C:\\no\\space\\");

  const std::string self = ilmeee::CurrentExecutablePath();
  CHECK(!self.empty());

  // ---- output capture, args with spaces/quotes, env, exit code ----
  {
    ilmeee::Process p;
    ilmeee::ProcessOptions o;
    o.program = self;
    o.args = {"--echo", "one", "two words", "q\"uote", "trail\\"};
    o.env = {{"ILMEEE_PROCESS_TEST", "hello world"}};
    std::string err;
    CHECK(p.Start(o, err));
    CHECK(p.Pid() > 0);
    const std::string out = ReadAll(p);
    CHECK(out.find("[one][two words][q\"uote][trail\\] env=hello world") !=
          std::string::npos);
    CHECK(out.find("to-stderr") != std::string::npos);
    CHECK(p.Wait() == 7);
    CHECK(p.Wait() == 7); // idempotent
    if (g_failed)
      std::cerr << "  child output: " << out << "\n  error: " << err << "\n";
  }

  // ---- terminate a running child ----
  {
    ilmeee::Process p;
    ilmeee::ProcessOptions o;
    o.program = self;
    o.args = {"--sleep"};
    std::string err;
    CHECK(p.Start(o, err));
    const auto t0 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    p.Terminate();
    ReadAll(p); // EOF once the child is gone
    const int code = p.Wait();
    CHECK(code != 0);
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10));
    p.Terminate(); // after the reap: must be a no-op
  }

  // ---- TryWait: false while running, the exit code once done ----
  {
    ilmeee::Process p;
    ilmeee::ProcessOptions o;
    o.program = self;
    o.args = {"--echo"};
    o.captureOutput = false;
    std::string err;
    CHECK(p.Start(o, err));
    int code = -1;
    bool done = false;
    for (int i = 0; i < 500 && !(done = p.TryWait(code)); ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(done);
    CHECK(code == 7);
    CHECK(p.TryWait(code) && code == 7); // repeatable after the reap
  }
  {
    ilmeee::Process p;
    ilmeee::ProcessOptions o;
    o.program = self;
    o.args = {"--sleep"};
    std::string err;
    CHECK(p.Start(o, err));
    int code = -1;
    CHECK(!p.TryWait(code)); // still sleeping
    p.Terminate();
    ReadAll(p);
    CHECK(p.Wait() != 0);
  }

  // ---- missing program ----
  {
    ilmeee::Process p;
    ilmeee::ProcessOptions o;
    o.program = "ilmeee-definitely-not-a-program";
    std::string err;
    const bool started = p.Start(o, err);
    // POSIX forks first and the exec fails in the child (exit 127);
    // Windows fails in CreateProcess.
    if (started) {
      ReadAll(p);
      CHECK(p.Wait() == 127);
    } else {
      CHECK(!err.empty());
    }
  }

  std::cout << (g_checks - g_failed) << "/" << g_checks << " checks passed\n";
  return g_failed ? 1 : 0;
}

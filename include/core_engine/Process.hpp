#pragma once
// Portable child process with captured output (POSIX fork/exec, Windows
// CreateProcessW). Replaces the hand-rolled fork/execvp/pipe/waitpid code so
// the Hub (and later the editor's launcher/build tools) runs on Windows too.
//
//   ilmeee::Process p;
//   ilmeee::ProcessOptions o;
//   o.program = "/path/to/GameEngineSDL";      // or a bare name: PATH search
//   o.args = {"--project", path};
//   o.env = {{"ILMEEE_MCP_TOKEN", token}};      // added to the parent's env
//   std::string err;
//   if (!p.Start(o, err)) ...
//   char buf[4096];
//   while (long n = p.Read(buf, sizeof buf)) ...  // stdout+stderr, 0 = EOF
//   int code = p.Wait();
//
// Read() and Wait() block; call them from a reader thread. Terminate() may be
// called from any thread while another one is blocked in Read()/Wait().
// Strings are UTF-8 on every platform.

#include <atomic>
#include <string>
#include <utility>
#include <vector>

namespace ilmeee {

struct ProcessOptions {
  std::string program;
  std::vector<std::string> args; // argv[1..]
  std::vector<std::pair<std::string, std::string>> env; // set/override
  std::string workingDir;      // empty = inherit
  bool captureOutput = true;   // stdout+stderr merged into Read()
};

class Process {
public:
  Process();
  ~Process(); // closes handles; does not kill a still-running child
  Process(const Process &) = delete;
  Process &operator=(const Process &) = delete;

  bool Start(const ProcessOptions &options, std::string &error);
  // Bytes read, 0 at EOF (child closed its output), -1 on error.
  long Read(char *buffer, unsigned long size);
  // Exit code; on POSIX a signal death returns 128 + signal number.
  int Wait();
  // Non-blocking Wait(): true (and the exit code) once the child has ended.
  bool TryWait(int &exitCode);
  // Ask the child to stop (SIGTERM / TerminateProcess). Safe to repeat.
  void Terminate();
  long Pid() const { return pid_; }
  bool Started() const { return pid_ > 0; }

private:
  long pid_ = -1;
  std::atomic<bool> waited_{false};
  int exitCode_ = -1;
#ifdef _WIN32
  void *process_ = nullptr; // HANDLE
  void *readPipe_ = nullptr;
#else
  int readFd_ = -1;
#endif
};

// Absolute path of the running executable ("" if unknown).
std::string CurrentExecutablePath();

// Windows command-line quoting for one argument (CommandLineToArgvW rules).
// Exposed for tests; used by Start() on Windows.
std::string QuoteWindowsArg(const std::string &arg);

} // namespace ilmeee

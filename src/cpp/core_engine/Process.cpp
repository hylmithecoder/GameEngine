#include "../../../include/core_engine/Process.hpp"

#include <cstring>
#include <filesystem>

#ifdef _WIN32
#include "../../../include/core_engine/Platform.hpp"
#include <cwctype>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace ilmeee {

std::string QuoteWindowsArg(const std::string &arg) {
  if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos)
    return arg;
  std::string out = "\"";
  for (size_t i = 0;; ++i) {
    size_t backslashes = 0;
    while (i < arg.size() && arg[i] == '\\') {
      ++i;
      ++backslashes;
    }
    if (i == arg.size()) {
      out.append(backslashes * 2, '\\'); // they precede the closing quote
      break;
    }
    if (arg[i] == '"') {
      out.append(backslashes * 2 + 1, '\\');
      out += '"';
    } else {
      out.append(backslashes, '\\');
      out += arg[i];
    }
  }
  out += '"';
  return out;
}

Process::Process() = default;

#ifdef _WIN32

namespace {
std::wstring Wide(const std::string &s) {
  if (s.empty())
    return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w((size_t)n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}

std::string Narrow(const std::wstring &w) {
  if (w.empty())
    return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0,
                                    nullptr, nullptr);
  std::string s((size_t)n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}

std::string LastErrorText(const char *what) {
  const DWORD code = GetLastError();
  wchar_t *msg = nullptr;
  FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                     FORMAT_MESSAGE_IGNORE_INSERTS,
                 nullptr, code, 0, (wchar_t *)&msg, 0, nullptr);
  std::string text = std::string(what) + " failed (" + std::to_string(code) + ")";
  if (msg) {
    std::string m = Narrow(msg);
    while (!m.empty() && (m.back() == '\n' || m.back() == '\r' || m.back() == ' '))
      m.pop_back();
    text += ": " + m;
    LocalFree(msg);
  }
  return text;
}

bool SameKey(const std::wstring &a, const std::wstring &b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::towupper(a[i]) != std::towupper(b[i]))
      return false;
  return true;
}

// Parent environment with `extra` set/overridden (names are case-insensitive).
std::wstring EnvironmentBlock(const std::vector<std::pair<std::string, std::string>> &extra) {
  std::vector<std::wstring> vars;
  if (wchar_t *block = GetEnvironmentStringsW()) {
    for (const wchar_t *p = block; *p; p += wcslen(p) + 1)
      vars.emplace_back(p);
    FreeEnvironmentStringsW(block);
  }
  for (const auto &[k, v] : extra) {
    const std::wstring key = Wide(k);
    bool replaced = false;
    for (auto &var : vars) {
      const size_t eq = var.find(L'=', 1); // "=C:=..." entries start with '='
      if (eq != std::wstring::npos && SameKey(var.substr(0, eq), key)) {
        var = key + L"=" + Wide(v);
        replaced = true;
      }
    }
    if (!replaced)
      vars.push_back(key + L"=" + Wide(v));
  }
  std::wstring out;
  for (const auto &var : vars) {
    out += var;
    out += L'\0';
  }
  out += L'\0';
  return out;
}
} // namespace

Process::~Process() {
  if (process_)
    CloseHandle((HANDLE)process_);
  if (readPipe_)
    CloseHandle((HANDLE)readPipe_);
}

bool Process::Start(const ProcessOptions &o, std::string &error) {
  if (pid_ > 0) {
    error = "process already started";
    return false;
  }
  std::string cmd = QuoteWindowsArg(o.program);
  for (const auto &a : o.args)
    cmd += " " + QuoteWindowsArg(a);
  std::wstring wcmd = Wide(cmd);
  std::wstring env = EnvironmentBlock(o.env);
  std::wstring cwd = Wide(o.workingDir);

  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE rd = nullptr, wr = nullptr, nul = INVALID_HANDLE_VALUE;
  STARTUPINFOEXW si{};
  si.StartupInfo.cb = sizeof(si);
  std::vector<char> attrBuf;
  HANDLE inherit[2] = {};
  DWORD flags = CREATE_UNICODE_ENVIRONMENT;
  BOOL inheritHandles = FALSE;

  auto cleanup = [&] {
    if (si.lpAttributeList)
      DeleteProcThreadAttributeList(si.lpAttributeList);
    if (wr)
      CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE)
      CloseHandle(nul);
  };

  if (o.captureOutput) {
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
      error = LastErrorText("CreatePipe");
      return false;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                      OPEN_EXISTING, 0, nullptr);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdOutput = wr;
    si.StartupInfo.hStdError = wr;
    si.StartupInfo.hStdInput = nul != INVALID_HANDLE_VALUE ? nul : nullptr;
    // Inherit exactly these handles, so concurrent spawns never leak each
    // other's pipe ends (which would keep a pipe from reaching EOF).
    inherit[0] = wr;
    const DWORD count = nul != INVALID_HANDLE_VALUE ? 2 : 1;
    if (count == 2)
      inherit[1] = nul;
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    attrBuf.resize(size);
    si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)attrBuf.data();
    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &size) ||
        !UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   inherit, count * sizeof(HANDLE), nullptr, nullptr)) {
      error = LastErrorText("UpdateProcThreadAttribute");
      si.lpAttributeList = nullptr;
      cleanup();
      CloseHandle(rd);
      return false;
    }
    flags |= EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW;
    inheritHandles = TRUE;
  }

  PROCESS_INFORMATION pi{};
  const BOOL ok = CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, inheritHandles,
                                 flags, env.data(), cwd.empty() ? nullptr : cwd.c_str(),
                                 &si.StartupInfo, &pi);
  if (!ok) {
    error = LastErrorText(("CreateProcess(" + o.program + ")").c_str());
    cleanup();
    if (rd)
      CloseHandle(rd);
    return false;
  }
  cleanup(); // the child has its copies; ours must close for EOF to happen
  CloseHandle(pi.hThread);
  process_ = pi.hProcess;
  readPipe_ = rd;
  pid_ = (long)pi.dwProcessId;
  return true;
}

long Process::Read(char *buffer, unsigned long size) {
  if (!readPipe_)
    return 0;
  DWORD n = 0;
  if (!ReadFile((HANDLE)readPipe_, buffer, size, &n, nullptr))
    return GetLastError() == ERROR_BROKEN_PIPE ? 0 : -1;
  return (long)n;
}

int Process::Wait() {
  if (!process_)
    return -1;
  if (!waited_) {
    WaitForSingleObject((HANDLE)process_, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess((HANDLE)process_, &code);
    exitCode_ = (int)code;
    waited_ = true;
  }
  return exitCode_;
}

bool Process::TryWait(int &exitCode) {
  if (!process_)
    return false;
  if (!waited_ && WaitForSingleObject((HANDLE)process_, 0) != WAIT_OBJECT_0)
    return false;
  exitCode = Wait();
  return true;
}

void Process::Terminate() {
  if (process_ && !waited_ && WaitForSingleObject((HANDLE)process_, 0) == WAIT_TIMEOUT)
    TerminateProcess((HANDLE)process_, 1);
}

std::string CurrentExecutablePath() {
  std::wstring buf(32768, L'\0');
  const DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
  if (n == 0 || n >= buf.size())
    return {};
  buf.resize(n);
  return Narrow(buf);
}

#else // POSIX

Process::~Process() {
  if (readFd_ >= 0)
    ::close(readFd_);
}

bool Process::Start(const ProcessOptions &o, std::string &error) {
  if (pid_ > 0) {
    error = "process already started";
    return false;
  }
  // Everything the child needs is built before fork(): after fork only
  // async-signal-safe calls are allowed in a multithreaded parent.
  std::vector<std::string> argStore;
  argStore.push_back(o.program);
  argStore.insert(argStore.end(), o.args.begin(), o.args.end());
  std::vector<char *> argv;
  for (auto &a : argStore)
    argv.push_back(a.data());
  argv.push_back(nullptr);

  std::vector<std::string> envStore;
  for (char **e = environ; e && *e; ++e) {
    const char *eq = std::strchr(*e, '=');
    const std::string key = eq ? std::string(*e, (size_t)(eq - *e)) : std::string(*e);
    bool overridden = false;
    for (const auto &kv : o.env)
      overridden |= kv.first == key;
    if (!overridden)
      envStore.emplace_back(*e);
  }
  for (const auto &[k, v] : o.env)
    envStore.push_back(k + "=" + v);
  std::vector<char *> envp;
  for (auto &e : envStore)
    envp.push_back(e.data());
  envp.push_back(nullptr);

  int fds[2] = {-1, -1};
  if (o.captureOutput && ::pipe(fds) != 0) {
    error = std::string("pipe() failed: ") + std::strerror(errno);
    return false;
  }
  if (fds[0] >= 0)
    ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);

  const pid_t pid = ::fork();
  if (pid < 0) {
    error = std::string("fork() failed: ") + std::strerror(errno);
    if (fds[0] >= 0) {
      ::close(fds[0]);
      ::close(fds[1]);
    }
    return false;
  }
  if (pid == 0) {
    if (fds[1] >= 0) {
      ::dup2(fds[1], STDOUT_FILENO);
      ::dup2(fds[1], STDERR_FILENO);
      ::close(fds[1]);
    }
    if (!o.workingDir.empty() && ::chdir(o.workingDir.c_str()) != 0)
      ::_exit(127);
    environ = envp.data();
    ::execvp(argv[0], argv.data());
    const char msg[] = "exec failed: ";
    (void)!::write(STDERR_FILENO, msg, sizeof msg - 1);
    (void)!::write(STDERR_FILENO, argv[0], std::strlen(argv[0]));
    (void)!::write(STDERR_FILENO, "\n", 1);
    ::_exit(127);
  }
  if (fds[1] >= 0)
    ::close(fds[1]);
  readFd_ = fds[0];
  pid_ = (long)pid;
  return true;
}

long Process::Read(char *buffer, unsigned long size) {
  if (readFd_ < 0)
    return 0;
  for (;;) {
    const ssize_t n = ::read(readFd_, buffer, size);
    if (n >= 0)
      return (long)n;
    if (errno != EINTR)
      return -1;
  }
}

int Process::Wait() {
  if (pid_ <= 0)
    return -1;
  if (!waited_) {
    int status = 0;
    while (::waitpid((pid_t)pid_, &status, 0) < 0 && errno == EINTR) {
    }
    if (WIFEXITED(status))
      exitCode_ = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
      exitCode_ = 128 + WTERMSIG(status);
    waited_ = true;
  }
  return exitCode_;
}

bool Process::TryWait(int &exitCode) {
  if (pid_ <= 0)
    return false;
  if (!waited_) {
    int status = 0;
    pid_t r;
    while ((r = ::waitpid((pid_t)pid_, &status, WNOHANG)) < 0 && errno == EINTR) {
    }
    if (r != (pid_t)pid_)
      return false;
    if (WIFEXITED(status))
      exitCode_ = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
      exitCode_ = 128 + WTERMSIG(status);
    waited_ = true;
  }
  exitCode = exitCode_;
  return true;
}

void Process::Terminate() {
  if (pid_ > 0 && !waited_) // after the reap the pid may belong to someone else
    ::kill((pid_t)pid_, SIGTERM);
}

std::string CurrentExecutablePath() {
  std::error_code ec;
  const auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
  return ec ? std::string() : p.string();
}

#endif

} // namespace ilmeee

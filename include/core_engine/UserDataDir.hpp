#pragma once
// Cross-platform user-data directory for the Ilmeee engine. Both the
// editor (GameEngineSDL) and the launcher (IlmeeeHub) use this to
// stash transient per-user / per-project data — caches, autosaves,
// imported asset thumbnails, the hub's project index, and so on.
//
// Resolution rules:
//   Linux/macOS: $HOME/.ilmeeeengine/
//   Windows:     %USERPROFILE%\.ilmeeeengine\
//
// Spelling is deliberate (engine name "Ilmeee" + suffix "engine"
// collapsed): keep it stable across platforms so projects authored on
// one OS open the same on the other.

#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif
#include <filesystem>
#include <string>
#include <system_error>

namespace ilmeee {

inline std::string HomeDirectory() {
#ifdef _WIN32
  if (const char *up = std::getenv("USERPROFILE"))
    return up;
  // Fallback: compose from HOMEDRIVE+HOMEPATH if USERPROFILE is unset.
  const char *hd = std::getenv("HOMEDRIVE");
  const char *hp = std::getenv("HOMEPATH");
  if (hd && hp)
    return std::string(hd) + hp;
  return "";
#else
  if (const char *h = std::getenv("HOME"))
    return h;
  return "";
#endif
}

// Directory holding the running executable.
//
// Needed whenever one of our binaries has to spawn a sibling: the working
// directory is wherever the user happened to launch us from, so a relative
// "./Sibling" only resolves by luck. Asking the OS where we actually live
// works the same in the build tree and in an installed bundle.
inline std::filesystem::path ExecutableDir() {
  std::error_code ec;
#ifdef _WIN32
  wchar_t buf[32768];
  DWORD n = GetModuleFileNameW(nullptr, buf, sizeof(buf) / sizeof(buf[0]));
  if (n > 0)
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
#else
  std::filesystem::path self =
      std::filesystem::read_symlink("/proc/self/exe", ec);
  if (!ec)
    return self.parent_path();
#endif
  // Last resort: behave as before rather than fail outright.
  return std::filesystem::current_path(ec);
}

// Resolve a sibling executable next to this one, falling back to the bare
// name so PATH still gets a chance if the layout is unexpected.
inline std::string SiblingExecutable(const std::string &name) {
  std::error_code ec;
  std::filesystem::path p = ExecutableDir() / name;
  if (std::filesystem::exists(p, ec))
    return p.string();
  return name;
}

// User-specified .ilmeee directory for layout settings and legacy
// configurations.
inline std::filesystem::path IlmeeeDir() {
  std::string home = HomeDirectory();
  std::filesystem::path root =
      home.empty() ? std::filesystem::temp_directory_path() / "ilmeee"
                   : std::filesystem::path(home) / ".ilmeee";
  std::error_code ec;
  std::filesystem::create_directories(root, ec);
  return root;
}

// Root of all Ilmeee user data. Created on first call.
inline std::filesystem::path UserDataRoot() {
  std::string home = HomeDirectory();
  std::filesystem::path root =
      home.empty() ? std::filesystem::temp_directory_path() / "ilmeeeengine"
                   : std::filesystem::path(home) / ".ilmeeeengine";
  std::error_code ec;
  std::filesystem::create_directories(root, ec);
  return root;
}

// Subdirectory for a specific project's transient data. The project
// path is hashed into a stable slug so a moved project doesn't collide
// with the original on disk.
inline std::filesystem::path
ProjectScratchDir(const std::string &projectAbsPath) {
  // Lightweight FNV-1a hash so we don't pull in <openssl>/<crypto>.
  uint64_t h = 1469598103934665603ull;
  for (unsigned char c : projectAbsPath) {
    h ^= c;
    h *= 1099511628211ull;
  }
  char buf[24];
  std::snprintf(buf, sizeof(buf), "proj-%016lx", (unsigned long)h);
  std::filesystem::path d = UserDataRoot() / "projects" / buf;
  std::error_code ec;
  std::filesystem::create_directories(d, ec);
  return d;
}

// Hub's recent-projects index (replaces the older ~/.ilmeee/hub.json
// path so all engine state lives under one root).
inline std::filesystem::path HubIndexPath() {
  return UserDataRoot() / "hub.json";
}

} // namespace ilmeee

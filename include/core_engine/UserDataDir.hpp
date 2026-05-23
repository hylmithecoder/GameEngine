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

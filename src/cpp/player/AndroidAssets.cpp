#include "AndroidAssets.hpp"
#include <SDL3/SDL.h>
#include <android/log.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <streambuf>
#include <vector>

namespace ilmeee::android {
namespace fs = std::filesystem;

namespace {
class LogcatBuffer : public std::streambuf {
public:
  explicit LogcatBuffer(int priority) : priority_(priority) {}

protected:
  int overflow(int c) override {
    if (c == traits_type::eof()) return 0;
    if (c == '\n') Flush();
    else line_.push_back((char)c);
    return c;
  }
  int sync() override {
    Flush();
    return 0;
  }

private:
  void Flush() {
    if (line_.empty()) return;
    __android_log_write(priority_, "Ilmeee", line_.c_str());
    line_.clear();
  }
  int priority_;
  std::string line_;
};

// Reads a whole APK asset (or any SDL path) as text.
bool ReadAsset(const std::string &path, std::string &out) {
  size_t size = 0;
  void *data = SDL_LoadFile(path.c_str(), &size);
  if (!data) return false;
  out.assign(static_cast<const char *>(data), size);
  SDL_free(data);
  return true;
}

// Streams one asset to disk in chunks (models can be tens of MB).
bool CopyAsset(const std::string &asset, const fs::path &target) {
  SDL_IOStream *in = SDL_IOFromFile(asset.c_str(), "rb");
  if (!in) return false;
  std::error_code ec;
  fs::create_directories(target.parent_path(), ec);
  std::ofstream out(target, std::ios::binary | std::ios::trunc);
  std::vector<char> buffer(1 << 20);
  bool ok = (bool)out;
  while (ok) {
    const size_t n = SDL_ReadIO(in, buffer.data(), buffer.size());
    if (n == 0) break;
    ok = (bool)out.write(buffer.data(), (std::streamsize)n);
  }
  ok = ok && SDL_GetIOStatus(in) == SDL_IO_STATUS_EOF;
  SDL_CloseIO(in);
  return ok && (bool)out.flush();
}

// Paths from the index must stay inside the game folder.
bool SafeRelative(const std::string &rel) {
  if (rel.empty() || rel[0] == '/') return false;
  for (const auto &part : fs::path(rel))
    if (part == "..") return false;
  return true;
}
} // namespace

void RedirectStdioToLogcat() {
  static LogcatBuffer out(ANDROID_LOG_INFO), err(ANDROID_LOG_ERROR);
  std::cout.rdbuf(&out);
  std::cerr.rdbuf(&err);
}

fs::path PrepareGameFiles(std::string &error) {
  const char *internal = SDL_GetAndroidInternalStoragePath();
  if (!internal) {
    error = std::string("No internal storage: ") + SDL_GetError();
    return {};
  }
  setenv("HOME", internal, 1);
  const fs::path root = fs::path(internal) / "game";

  std::string index;
  if (!ReadAsset(kFileIndex, index)) {
    error = std::string("APK has no ") + kFileIndex + " (not built by Ilmeee?)";
    return {};
  }
  std::istringstream lines(index);
  std::string header;
  std::getline(lines, header);
  if (header.rfind("ilmeee-files 1 ", 0) != 0) {
    error = std::string("Unsupported ") + kFileIndex;
    return {};
  }
  const std::string buildId = header.substr(15);

  // Same build already extracted: start immediately.
  const fs::path stamp = root / ".ilmeee_build_id";
  std::string extracted;
  if (std::ifstream(stamp) >> extracted && extracted == buildId) return root;

  std::cout << "Extracting game files (build " << buildId << ")" << std::endl;
  std::error_code ec;
  fs::remove_all(root, ec);
  size_t count = 0;
  for (std::string rel; std::getline(lines, rel);) {
    if (!rel.empty() && rel.back() == '\r') rel.pop_back();
    if (rel.empty()) continue;
    if (!SafeRelative(rel) || !CopyAsset("game/" + rel, root / rel)) {
      error = "Cannot extract " + rel + ": " + SDL_GetError();
      return {};
    }
    ++count;
  }
  // Written last so an interrupted copy is redone on the next start.
  if (!(std::ofstream(stamp) << buildId)) {
    error = "Cannot write " + stamp.string();
    return {};
  }
  std::cout << "Extracted " << count << " file(s) to " << root << std::endl;
  return root;
}

} // namespace ilmeee::android

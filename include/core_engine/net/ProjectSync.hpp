// Project files for online workspaces: an editor on a fresh device can join a
// workspace without having the project folder.
//
// The publisher uploads its project to the realtime server (content-addressed:
// each file is stored once under its SHA-256, plus one manifest per room). A
// joiner downloads the manifest and only the files it does not have yet into
// a cache folder (WorkspaceCacheDir), then opens the editor there. The live
// scene still comes from the room itself; these are the assets around it.
//
// Server side: server/realtime/src/files.rs. Calls block (network + disk);
// run them on a worker thread and read progress through the callback.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ilmeee::net {

// ---- SHA-256 -----------------------------------------------------------------
class Sha256 {
public:
  Sha256();
  void Update(const void *data, std::size_t len);
  std::string FinishHex(); // lowercase, 64 chars; the object is spent after

private:
  void Block(const unsigned char *p);
  std::uint32_t h_[8];
  unsigned char buf_[64];
  std::size_t bufLen_ = 0;
  std::uint64_t total_ = 0;
};

std::string Sha256Hex(const void *data, std::size_t len);
// Empty string when the file cannot be read.
std::string Sha256File(const std::filesystem::path &file);

// ---- paths -------------------------------------------------------------------
// UTF-8 <-> filesystem path (Windows paths are UTF-16 underneath).
std::string PathToUtf8(const std::filesystem::path &p);
std::filesystem::path PathFromUtf8(const std::string &s);

// Same rule as the server: relative, '/' separators, cannot leave the folder.
bool ValidSyncPath(const std::string &rel);

// Build output, editor caches and per-machine files never leave the device.
// `userRules` are the lines of the project's .ilmeeeignore:
//   "dir/"  a folder (from the project root)   "*.ext"  a file suffix
//   "path"  that file or folder
bool IsSyncIgnored(const std::string &rel, const std::vector<std::string> &userRules = {});
std::vector<std::string> ReadSyncIgnore(const std::filesystem::path &projectDir);

// ---- scanning ----------------------------------------------------------------
struct SyncFile {
  std::string path; // project-relative, '/' separators, UTF-8
  std::string hash; // SHA-256 hex
  std::uint64_t size = 0;
};

struct SyncStatus {
  std::string phase; // "Scanning", "Uploading", "Downloading", ...
  std::string file;  // current file, if any
  std::size_t doneFiles = 0, totalFiles = 0;
  std::uint64_t doneBytes = 0, totalBytes = 0;
};
using SyncProgressFn = std::function<void(const SyncStatus &)>;

// All files of the project that would be published, hashed. Hashes are cached
// in <dir>/.ilmeee/sync-state.json by size + modification time, so a rescan
// only reads files that changed.
std::vector<SyncFile> ScanProject(const std::filesystem::path &dir, std::string &error,
                                  const SyncProgressFn &progress = {},
                                  const std::atomic<bool> *cancel = nullptr);

// ---- transfer ----------------------------------------------------------------
struct SyncResult {
  bool ok = false;
  std::string error;
  std::size_t files = 0;          // files in the project
  std::uint64_t bytes = 0;        // their total size
  std::size_t transferred = 0;    // files actually sent / received
  std::uint64_t transferredBytes = 0;
  std::size_t removed = 0;        // DownloadProject: stale files deleted
};

// Uploads `dir` as the project of `room` (only files the server lacks).
SyncResult PublishProject(const std::string &wsUrl, const std::string &token,
                          const std::string &room, const std::filesystem::path &dir,
                          const std::string &by, const SyncProgressFn &progress = {},
                          const std::atomic<bool> *cancel = nullptr);

// Brings `dir` up to date with the room's published project. Files the
// previous download created but the project no longer has are removed;
// anything else already in `dir` is left alone unless the project has a file
// with the same path (that one is replaced by the published version).
SyncResult DownloadProject(const std::string &wsUrl, const std::string &token,
                           const std::string &room, const std::filesystem::path &dir,
                           const SyncProgressFn &progress = {},
                           const std::atomic<bool> *cancel = nullptr);

// Where a downloaded workspace lives on this device:
// <UserDataRoot>/workspaces/<server host>/<room>.
std::filesystem::path WorkspaceCacheDir(const std::string &wsUrl, const std::string &room);

} // namespace ilmeee::net

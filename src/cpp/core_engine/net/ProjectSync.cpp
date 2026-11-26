// See include/core_engine/net/ProjectSync.hpp.
#include "../../../../include/core_engine/net/ProjectSync.hpp"
#include "../../../../include/core_engine/UserDataDir.hpp"
#include "../../../../include/core_engine/net/CollabClient.hpp"
#include "../../../../include/core_engine/net/WsTransport.hpp"

#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXNetSystem.h>
#include "../../../../vendor/nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <system_error>
#include <unordered_map>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace ilmeee::net {

// ============================================================================
// SHA-256 (FIPS 180-4)
// ============================================================================
namespace {
constexpr std::uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};

inline std::uint32_t Rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
} // namespace

Sha256::Sha256()
    : h_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab,
         0x5be0cd19} {}

void Sha256::Block(const unsigned char *p) {
  std::uint32_t w[64];
  for (int i = 0; i < 16; ++i)
    w[i] = (std::uint32_t(p[i * 4]) << 24) | (std::uint32_t(p[i * 4 + 1]) << 16) |
           (std::uint32_t(p[i * 4 + 2]) << 8) | std::uint32_t(p[i * 4 + 3]);
  for (int i = 16; i < 64; ++i) {
    const std::uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const std::uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6],
                h = h_[7];
  for (int i = 0; i < 64; ++i) {
    const std::uint32_t S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
    const std::uint32_t ch = (e & f) ^ (~e & g);
    const std::uint32_t t1 = h + S1 + ch + kK[i] + w[i];
    const std::uint32_t S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
    const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t t2 = S0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  h_[0] += a;
  h_[1] += b;
  h_[2] += c;
  h_[3] += d;
  h_[4] += e;
  h_[5] += f;
  h_[6] += g;
  h_[7] += h;
}

void Sha256::Update(const void *data, std::size_t len) {
  auto *p = static_cast<const unsigned char *>(data);
  total_ += len;
  if (bufLen_) {
    const std::size_t take = std::min(len, sizeof buf_ - bufLen_);
    std::memcpy(buf_ + bufLen_, p, take);
    bufLen_ += take;
    p += take;
    len -= take;
    if (bufLen_ < sizeof buf_)
      return;
    Block(buf_);
    bufLen_ = 0;
  }
  for (; len >= 64; p += 64, len -= 64)
    Block(p);
  std::memcpy(buf_, p, len);
  bufLen_ = len;
}

std::string Sha256::FinishHex() {
  const std::uint64_t bits = total_ * 8;
  const unsigned char pad = 0x80;
  Update(&pad, 1);
  const unsigned char zero = 0;
  while (bufLen_ != 56)
    Update(&zero, 1);
  unsigned char lenBytes[8];
  for (int i = 0; i < 8; ++i)
    lenBytes[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
  Update(lenBytes, 8);
  static const char *hex = "0123456789abcdef";
  std::string out(64, '0');
  for (int i = 0; i < 8; ++i)
    for (int j = 0; j < 4; ++j) {
      const unsigned char byte = static_cast<unsigned char>(h_[i] >> (24 - 8 * j));
      out[i * 8 + j * 2] = hex[byte >> 4];
      out[i * 8 + j * 2 + 1] = hex[byte & 15];
    }
  return out;
}

std::string Sha256Hex(const void *data, std::size_t len) {
  Sha256 h;
  h.Update(data, len);
  return h.FinishHex();
}

std::string Sha256File(const fs::path &file) {
  std::ifstream in(file, std::ios::binary);
  if (!in)
    return {};
  Sha256 h;
  std::vector<char> buf(1 << 16);
  while (in) {
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    const std::streamsize n = in.gcount();
    if (n > 0)
      h.Update(buf.data(), static_cast<std::size_t>(n));
  }
  if (in.bad())
    return {};
  return h.FinishHex();
}

// ============================================================================
// Paths and ignore rules
// ============================================================================
std::string PathToUtf8(const fs::path &p) {
  const std::u8string s = p.generic_u8string();
  return std::string(s.begin(), s.end());
}

fs::path PathFromUtf8(const std::string &s) {
  return fs::path(std::u8string(s.begin(), s.end()));
}

bool ValidSyncPath(const std::string &rel) {
  if (rel.empty() || rel.size() > 512 || rel.front() == '/')
    return false;
  for (unsigned char c : rel)
    if (c < 0x20 || c == '\\' || c == ':')
      return false;
  std::size_t start = 0;
  while (start <= rel.size()) {
    std::size_t end = rel.find('/', start);
    if (end == std::string::npos)
      end = rel.size();
    const std::string part = rel.substr(start, end - start);
    if (part.empty() || part == "." || part == "..")
      return false;
    start = end + 1;
  }
  return true;
}

std::string WindowsNameProblem(const std::string &rel) {
  for (unsigned char c : rel)
    if (c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*')
      return std::string("contains '") + (char)c + "', which Windows does not allow";
  std::size_t start = 0;
  while (start < rel.size()) {
    std::size_t end = rel.find('/', start);
    if (end == std::string::npos)
      end = rel.size();
    const std::string part = rel.substr(start, end - start);
    start = end + 1;
    if (part.empty())
      continue;
    if (part.back() == '.' || part.back() == ' ')
      return "'" + part + "' ends with a dot or space (Windows drops it)";
    // Device names are reserved with any extension: "aux.png", "nul.txt".
    std::string stem = part.substr(0, part.find('.'));
    while (!stem.empty() && stem.back() == ' ')
      stem.pop_back();
    for (auto &ch : stem)
      ch = (char)std::toupper((unsigned char)ch);
    static const char *const kReserved[] = {"CON", "PRN", "AUX", "NUL"};
    bool reserved = false;
    for (const char *r : kReserved)
      reserved |= stem == r;
    if (stem.size() == 4 && (stem.rfind("COM", 0) == 0 || stem.rfind("LPT", 0) == 0) &&
        stem[3] >= '1' && stem[3] <= '9')
      reserved = true;
    if (reserved)
      return "'" + part + "' is a reserved device name on Windows";
  }
  return {};
}

namespace {
const char *const kSyncStateRel = ".ilmeee/sync-state.json";
const char *const kPartSuffix = ".ilmeee-part";

bool MatchesRule(const std::string &rel, std::string rule) {
  if (rule.empty())
    return false;
  if (rule.rfind("*.", 0) == 0) {
    const std::string suffix = rule.substr(1);
    return rel.size() >= suffix.size() &&
           rel.compare(rel.size() - suffix.size(), suffix.size(), suffix) == 0;
  }
  if (rule.back() == '/')
    rule.pop_back();
  while (!rule.empty() && rule.front() == '/')
    rule.erase(rule.begin());
  if (rule.empty())
    return false;
  return rel == rule || rel.rfind(rule + "/", 0) == 0;
}

std::string LastComponent(const std::string &rel) {
  const auto slash = rel.rfind('/');
  return slash == std::string::npos ? rel : rel.substr(slash + 1);
}
} // namespace

bool IsSyncIgnored(const std::string &rel, const std::vector<std::string> &userRules) {
  // Build output, IDE/tool caches, compiled scripts: rebuilt on each device.
  static const char *const kDirs[] = {"build/",  ".git/",  ".cache/",
                                      ".vscode/", ".idea/", ".ilmeee/native-scripts/"};
  static const char *const kFiles[] = {"compile_commands.json", kSyncStateRel};
  static const char *const kSuffixes[] = {"*.blend1", "*.ilmeee-part", "*.swp"};
  static const char *const kNames[] = {".DS_Store", "Thumbs.db", "desktop.ini"};
  for (const char *d : kDirs)
    if (MatchesRule(rel, d))
      return true;
  for (const char *f : kFiles)
    if (rel == f)
      return true;
  for (const char *s : kSuffixes)
    if (MatchesRule(rel, s))
      return true;
  const std::string name = LastComponent(rel);
  for (const char *n : kNames)
    if (name == n)
      return true;
  if (!name.empty() && name.back() == '~')
    return true;
  // The editor's scratch scenes for Play mode (TempScene_*.json at the root).
  if (rel.find('/') == std::string::npos && rel.rfind("TempScene_", 0) == 0)
    return true;
  for (const auto &r : userRules)
    if (MatchesRule(rel, r))
      return true;
  return false;
}

std::vector<std::string> ReadSyncIgnore(const fs::path &projectDir) {
  std::vector<std::string> rules;
  std::ifstream in(projectDir / ".ilmeeeignore");
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
      line.pop_back();
    std::size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
      ++i;
    line = line.substr(i);
    if (!line.empty() && line[0] != '#')
      rules.push_back(line);
  }
  return rules;
}

// ============================================================================
// Sync state (hash cache + what the last download created)
// ============================================================================
namespace {
struct CachedHash {
  std::uint64_t size = 0;
  std::int64_t mtime = 0;
  std::string hash;
};

struct SyncState {
  std::unordered_map<std::string, CachedHash> hashes;
  std::set<std::string> downloaded;
};

std::int64_t MTimeOf(const fs::path &p) {
  std::error_code ec;
  const auto t = fs::last_write_time(p, ec);
  return ec ? 0 : static_cast<std::int64_t>(t.time_since_epoch().count());
}

SyncState LoadState(const fs::path &dir) {
  SyncState st;
  std::ifstream in(dir / PathFromUtf8(kSyncStateRel), std::ios::binary);
  if (!in)
    return st;
  const json j = json::parse(in, nullptr, false);
  if (!j.is_object())
    return st;
  if (auto h = j.find("hashes"); h != j.end() && h->is_object())
    for (auto it = h->begin(); it != h->end(); ++it)
      if (it->is_object())
        st.hashes[it.key()] = CachedHash{it->value("size", std::uint64_t{0}),
                                         it->value("mtime", std::int64_t{0}),
                                         it->value("hash", std::string())};
  if (auto d = j.find("downloaded"); d != j.end() && d->is_array())
    for (const auto &p : *d)
      if (p.is_string())
        st.downloaded.insert(p.get<std::string>());
  return st;
}

void SaveState(const fs::path &dir, const SyncState &st) {
  json h = json::object();
  for (const auto &[path, c] : st.hashes)
    h[path] = {{"size", c.size}, {"mtime", c.mtime}, {"hash", c.hash}};
  json j = {{"v", 1}, {"hashes", h}, {"downloaded", st.downloaded}};
  const fs::path file = dir / PathFromUtf8(kSyncStateRel);
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  fs::path tmp = file;
  tmp += ".tmp"; // not string(): that loses non-ASCII names on Windows
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out << j.dump();
    if (!out)
      return;
  }
  fs::rename(tmp, file, ec);
}

// Hash of a local file, reusing the cache when size + mtime still match.
std::string HashCached(SyncState &st, const fs::path &file, const std::string &rel,
                       std::uint64_t size) {
  const std::int64_t mtime = MTimeOf(file);
  auto it = st.hashes.find(rel);
  if (it != st.hashes.end() && it->second.size == size && it->second.mtime == mtime &&
      it->second.hash.size() == 64)
    return it->second.hash;
  std::string h = Sha256File(file);
  if (!h.empty())
    st.hashes[rel] = CachedHash{size, mtime, h};
  return h;
}

bool Cancelled(const std::atomic<bool> *cancel) { return cancel && cancel->load(); }
} // namespace

std::vector<SyncFile> ScanProject(const fs::path &dir, std::string &error,
                                  const SyncProgressFn &progress,
                                  const std::atomic<bool> *cancel) {
  std::vector<SyncFile> out;
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    error = "not a folder: " + PathToUtf8(dir);
    return out;
  }
  const auto rules = ReadSyncIgnore(dir);

  // Pass 1: list (cheap), so progress can show a total.
  std::vector<std::pair<std::string, std::uint64_t>> found;
  std::uint64_t totalBytes = 0;
  fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
  if (ec) {
    error = "cannot read " + PathToUtf8(dir) + ": " + ec.message();
    return out;
  }
  for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) {
      error = "cannot read " + PathToUtf8(dir) + ": " + ec.message();
      return out;
    }
    if (Cancelled(cancel)) {
      error = "cancelled";
      return {};
    }
    const fs::path rel = fs::relative(it->path(), dir, ec);
    if (ec)
      continue;
    const std::string r = PathToUtf8(rel);
    std::error_code sec;
    const auto st = it->symlink_status(sec);
    if (fs::is_directory(st)) {
      if (IsSyncIgnored(r, rules))
        it.disable_recursion_pending();
      continue;
    }
    if (!fs::is_regular_file(st) || IsSyncIgnored(r, rules))
      continue; // symlinks, sockets, ... stay local
    if (!ValidSyncPath(r))
      continue;
    const std::uint64_t size = it->file_size(sec);
    if (sec)
      continue;
    found.emplace_back(r, size);
    totalBytes += size;
  }
  std::sort(found.begin(), found.end());

  // Pass 2: hash (cached by size + mtime).
  SyncState state = LoadState(dir);
  SyncStatus s;
  s.phase = "Scanning";
  s.totalFiles = found.size();
  s.totalBytes = totalBytes;
  for (const auto &[rel, size] : found) {
    if (Cancelled(cancel)) {
      error = "cancelled";
      return {};
    }
    if (progress) {
      s.file = rel;
      progress(s);
    }
    const std::string h = HashCached(state, dir / PathFromUtf8(rel), rel, size);
    if (h.empty()) {
      error = "cannot read " + rel;
      return {};
    }
    out.push_back(SyncFile{rel, h, size});
    ++s.doneFiles;
    s.doneBytes += size;
  }
  // Forget cache entries of files that are gone.
  std::set<std::string> live;
  for (const auto &f : out)
    live.insert(f.path);
  for (auto i = state.hashes.begin(); i != state.hashes.end();)
    i = live.count(i->first) ? std::next(i) : state.hashes.erase(i);
  SaveState(dir, state);
  return out;
}

// ============================================================================
// HTTP
// ============================================================================
namespace {
struct Http {
  std::string base;
  std::string token;
  ix::HttpClient client;
  std::string error; // set by Init

  bool Init(const std::string &wsUrl) {
    base = CollabHttpBase(wsUrl);
    if (base.empty()) {
      error = "server URL must start with ws:// or wss://";
      return false;
    }
    if (base.rfind("https://", 0) == 0) {
      if (!WsTlsAvailable()) {
        error = "https needs TLS, but this build has no OpenSSL";
        return false;
      }
      ix::SocketTLSOptions tls;
      tls.caFile = ResolveCaFile("");
      client.setTLSOptions(tls);
    }
    ix::initNetSystem();
    return true;
  }

  ix::HttpRequestArgsPtr Args(const std::string &url, int transferTimeout) {
    auto a = client.createRequest(url);
    a->connectTimeout = 15;
    a->transferTimeout = transferTimeout;
    if (!token.empty())
      a->extraHeaders["Authorization"] = "Bearer " + token;
    return a;
  }

  ix::HttpResponsePtr Json(const std::string &verb, const std::string &path,
                           const std::string &body = {}) {
    const std::string url = base + path;
    auto a = Args(url, 300);
    a->extraHeaders["Accept"] = "application/json";
    if (verb != ix::HttpClient::kGet)
      a->extraHeaders["Content-Type"] = "application/json";
    return client.request(url, verb, body, a);
  }
};

// "HTTP 413: file too large (max ...)" from a response.
std::string Describe(const ix::HttpResponsePtr &r) {
  if (!r)
    return "no response";
  if (r->statusCode == 0)
    return r->errorMsg.empty() ? "cannot reach the server" : r->errorMsg;
  std::string msg = "HTTP " + std::to_string(r->statusCode);
  const json j = json::parse(r->body, nullptr, false);
  if (j.is_object() && j.contains("error") && j["error"].is_string())
    msg += ": " + j["error"].get<std::string>();
  else if (r->statusCode == 404 || r->statusCode == 405)
    msg += " (server too old for project files?)";
  if (r->statusCode == 401)
    msg = "invalid token";
  return msg;
}

bool ReadFileBytes(const fs::path &p, std::string &out) {
  std::ifstream in(p, std::ios::binary);
  if (!in)
    return false;
  in.seekg(0, std::ios::end);
  const auto len = in.tellg();
  if (len < 0)
    return false;
  out.resize(static_cast<std::size_t>(len));
  in.seekg(0);
  in.read(out.data(), len);
  return static_cast<bool>(in) || out.empty();
}
// Files a peer on another OS will fail to write (see SyncResult::warnings).
std::vector<std::string> PortabilityWarnings(const std::vector<SyncFile> &files) {
  std::vector<std::string> out;
  std::map<std::string, std::string> folded; // lower-case path -> first path
  for (const auto &f : files) {
    if (std::string why = WindowsNameProblem(f.path); !why.empty())
      out.push_back(f.path + ": " + why);
    std::string lower = f.path;
    for (auto &c : lower)
      c = (char)std::tolower((unsigned char)c);
    auto [it, inserted] = folded.emplace(lower, f.path);
    if (!inserted)
      out.push_back(f.path + ": differs from '" + it->second +
                    "' only in upper/lower case (one overwrites the other on Windows)");
    // The workspace folder on Windows (C:\Users\<name>\.ilmeeeengine\workspaces
    // \<host>\<room>\) already takes ~100 of the 260 characters Windows allows.
    if (f.path.size() > 150)
      out.push_back(f.path + ": path is very long; Windows may not be able to open it");
  }
  return out;
}
} // namespace

SyncResult PublishProject(const std::string &wsUrl, const std::string &token,
                          const std::string &room, const fs::path &dir, const std::string &by,
                          const SyncProgressFn &progress, const std::atomic<bool> *cancel) {
  SyncResult res;
  Http http;
  http.token = token;
  if (!http.Init(wsUrl)) {
    res.error = http.error;
    return res;
  }
  const auto files = ScanProject(dir, res.error, progress, cancel);
  if (!res.error.empty())
    return res;
  res.files = files.size();
  for (const auto &f : files)
    res.bytes += f.size;
  res.warnings = PortabilityWarnings(files);

  std::map<std::string, const SyncFile *> byHash;
  for (const auto &f : files)
    byHash.emplace(f.hash, &f);

  json manifest = {{"by", by}, {"files", json::array()}};
  for (const auto &f : files)
    manifest["files"].push_back({{"path", f.path}, {"hash", f.hash}, {"size", f.size}});
  const std::string manifestBody = manifest.dump();

  // Up to two rounds: a blob may vanish between upload and manifest (server
  // clean-up), in which case the 409 lists it again.
  std::vector<std::string> missing;
  {
    json q = {{"hashes", json::array()}};
    for (const auto &[h, f] : byHash)
      q["hashes"].push_back(h);
    const auto r = http.Json(ix::HttpClient::kPost, "/files/missing", q.dump());
    if (!r || r->statusCode != 200) {
      res.error = Describe(r);
      return res;
    }
    const json j = json::parse(r->body, nullptr, false);
    if (!j.is_object() || !j.contains("missing") || !j["missing"].is_array()) {
      res.error = "unexpected answer from the server";
      return res;
    }
    for (const auto &h : j["missing"])
      if (h.is_string())
        missing.push_back(h.get<std::string>());
  }

  for (int round = 0; round < 2; ++round) {
    SyncStatus s;
    s.phase = "Uploading";
    s.totalFiles = missing.size();
    for (const auto &h : missing)
      if (auto it = byHash.find(h); it != byHash.end())
        s.totalBytes += it->second->size;
    for (const auto &h : missing) {
      if (Cancelled(cancel)) {
        res.error = "cancelled";
        return res;
      }
      auto it = byHash.find(h);
      if (it == byHash.end()) {
        res.error = "server asked for a file this project does not have";
        return res;
      }
      const SyncFile &f = *it->second;
      if (progress) {
        s.file = f.path;
        progress(s);
      }
      std::string bytes;
      if (!ReadFileBytes(dir / PathFromUtf8(f.path), bytes)) {
        res.error = "cannot read " + f.path;
        return res;
      }
      if (Sha256Hex(bytes.data(), bytes.size()) != f.hash) {
        res.error = f.path + " changed while uploading; try again";
        return res;
      }
      const std::string url = http.base + "/files/blob/" + f.hash;
      auto a = http.Args(url, 3600);
      a->extraHeaders["Content-Type"] = "application/octet-stream";
      const auto r = http.client.request(url, "PUT", bytes, a);
      if (!r || (r->statusCode != 200 && r->statusCode != 201)) {
        res.error = f.path + ": " + Describe(r);
        return res;
      }
      ++res.transferred;
      res.transferredBytes += f.size;
      ++s.doneFiles;
      s.doneBytes += f.size;
    }

    if (progress) {
      SyncStatus fin;
      fin.phase = "Publishing";
      progress(fin);
    }
    const auto r = http.Json("PUT", "/rooms/" + room + "/files", manifestBody);
    if (r && r->statusCode == 200) {
      res.ok = true;
      return res;
    }
    if (!r || r->statusCode != 409 || round == 1) {
      res.error = Describe(r);
      return res;
    }
    missing.clear();
    const json j = json::parse(r->body, nullptr, false);
    if (j.is_object() && j.contains("missing") && j["missing"].is_array())
      for (const auto &h : j["missing"])
        if (h.is_string())
          missing.push_back(h.get<std::string>());
  }
  res.error = "could not publish";
  return res;
}

SyncResult DownloadProject(const std::string &wsUrl, const std::string &token,
                           const std::string &room, const fs::path &dir,
                           const SyncProgressFn &progress, const std::atomic<bool> *cancel) {
  SyncResult res;
  Http http;
  http.token = token;
  if (!http.Init(wsUrl)) {
    res.error = http.error;
    return res;
  }
  if (progress) {
    SyncStatus s;
    s.phase = "Reading file list";
    progress(s);
  }
  const auto r = http.Json(ix::HttpClient::kGet, "/rooms/" + room + "/files");
  if (!r || r->statusCode != 200) {
    res.error = (r && r->statusCode == 404 && r->body.find("no project files") != std::string::npos)
                    ? "this workspace has no project files yet"
                    : Describe(r);
    return res;
  }
  const json m = json::parse(r->body, nullptr, false);
  if (!m.is_object() || !m.contains("files") || !m["files"].is_array()) {
    res.error = "unexpected answer from the server";
    return res;
  }
  std::vector<SyncFile> files;
  for (const auto &f : m["files"]) {
    if (!f.is_object())
      continue;
    SyncFile sf{f.value("path", std::string()), f.value("hash", std::string()),
                f.value("size", std::uint64_t{0})};
    if (!ValidSyncPath(sf.path) || sf.hash.size() != 64) {
      res.error = "the server sent an unsafe path: " + sf.path;
      return res; // never write outside `dir`
    }
    files.push_back(std::move(sf));
  }
  res.files = files.size();
  for (const auto &f : files)
    res.bytes += f.size;

  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    res.error = "cannot create " + PathToUtf8(dir) + ": " + ec.message();
    return res;
  }
  SyncState state = LoadState(dir);

  // What is already here with the right content?
  std::vector<const SyncFile *> need;
  SyncStatus s;
  s.phase = "Checking";
  s.totalFiles = files.size();
  for (const auto &f : files) {
    if (Cancelled(cancel)) {
      res.error = "cancelled";
      return res;
    }
    const fs::path local = dir / PathFromUtf8(f.path);
    std::error_code fec;
    const bool have = fs::is_regular_file(local, fec) && fs::file_size(local, fec) == f.size &&
                      !fec && HashCached(state, local, f.path, f.size) == f.hash;
    if (!have) {
      need.push_back(&f);
      s.totalBytes += f.size;
    }
    ++s.doneFiles;
    if (progress && (s.doneFiles % 64 == 0 || s.doneFiles == files.size())) {
      s.file = f.path;
      progress(s);
    }
  }

  s = SyncStatus{};
  s.phase = "Downloading";
  s.totalFiles = need.size();
  for (const auto *f : need)
    s.totalBytes += f->size;

  // One file. Returns "" on success, else why it failed; `connection` tells a
  // server that cannot be reached (worth stopping for) from a bad file.
  auto fetchOne = [&](const SyncFile &f, bool &connection) -> std::string {
    connection = false;
#ifdef _WIN32
    if (std::string why = WindowsNameProblem(f.path); !why.empty())
      return why + "; rename it in the original project";
#endif
    const fs::path local = dir / PathFromUtf8(f.path);
    std::error_code fec;
    fs::create_directories(local.parent_path(), fec);
    if (fec)
      return "cannot create folder: " + fec.message();
    fs::path part = local;
    part += kPartSuffix;
    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    if (!out)
      return "cannot write " + PathToUtf8(part) + " (path too long or not allowed?)";
    Sha256 hasher;
    std::uint64_t got = 0;
    const std::string url = http.base + "/files/blob/" + f.hash;
    auto a = http.Args(url, 3600);
    a->compress = false;
    // Raw bytes only: a proxy that gzips the body would break the hash check
    // (this IXWebSocket build has no zlib to undo it).
    a->extraHeaders["Accept-Encoding"] = "identity";
    const std::uint64_t before = s.doneBytes;
    a->onChunkCallback = [&](const std::string &chunk) {
      out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
      hasher.Update(chunk.data(), chunk.size());
      got += chunk.size();
    };
    a->onProgressCallback = [&, a = a.get()](int, int) {
      if (Cancelled(cancel))
        a->cancel = true;
      if (progress) {
        s.doneBytes = before + got;
        progress(s);
      }
      return true;
    };
    const auto br = http.client.get(url, a);
    out.close();
    const bool wrote = static_cast<bool>(out);
    const bool okStatus = br && br->statusCode == 200;
    const std::string digest = hasher.FinishHex();
    std::string why;
    if (!okStatus) {
      connection = !br || br->statusCode == 0;
      why = Describe(br);
    } else if (auto enc = br->headers.find("Content-Encoding");
               enc != br->headers.end() && !enc->second.empty() && enc->second != "identity") {
      why = "the server or a proxy compressed the file (" + enc->second + ")";
    } else if (!wrote) {
      why = "cannot write " + PathToUtf8(part) + " (disk full?)";
    } else if (got != f.size) {
      why = "incomplete download (" + std::to_string(got) + " of " + std::to_string(f.size) +
            " bytes)";
    } else if (digest != f.hash) {
      why = "download was damaged (hash mismatch)";
    }
    if (!why.empty()) {
      fs::remove(part, fec);
      s.doneBytes = before;
      return why;
    }
    fs::rename(part, local, fec);
    if (fec) {
      // Windows refuses to replace a file another program has open.
      fs::remove(part, fec);
      s.doneBytes = before;
      return "cannot replace the old copy (is it open in another program?)";
    }
    state.hashes[f.path] = CachedHash{f.size, MTimeOf(local), f.hash};
    s.doneBytes = before + f.size;
    return {};
  };

  int unreachableInARow = 0;
  for (const SyncFile *f : need) {
    if (Cancelled(cancel)) {
      res.error = "cancelled";
      SaveState(dir, state);
      return res;
    }
    if (progress) {
      s.file = f->path;
      progress(s);
    }
    std::string why;
    bool connection = false;
    // A dropped connection or a damaged transfer usually works the second
    // time; a name Windows rejects never does.
    for (int attempt = 0; attempt < 3; ++attempt) {
      why = fetchOne(*f, connection);
      if (why.empty() || Cancelled(cancel))
        break;
      if (why.find("Windows") != std::string::npos || why.find("cannot create") == 0 ||
          why.find("cannot write") == 0)
        break;
    }
    if (Cancelled(cancel)) {
      res.error = "cancelled";
      SaveState(dir, state);
      return res;
    }
    ++s.doneFiles;
    if (why.empty()) {
      unreachableInARow = 0;
      ++res.transferred;
      res.transferredBytes += f->size;
      continue;
    }
    res.failed.push_back(f->path + ": " + why);
    unreachableInARow = connection ? unreachableInARow + 1 : 0;
    if (unreachableInARow >= 3) {
      res.error = "lost the connection to the server (" + why + ")";
      SaveState(dir, state);
      return res;
    }
  }

  // Files our previous download created that the project dropped.
  std::set<std::string> now;
  for (const auto &f : files)
    now.insert(f.path);
  for (const auto &old : state.downloaded) {
    if (now.count(old) || !ValidSyncPath(old))
      continue;
    if (fs::remove(dir / PathFromUtf8(old), ec))
      ++res.removed;
    state.hashes.erase(old);
  }
  state.downloaded = std::move(now);
  SaveState(dir, state);
  if (!res.failed.empty()) {
    res.error = std::to_string(res.failed.size()) + " of " + std::to_string(files.size()) +
                " files could not be downloaded (first: " + res.failed.front() + ")";
    return res;
  }
  res.ok = true;
  return res;
}

fs::path WorkspaceCacheDir(const std::string &wsUrl, const std::string &room) {
  std::string host = CollabHttpBase(wsUrl);
  if (auto p = host.find("://"); p != std::string::npos)
    host = host.substr(p + 3);
  return WorkspaceCacheDirForKey(host, room);
}

fs::path WorkspaceCacheDirForKey(const std::string &host, const std::string &room) {
  std::string safeHost;
  for (char c : host)
    safeHost += (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-') ? c : '_';
  if (safeHost.empty())
    safeHost = "server";
  std::string safeRoom;
  for (char c : room)
    safeRoom += (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_')
                    ? c
                    : '_';
  if (safeRoom.empty() || safeRoom[0] == '.')
    safeRoom = "_" + safeRoom;
  return ilmeee::UserDataRoot() / "workspaces" / safeHost / safeRoom;
}

} // namespace ilmeee::net

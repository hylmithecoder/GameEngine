// ProjectSync: hashing, path rules, ignore rules, and (when ILMEEE_RT_URL
// points at a realtime server) a full publish -> fresh download round trip.
#include "../../include/core_engine/net/ProjectSync.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace ilmeee::net;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                            \
  do {                                                                         \
    ++g_checks;                                                                \
    if (!(cond)) {                                                             \
      ++g_failed;                                                              \
      std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; \
    }                                                                          \
  } while (0)

static void WriteFile(const fs::path &p, const std::string &data) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary | std::ios::trunc) << data;
}

static std::string ReadFile(const fs::path &p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int main() {
  // ---- SHA-256 test vectors ----
  CHECK(Sha256Hex("", 0) ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(Sha256Hex("abc", 3) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  CHECK(Sha256Hex(two.data(), two.size()) ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  {
    const std::string million(1000000, 'a');
    Sha256 h; // fed in odd-sized pieces to exercise the buffering
    for (std::size_t i = 0; i < million.size(); i += 777)
      h.Update(million.data() + i, std::min<std::size_t>(777, million.size() - i));
    CHECK(h.FinishHex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  }

  // ---- paths ----
  CHECK(ValidSyncPath("assets/textures/a.png"));
  CHECK(ValidSyncPath(".ilmeee/project.json"));
  CHECK(!ValidSyncPath("../x"));
  CHECK(!ValidSyncPath("a/../../x"));
  CHECK(!ValidSyncPath("/etc/passwd"));
  CHECK(!ValidSyncPath("C:/Windows"));
  CHECK(!ValidSyncPath("a\\b"));
  CHECK(!ValidSyncPath("a//b"));
  CHECK(!ValidSyncPath(""));
  const std::string utf8 = "assets/\xE6\xAD\xA6\xE5\x99\xA8.pmx"; // 武器.pmx
  CHECK(ValidSyncPath(utf8));

  // Names a Linux project can hold but Windows cannot write.
  CHECK(WindowsNameProblem("assets/textures/a.png").empty());
  CHECK(WindowsNameProblem("assets/console.png").empty());
  CHECK(WindowsNameProblem(utf8).empty());
  CHECK(!WindowsNameProblem("assets/what?.png").empty());
  CHECK(!WindowsNameProblem("a\"b.txt").empty());
  CHECK(!WindowsNameProblem("assets/aux.png").empty());
  CHECK(!WindowsNameProblem("NUL").empty());
  CHECK(!WindowsNameProblem("x/com1.txt").empty());
  CHECK(WindowsNameProblem("x/com0.txt").empty());
  CHECK(!WindowsNameProblem("folder./a.png").empty());
  CHECK(!WindowsNameProblem("a.png ").empty());
  CHECK(PathToUtf8(PathFromUtf8(utf8)) == utf8);

  // ---- ignore rules ----
  CHECK(IsSyncIgnored("build"));
  CHECK(IsSyncIgnored("build/linux/game"));
  CHECK(!IsSyncIgnored("assets/build")); // only the root build/ is output
  CHECK(IsSyncIgnored(".ilmeee/native-scripts/x/lib.so"));
  CHECK(!IsSyncIgnored(".ilmeee/project.json"));
  CHECK(IsSyncIgnored(".ilmeee/sync-state.json"));
  CHECK(IsSyncIgnored("TempScene_Game1.json"));
  CHECK(!IsSyncIgnored("scenes/TempScene_Game1.json"));
  CHECK(IsSyncIgnored("assets/models/a.blend1"));
  CHECK(!IsSyncIgnored("assets/models/a.blend"));
  CHECK(IsSyncIgnored("assets/.DS_Store"));
  CHECK(IsSyncIgnored("compile_commands.json"));
  CHECK(IsSyncIgnored("assets/videos/raw.mov", {"*.mov"}));
  CHECK(IsSyncIgnored("assets/videos/raw.mov", {"assets/videos/"}));
  CHECK(!IsSyncIgnored("assets/videos2/raw.mov", {"assets/videos/"}));

  // ---- scanning ----
  const fs::path root = fs::temp_directory_path() /
                        ("ilmeee-sync-test-" + std::to_string(std::chrono::steady_clock::now()
                                                                  .time_since_epoch()
                                                                  .count()));
  const fs::path src = root / "src";
  WriteFile(src / ".ilmeee/project.json", "{\"name\":\"Demo\"}");
  WriteFile(src / "scenes/main.ilmeeescene", std::string("ILMS\0\1binary", 12));
  WriteFile(src / "assets/textures/a.png", "png bytes");
  WriteFile(src / "assets/textures/copy.png", "png bytes"); // same content
  WriteFile(src / PathFromUtf8(utf8), "pmx");
  WriteFile(src / "build/linux/game", "output");
  WriteFile(src / "assets/raw/huge.wav", "skip me");
  WriteFile(src / ".ilmeeeignore", "# comment\nassets/raw/\n");
  std::string err;
  auto files = ScanProject(src, err);
  CHECK(err.empty());
  CHECK(files.size() == 6); // project.json, scene, 2 png, pmx, .ilmeeeignore
  bool sawUtf8 = false, sawBuild = false, sawRaw = false;
  for (const auto &f : files) {
    sawUtf8 |= f.path == utf8;
    sawBuild |= f.path.rfind("build/", 0) == 0;
    sawRaw |= f.path.rfind("assets/raw/", 0) == 0;
  }
  CHECK(sawUtf8);
  CHECK(!sawBuild);
  CHECK(!sawRaw);
  CHECK(fs::exists(src / ".ilmeee/sync-state.json"));
  auto again = ScanProject(src, err); // from the hash cache
  CHECK(again.size() == files.size());
  for (std::size_t i = 0; i < files.size() && i < again.size(); ++i)
    CHECK(files[i].hash == again[i].hash);

  // ---- WorkspaceCacheDir ----
  const auto cache = WorkspaceCacheDir("wss://realtime.example.com:443/ws", "my-room");
  CHECK(PathToUtf8(cache).find("workspaces/realtime.example.com_443/my-room") != std::string::npos);

  // ---- online round trip ----
  const char *url = std::getenv("ILMEEE_RT_URL");
  if (url && *url) {
    const char *tok = std::getenv("ILMEEE_RT_TOKEN");
    const std::string token = tok ? tok : "";
    const std::string room =
        "sync-test-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count() %
                                      100000000);
    SyncStatus last;
    int calls = 0;
    auto pub = PublishProject(url, token, room, src, "tester", [&](const SyncStatus &s) {
      last = s;
      ++calls;
    });
    if (!pub.ok)
      std::cerr << "  publish: " << pub.error << "\n";
    CHECK(pub.ok);
    CHECK(pub.files == 6);
    CHECK(calls > 0);
    // Second publish: nothing new to send.
    auto pub2 = PublishProject(url, token, room, src, "tester");
    CHECK(pub2.ok);
    CHECK(pub2.transferred == 0);

    const fs::path dst = root / "fresh";
    auto dl = DownloadProject(url, token, room, dst);
    if (!dl.ok)
      std::cerr << "  download: " << dl.error << "\n";
    CHECK(dl.ok);
    CHECK(dl.transferred == 6);
    CHECK(ReadFile(dst / "scenes/main.ilmeeescene") == std::string("ILMS\0\1binary", 12));
    CHECK(ReadFile(dst / PathFromUtf8(utf8)) == "pmx");
    CHECK(!fs::exists(dst / "build"));

    // Up to date: nothing transferred. Then the project changes.
    auto dl2 = DownloadProject(url, token, room, dst);
    CHECK(dl2.ok && dl2.transferred == 0);
    WriteFile(dst / "my-notes.txt", "local only"); // not ours: must survive
    WriteFile(src / "assets/textures/a.png", "png v2");
    fs::remove(src / "assets/textures/copy.png");
    CHECK(PublishProject(url, token, room, src, "tester").ok);
    auto dl3 = DownloadProject(url, token, room, dst);
    CHECK(dl3.ok);
    CHECK(dl3.transferred == 1);
    CHECK(dl3.removed == 1);
    CHECK(ReadFile(dst / "assets/textures/a.png") == "png v2");
    CHECK(!fs::exists(dst / "assets/textures/copy.png"));
    CHECK(fs::exists(dst / "my-notes.txt"));

    // Wrong token / unknown room.
    if (!token.empty())
      CHECK(!DownloadProject(url, "wrong", room, root / "x").ok);
    auto none = DownloadProject(url, token, room + "-nope", root / "y");
    CHECK(!none.ok);
    CHECK(none.error.find("no project files") != std::string::npos);

    // One file that cannot be fetched must not stop the others (it used to
    // abort the whole download). Needs the server's blob folder to break it.
    if (const char *fd = std::getenv("ILMEEE_RT_FILES_DIR"); fd && *fd) {
      const fs::path broken = root / "broken";
      WriteFile(broken / "a.txt", "file a");
      WriteFile(broken / "b.txt", "file b, lost on the server");
      WriteFile(broken / "c.txt", "file c");
      const std::string room2 = room + "-broken";
      CHECK(PublishProject(url, token, room2, broken, "tester").ok);
      const std::string hb = Sha256Hex("file b, lost on the server", 26);
      std::error_code rec;
      CHECK(fs::remove(fs::path(fd) / "blobs" / hb.substr(0, 2) / hb, rec));
      const fs::path got = root / "broken-copy";
      auto part = DownloadProject(url, token, room2, got);
      CHECK(!part.ok);
      CHECK(part.failed.size() == 1);
      CHECK(!part.failed.empty() && part.failed[0].rfind("b.txt: ", 0) == 0);
      CHECK(part.transferred == 3 || part.transferred == 2); // .ilmeee state may be listed
      CHECK(ReadFile(got / "a.txt") == "file a");
      CHECK(ReadFile(got / "c.txt") == "file c");
      CHECK(!fs::exists(got / "b.txt"));
      CHECK(part.error.find("could not be downloaded") != std::string::npos);
    }
  } else {
    std::cout << "(ILMEEE_RT_URL not set: online round trip skipped)\n";
  }

  std::error_code ec;
  fs::remove_all(root, ec);
  std::cout << (g_checks - g_failed) << "/" << g_checks << " checks passed\n";
  return g_failed ? 1 : 0;
}

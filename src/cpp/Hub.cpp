// IlmeeeHub — project launcher for the Ilmeee engine.
//
// Responsibilities:
//   1. Persist a list of recent projects in ~/.ilmeee/hub.json so the
//      user doesn't have to re-pick directories every time.
//   2. Surface each project's metadata (name, engine version, last
//      opened scene) read from <project>/.ilmeee/project.json.
//   3. Spawn the GameEngineSDL editor with `--project <abs_path>` so
//      the editor opens directly in the chosen project's context.
//
// The Hub deliberately keeps a small footprint: SDL3 + SDL3Renderer +
// ImGui only. Vulkan is reserved for the editor itself.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
// Use OpenGL3 as the ImGui renderer backend: the vendored imgui's
// SDLRenderer3 backend is half-migrated against a newer imgui API
// (references ImTextureData which doesn't exist in this copy).
// OpenGL3 backend is clean.
#include <glad/glad.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <core_engine/Process.hpp>
#include <core_engine/UserDataDir.hpp>
#include <core_engine/net/LanDiscovery.hpp>
#include <core_engine/net/CollabClient.hpp>
#include <core_engine/net/ProjectSync.hpp>
#include <json.hpp>
#include <nfd.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Editor binary resolution
// ---------------------------------------------------------------------------
// The Hub used to exec "./GameEngineSDL", which resolves against the
// *current working directory*. Launching the Hub from anywhere other
// than the directory holding the editor binary made exec fail with
// ENOENT (child exits 127). Resolve against the Hub's own executable
// directory instead, then fall back to a few sibling layouts and
// finally to PATH.
static fs::path ExecutableDir() {
  const std::string self = ilmeee::CurrentExecutablePath();
  if (!self.empty())
    return fs::path(self).parent_path();
  std::error_code ec;
  return fs::current_path(ec);
}

static std::tm LocalTime(std::time_t t) {
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  return tm;
}

#ifdef _WIN32
static constexpr const char *kEditorExe = "GameEngineSDL.exe";
#else
static constexpr const char *kEditorExe = "GameEngineSDL";
#endif

static std::string ResolveEditorBinary() {
  const char *override_env = std::getenv("ILMEEE_EDITOR_BIN");
  if (override_env && *override_env)
    return override_env;

  const fs::path dir = ExecutableDir();
  const fs::path candidates[] = {
      dir / kEditorExe,              // installed / build bin layout
      dir / "bin" / kEditorExe,      // Hub at prefix root
      dir.parent_path() / "bin" / kEditorExe,
      fs::path("build") / "bin" / kEditorExe, // run from repo root
  };
  std::error_code ec;
  for (const auto &c : candidates) {
    if (fs::exists(c, ec) && !ec)
      return fs::absolute(c, ec).string();
  }
  // Last resort: let the OS search PATH.
  return kEditorExe;
}

// True when ResolveEditorBinary() found a real file (or an explicit override).
// The Windows build ships without the editor until the port's stage 3, so the
// Hub must say so instead of failing with a bare CreateProcess error.
static bool EditorAvailable() {
  const char *override_env = std::getenv("ILMEEE_EDITOR_BIN");
  if (override_env && *override_env)
    return true;
#ifdef _WIN32
  // Checked once: the banner asks for it every frame.
  static const bool found = [] {
    std::error_code ec;
    return fs::exists(ResolveEditorBinary(), ec);
  }();
  return found;
#else
  return true; // a PATH lookup can still succeed on Linux installs
#endif
}

static constexpr const char *kEditorMissingText =
    "The editor (GameEngineSDL) is not part of this Windows build yet. "
    "The Hub, online workspaces and the game player work; opening a project "
    "needs the editor, which comes with a later release.";

// ---------------------------------------------------------------------------
// Global debug mode flag
// ---------------------------------------------------------------------------
static bool g_DebugMode = false;

// ---------------------------------------------------------------------------
// Data model
// ---------------------------------------------------------------------------

struct ProjectEntry {
  std::string name;
  std::string path;          // absolute path to project root
  std::string engineVersion; // "0.1" if unknown
  std::string lastOpened;    // ISO date, e.g. "2026-05-17"
  std::string lastScene;     // optional, last opened scene filename
  bool mcpEnabled = false;   // Hub launch preference, stored in hub.json
};

// Hub-wide MCP settings (hub.json "mcp"). Shared by every project because
// only one editor at a time can own port 9220.
struct McpSettings {
  std::string token;      // passed as ILMEEE_MCP_TOKEN; empty = per-session
  std::string publicHost; // tunnel hostname for --mcp-public-host
};

// ---------------------------------------------------------------------------
// Log aggregator: combines Hub's own messages and captured stdout/stderr
// from every spawned editor process so the user gets a single tail for
// deep debugging.
// ---------------------------------------------------------------------------

struct LogEntry {
  std::string channel; // "Hub" or "Editor:<pid>"
  std::string time;    // "HH:MM:SS"
  std::string text;
};

class LogStore {
public:
  void Push(std::string channel, std::string text) {
    if (text.empty())
      return;
    // Normalize trailing \r from CRLF lines.
    if (!text.empty() && text.back() == '\r')
      text.pop_back();
    char tbuf[16];
    std::time_t t = std::time(nullptr);
    const std::tm tm = LocalTime(t);
    std::snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min,
                  tm.tm_sec);
    std::lock_guard<std::mutex> lk(mu);
    entries.push_back({std::move(channel), tbuf, std::move(text)});
    while (entries.size() > kMax)
      entries.pop_front();
    bumped = true;
  }
  std::vector<LogEntry> Snapshot() {
    std::lock_guard<std::mutex> lk(mu);
    return std::vector<LogEntry>(entries.begin(), entries.end());
  }
  void Clear() {
    std::lock_guard<std::mutex> lk(mu);
    entries.clear();
  }
  bool ConsumeBump() {
    std::lock_guard<std::mutex> lk(mu);
    bool b = bumped;
    bumped = false;
    return b;
  }

  static constexpr size_t kMax = 5000;

private:
  mutable std::mutex mu;
  std::deque<LogEntry> entries;
  bool bumped = false;
};

// Strip ANSI escape sequences (the editor's Debug::Log writes colored
// output via \x1b[...m). Without this the ImGui console would render
// raw escape bytes.
static std::string StripAnsi(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    unsigned char c = (unsigned char)s[i];
    if (c == 0x1b && i + 1 < s.size() && s[i + 1] == '[') {
      i += 2;
      while (i < s.size() &&
             !((unsigned char)s[i] >= 0x40 && (unsigned char)s[i] <= 0x7e))
        ++i;
      // i is on the final byte of the CSI sequence; loop increment skips it.
    } else if (c == 0x1b) {
      // Lone ESC: skip.
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

struct ChildProc {
  std::shared_ptr<ilmeee::Process> proc;
  long pid = -1;
  std::string label;       // "Editor:<pid>"
  std::string projectPath; // for display
  bool mcpEnabled = false;
  std::mutex tokenMutex;
  std::string mcpToken;
  std::string publicHost; // --mcp-public-host given at launch
  std::string collabRoom;  // online workspace it was opened for ("" = none)
  std::string collabServer; // LAN server URL it joined ("" = the online server)
  std::thread reader;
  std::atomic<bool> alive{false};
};

// ---------------------------------------------------------------------------
// Filesystem / persistence helpers
// ---------------------------------------------------------------------------

namespace {

std::string TodayIsoDate() {
  const std::tm tm = LocalTime(std::time(nullptr));
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900,
                tm.tm_mon + 1, tm.tm_mday);
  return buf;
}

std::string HomeDir() { return ilmeee::HomeDirectory(); }

std::string HubConfigPath() { return ilmeee::HubIndexPath().string(); }

bool EnsureParentDir(const std::string &filePath) {
  fs::path p = fs::path(filePath).parent_path();
  std::error_code ec;
  fs::create_directories(p, ec);
  return !ec;
}

// Read <project>/.ilmeee/project.json if present and merge into entry.
// Missing/invalid files are silently ignored — the hub's recent list
// is authoritative for path/name.
void ReadProjectMetadata(ProjectEntry &entry) {
  fs::path meta = fs::path(entry.path) / ".ilmeee" / "project.json";
  std::ifstream f(meta);
  if (!f.is_open())
    return;
  try {
    json j;
    f >> j;
    if (j.contains("name"))
      entry.name = j["name"].get<std::string>();
    if (j.contains("engineVersion"))
      entry.engineVersion = j["engineVersion"].get<std::string>();
    if (j.contains("lastScene"))
      entry.lastScene = j["lastScene"].get<std::string>();
  } catch (...) {
    // Bad JSON shouldn't kill Hub startup.
  }
}

// Write/refresh <project>/.ilmeee/project.json so projects created or
// opened through Hub always carry the engine version + last scene info.
void WriteProjectMetadata(const ProjectEntry &entry) {
  fs::path metaDir = fs::path(entry.path) / ".ilmeee";
  std::error_code ec;
  fs::create_directories(metaDir, ec);
  fs::path meta = metaDir / "project.json";
  json j;
  j["name"] = entry.name;
  j["engineVersion"] =
      entry.engineVersion.empty() ? "0.1" : entry.engineVersion;
  j["lastScene"] = entry.lastScene;
  std::ofstream f(meta);
  if (f.is_open())
    f << j.dump(2);
}

// hub.json holds secrets (MCP token), so keep it private to the user.
json ReadHubJson() {
  std::ifstream f(HubConfigPath());
  json j = json::parse(f, nullptr, false);
  return j.is_object() ? j : json::object();
}

void WriteHubJson(const json &j) {
  std::string p = HubConfigPath();
  if (p.empty())
    return;
  EnsureParentDir(p);
  std::ofstream f(p);
  if (!f.is_open())
    return;
  f << j.dump(2);
  f.close();
  std::error_code ec;
  fs::permissions(p, fs::perms::owner_read | fs::perms::owner_write,
                  fs::perm_options::replace, ec);
}

McpSettings LoadMcpSettings() {
  McpSettings out;
  const json j = ReadHubJson();
  if (j.contains("mcp") && j["mcp"].is_object()) {
    out.token = j["mcp"].value("token", "");
    out.publicHost = j["mcp"].value("publicHost", "");
  }
  return out;
}

void SaveMcpSettings(const McpSettings &settings) {
  json j = ReadHubJson();
  j["mcp"] = {{"token", settings.token}, {"publicHost", settings.publicHost}};
  WriteHubJson(j);
}

// 32 random bytes as hex, same strength as the editor's session token.
std::string GenerateMcpToken() {
  std::random_device rd;
  static const char hex[] = "0123456789abcdef";
  std::string token;
  for (int i = 0; i < 32; ++i) {
    const unsigned byte = rd() & 0xFF;
    token += hex[byte >> 4];
    token += hex[byte & 0xF];
  }
  return token;
}

// ~/.ilmeeeengine/collab.json is shared with the editor's Realtime Collab
// window: server URL, token, display name, color, and rooms[<project path>].
fs::path CollabJsonPath() { return ilmeee::UserDataRoot() / "collab.json"; }

json ReadCollabJson() {
  std::ifstream f(CollabJsonPath());
  json j = f ? json::parse(f, nullptr, false) : json::object();
  return j.is_object() ? j : json::object();
}

void WriteCollabJson(const json &j) {
  std::error_code ec;
  fs::create_directories(CollabJsonPath().parent_path(), ec);
  {
    std::ofstream f(CollabJsonPath(), std::ios::trunc);
    f << j.dump(2);
  }
  // It holds the server token: private to the user.
  fs::permissions(CollabJsonPath(), fs::perms::owner_read | fs::perms::owner_write,
                  fs::perm_options::replace, ec);
}

// Same rule as the server / editor: [A-Za-z0-9_.-], max 64, no leading dot.
std::string SanitizeRoomName(const std::string &in) {
  std::string out;
  for (char c : in)
    if (std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.')
      out += c;
    else if (!out.empty() && out.back() != '-')
      out += '-';
  while (!out.empty() && (out.front() == '.' || out.front() == '-'))
    out.erase(out.begin());
  if (out.size() > 64)
    out.resize(64);
  return out;
}

std::vector<ProjectEntry> LoadRecentProjects() {
  std::vector<ProjectEntry> out;
  std::ifstream f(HubConfigPath());
  if (!f.is_open())
    return out;
  try {
    json j;
    f >> j;
    if (j.contains("projects") && j["projects"].is_array()) {
      for (auto &item : j["projects"]) {
        ProjectEntry e;
        e.name = item.value("name", "");
        e.path = item.value("path", "");
        e.engineVersion = item.value("engineVersion", "0.1");
        e.lastOpened = item.value("lastOpened", "");
        e.lastScene = item.value("lastScene", "");
        if (item.contains("mcpEnabled") && item["mcpEnabled"].is_boolean())
          e.mcpEnabled = item["mcpEnabled"].get<bool>();
        if (e.path.empty())
          continue;
        // Hub config is the index; project.json is the source of truth
        // for per-project metadata.
        ReadProjectMetadata(e);
        out.push_back(std::move(e));
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "Hub: failed to read " << HubConfigPath() << ": " << e.what()
              << "\n";
  }
  return out;
}

void SaveRecentProjects(const std::vector<ProjectEntry> &projects) {
  json j = ReadHubJson(); // keep other keys such as "mcp"
  j["projects"] = json::array();
  for (const auto &e : projects) {
    j["projects"].push_back({{"name", e.name},
                             {"path", e.path},
                             {"engineVersion", e.engineVersion},
                             {"lastOpened", e.lastOpened},
                             {"lastScene", e.lastScene},
                             {"mcpEnabled", e.mcpEnabled}});
  }
  WriteHubJson(j);
}

} // namespace

// ---------------------------------------------------------------------------
// Hub UI
// ---------------------------------------------------------------------------

class IlmeeeHub {
public:
  IlmeeeHub() {
    projects = LoadRecentProjects();
    mcpSettings = LoadMcpSettings();
    std::snprintf(mcpTokenBuf, sizeof(mcpTokenBuf), "%s",
                  mcpSettings.token.c_str());
    std::snprintf(mcpHostBuf, sizeof(mcpHostBuf), "%s",
                  mcpSettings.publicHost.c_str());
    NFD::Init();
    logStore.Push("Hub", "IlmeeeHub started.");
  }

  ~IlmeeeHub() {
    // Coupling editor lifetime to Hub keeps the log capture loop honest
    // — without termination the child's pipe would never EOF and the
    // reader thread would hang forever on shutdown.
    for (auto &c : children) {
      if (c->alive && c->proc)
        c->proc->Terminate();
    }
    if (roomFetch.joinable())
      roomFetch.join();
    StopLanServer();
    if (lanScan.joinable())
      lanScan.join();
    syncJob.cancel = true;
    if (syncJob.thread.joinable())
      syncJob.thread.join();
    for (auto &c : children) {
      if (c->reader.joinable())
        c->reader.join();
    }
    NFD::Quit();
  }

  LogStore &Logs() { return logStore; }
  const std::vector<std::shared_ptr<ChildProc>> &Children() const {
    return children;
  }

  // Launch the editor binary as a child process, redirecting its
  // stdout+stderr into a pipe whose reader thread feeds logStore. The
  // process is intentionally kept in the same process group so the
  // user can quit everything by closing Hub.
  // collabUrl/collabToken: a LAN server for this session (else the editor
  // uses the online server saved in collab.json).
  bool LaunchEditor(const std::string &projectPath, bool mcpEnabled,
                    const std::string &collabRoom = {}, const std::string &collabUrl = {},
                    const std::string &collabToken = {}) {
    if (projectPath.empty()) {
      logStore.Push("Hub", "LaunchEditor: empty project path.");
      return false;
    }
    if (mcpEnabled) {
      for (const auto &child : children) {
        if (child->alive && child->mcpEnabled) {
          logStore.Push("Hub", "MCP port 9220 is already used by an open editor. "
                                "Close it or turn off Use MCP for this project.");
          return false;
        }
      }
    }

    if (!EditorAvailable()) {
      logStore.Push("Hub", kEditorMissingText);
      return false;
    }

    ilmeee::ProcessOptions o;
    o.program = ResolveEditorBinary();
#ifdef _WIN32
    // The editor finds assets/ (shaders, icons, fonts) relative to its
    // working directory; on Windows they sit next to the installed exe.
    o.workingDir = fs::path(o.program).parent_path().string();
#endif
    o.args = {"--project", projectPath};
    if (g_DebugMode)
      o.args.push_back("--debug");
    if (mcpEnabled) {
      o.args.push_back("--mcp");
      // Token goes through the environment, not argv, so it stays out of
      // `ps` / Task Manager command lines.
      if (!mcpSettings.token.empty())
        o.env.emplace_back("ILMEEE_MCP_TOKEN", mcpSettings.token);
      if (!mcpSettings.publicHost.empty()) {
        o.args.push_back("--mcp-public-host");
        o.args.push_back(mcpSettings.publicHost);
      }
    }
    if (!collabRoom.empty()) {
      o.args.push_back("--collab-room");
      o.args.push_back(collabRoom);
      if (!collabUrl.empty()) {
        // Environment, not argv: the token stays out of process listings.
        o.env.emplace_back("ILMEEE_COLLAB_URL", collabUrl);
        o.env.emplace_back("ILMEEE_COLLAB_TOKEN", collabToken);
      }
    }

    auto proc = std::make_shared<ilmeee::Process>();
    std::string error;
    if (!proc->Start(o, error)) {
      logStore.Push("Hub", "Could not start the editor (" + o.program + "): " + error);
      return false;
    }

    auto child = std::make_shared<ChildProc>();
    child->proc = proc;
    child->pid = proc->Pid();
    child->label = "Editor:" + std::to_string(child->pid);
    child->projectPath = projectPath;
    child->mcpEnabled = mcpEnabled;
    child->collabRoom = collabRoom;
    child->collabServer = collabUrl;
    if (mcpEnabled) {
      // A configured token is never logged by the editor, so remember it.
      child->mcpToken = mcpSettings.token;
      child->publicHost = mcpSettings.publicHost;
    }
    child->alive.store(true);

    LogStore *store = &logStore;
    const std::string label = child->label;
    child->reader = std::thread([label, store, child]() {
      std::string buf;
      char chunk[4096];
      long n;
      while ((n = child->proc->Read(chunk, sizeof(chunk))) > 0) {
        buf.append(chunk, (size_t)n);
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
          std::string line = buf.substr(0, pos);
          buf.erase(0, pos + 1);
          if (!line.empty() && line.back() == '\r')
            line.pop_back();
          std::string clean = StripAnsi(line);
          const std::string marker = "[MCP] Session token: ";
          const size_t tokenPos = clean.find(marker);
          if (tokenPos != std::string::npos) {
            std::lock_guard<std::mutex> lock(child->tokenMutex);
            child->mcpToken = clean.substr(tokenPos + marker.size());
          }
          store->Push(label, std::move(clean));
        }
      }
      if (!buf.empty())
        store->Push(label, StripAnsi(buf));
      const int code = child->proc->Wait();
      child->alive.store(false);
      std::string exitMsg = "Editor pid=" + std::to_string(child->pid) + " exited (code " +
                            std::to_string(code) + ")";
      if (code == 127)
        exitMsg += " - editor binary not found or not runnable";
      store->Push("Hub", exitMsg);
    });

    children.push_back(child);
    logStore.Push("Hub", "Launched editor pid=" + std::to_string(child->pid) +
                             " project=" + projectPath +
                             (mcpEnabled ? " MCP=on" : " MCP=off") +
                             (collabRoom.empty() ? "" : " workspace=" + collabRoom));
    return true;
  }

  void SetupTheme() {
    ImGuiStyle &s = ImGui::GetStyle();
    ImVec4 *c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.080f, 0.088f, 1.0f);
    c[ImGuiCol_ChildBg] = ImVec4(0.085f, 0.090f, 0.100f, 1.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.13f, 0.14f, 0.16f, 0.99f);
    c[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.16f, 0.18f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.21f, 0.24f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.28f, 0.36f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.22f, 0.30f, 0.43f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.38f, 0.52f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.32f, 0.43f, 0.58f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.19f, 0.25f, 0.34f, 1.0f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.23f, 0.30f, 0.39f, 1.0f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.27f, 0.36f, 0.48f, 1.0f);
    c[ImGuiCol_CheckMark] = ImVec4(0.70f, 0.82f, 0.98f, 1.0f);
    c[ImGuiCol_Text] = ImVec4(0.91f, 0.93f, 0.95f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.56f, 0.60f, 0.65f, 1.0f);
    c[ImGuiCol_Border] = ImVec4(0.28f, 0.31f, 0.35f, 0.55f);
    c[ImGuiCol_Separator] = ImVec4(0.25f, 0.28f, 0.32f, 0.65f);
    s.WindowRounding = 7.0f;
    s.ChildRounding = 8.0f;
    s.FrameRounding = 5.0f;
    s.PopupRounding = 6.0f;
    s.GrabRounding = 4.0f;
    s.WindowPadding = ImVec2(14, 14);
    s.FramePadding = ImVec2(11, 7);
    s.ItemSpacing = ImVec2(9, 9);
  }

  // Top-level render — single full-window root, two-pane top + console
  // strip at the bottom for the unified log feed.
  void Render() {
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##IlmeeeHubRoot", nullptr, flags);
    ImGui::PopStyleVar();

    const float consoleHeight = consoleVisible ? 230.0f : 28.0f;

    ImGui::BeginChild("##HubTop", ImVec2(0, -consoleHeight), false);
    DrawSidebar();
    ImGui::SameLine(0.0f, 0.0f);
    DrawMainPane();
    ImGui::EndChild();

    DrawConsole();

    ImGui::End();
    PollSyncJob();

    if (showNewProjectDialog)
      DrawNewProjectDialog();
  }

  // ---------------- Console (unified log feed) ----------------
  void DrawConsole() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.05f, 0.07f, 1.0f));
    ImGui::BeginChild("##Console", ImVec2(0, 0), true);
    ImGui::PopStyleColor();

    // Toolbar
    if (ImGui::ArrowButton("##ConsoleToggle",
                           consoleVisible ? ImGuiDir_Down : ImGuiDir_Up)) {
      consoleVisible = !consoleVisible;
    }
    ImGui::SameLine();
    ImGui::Text("Console");
    ImGui::SameLine();
    ImGui::TextDisabled("(%d entries)", (int)logStore.Snapshot().size());

    if (!consoleVisible) {
      ImGui::EndChild();
      return;
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear"))
      logStore.Clear();
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &consoleAutoScroll);
    ImGui::SameLine();
    ImGui::PushItemWidth(220);
    ImGui::InputTextWithHint("##LogFilter", "Filter text...", consoleFilter,
                             IM_ARRAYSIZE(consoleFilter));
    ImGui::PopItemWidth();
    ImGui::SameLine();

    // Channel selector populated from active children.
    const char *currentChannel =
        consoleChannel.empty() ? "All" : consoleChannel.c_str();
    ImGui::PushItemWidth(140);
    if (ImGui::BeginCombo("##Channel", currentChannel)) {
      if (ImGui::Selectable("All", consoleChannel.empty()))
        consoleChannel.clear();
      if (ImGui::Selectable("Hub", consoleChannel == "Hub"))
        consoleChannel = "Hub";
      for (const auto &c : children) {
        bool sel = consoleChannel == c->label;
        if (ImGui::Selectable(c->label.c_str(), sel))
          consoleChannel = c->label;
      }
      ImGui::EndCombo();
    }
    ImGui::PopItemWidth();

    ImGui::Separator();

    // Log scroll area
    ImGui::BeginChild("##LogScroll", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    auto snap = logStore.Snapshot();
    const std::string filter = consoleFilter;
    for (const auto &e : snap) {
      if (!consoleChannel.empty() && e.channel != consoleChannel)
        continue;
      if (!filter.empty() && e.text.find(filter) == std::string::npos &&
          e.channel.find(filter) == std::string::npos)
        continue;
      ImVec4 col = (e.channel == "Hub") ? ImVec4(0.55f, 0.80f, 1.00f, 1.0f)
                                        : ImVec4(0.95f, 0.85f, 0.55f, 1.0f);
      ImGui::TextColored(col, "[%s][%s]", e.time.c_str(), e.channel.c_str());
      ImGui::SameLine();
      ImGui::TextUnformatted(e.text.c_str());
    }
    bool bumped = logStore.ConsumeBump();
    if (consoleAutoScroll && bumped)
      ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    ImGui::EndChild();
  }

private:
  enum class Section { Projects, Online, Templates, Learn, Settings };

  std::vector<ProjectEntry> projects;
  Section currentSection = Section::Projects;
  char searchBuffer[128] = "";
  bool showNewProjectDialog = false;
  McpSettings mcpSettings;
  char mcpTokenBuf[128] = "";
  char mcpHostBuf[256] = "";
  bool showMcpToken = false;
  char newProjName[128] = "";
  char newProjLocation[512] = "";
  std::string newProjError;

  // Online workspaces (realtime collab rooms on the server).
  bool onlineLoaded = false;
  bool showOnlineToken = false;
  char onlineUrl[256] = "";
  char onlineToken[128] = "";
  char onlineName[64] = "";
  std::thread roomFetch;
  std::atomic<bool> fetching{false};
  std::mutex roomMutex;
  ilmeee::net::CollabRoomList roomList; // guarded by roomMutex
  bool roomListValid = false;           // guarded by roomMutex
  double lastFetch = -1000.0;
  struct WsTarget {
    std::string url;      // ws://... or wss://...
    std::string token;
    std::string cacheKey; // WorkspaceCacheDirForKey; "" = derive from url
    std::string label;    // shown on cards / messages
    bool lan = false;
    std::string serverId; // LAN only
  };

  std::string pickForRoom; // workspace waiting for "which local project?"
  WsTarget pickTarget;     // ... and the server it is on
  int hostProjectIdx = 0;
  char hostRoom[64] = "";
  bool hostUploadFiles = true; // "Go online" also publishes the project files

  // Project files going up to / coming down from the server (one at a time).
  struct SyncJob {
    std::thread thread;
    std::atomic<bool> running{false};
    std::atomic<bool> finished{false}; // result ready, not handled yet
    std::atomic<bool> cancel{false};
    std::mutex mutex;
    ilmeee::net::SyncStatus status;   // guarded by mutex
    ilmeee::net::SyncResult result;   // valid once finished
    bool upload = false;
    bool openAfter = false;
    std::string room, project;
    WsTarget target; // server the job talks to
  } syncJob;

  // LAN: the server this Hub hosts (Host on LAN) and the ones found nearby.
  struct LanHostProc {
    std::shared_ptr<ilmeee::Process> proc;
    std::thread reader;
    std::atomic<bool> alive{false};
    std::atomic<int> exitCode{0};
  };
  struct LanEntry {
    ilmeee::net::LanServer server;
    ilmeee::net::CollabRoomList rooms;
  };
  char lanPassword[64] = "";
  std::string lanServerId; // stable id of this computer's LAN server
  std::shared_ptr<LanHostProc> lanHost;
  std::string lanHostError;
  std::thread lanAnnouncer;
  std::atomic<bool> lanAnnounceStop{false};
  std::condition_variable lanCv;
  std::mutex lanMutex;                 // guards the fields below + lanCv
  std::string lanAnnounceStatus;
  std::vector<LanEntry> lanFound;
  std::string lanRendezvousError;
  bool lanScanned = false;
  std::thread lanScan;
  std::atomic<bool> lanScanning{false};
  double lastLanScan = -1000.0;
  std::string syncMessage; // last outcome, shown on the Online page
  bool syncMessageError = false;

  // Log capture state.
  LogStore logStore;
  std::vector<std::shared_ptr<ChildProc>> children;
  // Console UI state.
  bool consoleVisible = true;
  bool consoleAutoScroll = true;
  char consoleFilter[128] = "";
  // Channel filter: "" = all, "Hub" = hub only, "Editor" = any editor.
  std::string consoleChannel = "";

  // ---------------- Sidebar ----------------
  void DrawSidebar() {
    ImGui::BeginChild("##Sidebar", ImVec2(220, 0), false);

    // Logo header
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.065f, 0.070f, 0.079f, 1.0f));
    ImGui::BeginChild("##Logo", ImVec2(0, 96), false);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.82f, 0.98f, 1.0f));
    ImGui::SetCursorPos(ImVec2(22, 24));
    ImGui::SetWindowFontScale(1.16f);
    ImGui::TextUnformatted("ILMEEE");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::SetCursorPos(ImVec2(22, 51));
    ImGui::TextDisabled("PROJECT HUB  /  0.1");
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::SetCursorPos(ImVec2(20, ImGui::GetCursorPosY() + 8));
    ImGui::TextDisabled("WORKSPACE");
    ImGui::Spacing();
    SidebarButton("Projects", Section::Projects);
    SidebarButton("Online Workspaces", Section::Online);
    SidebarButton("Templates", Section::Templates);
    SidebarButton("Learn", Section::Learn);
    SidebarButton("Settings", Section::Settings);

    // Footer
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 56);
    ImGui::Separator();
    ImGui::TextDisabled("Engine: 0.1");
    ImGui::TextDisabled("Vulkan · SDL3");

    ImGui::EndChild();
  }

  void SidebarButton(const char *label, Section sec) {
    bool active = (currentSection == sec);
    ImGui::SetCursorPosX(12);
    const float width = ImGui::GetContentRegionAvail().x - 12.0f;
    if (ImGui::Selectable(label, active, 0, ImVec2(width, 36)))
      currentSection = sec;
    if (active) {
      const ImVec2 min = ImGui::GetItemRectMin();
      ImGui::GetWindowDrawList()->AddRectFilled(
          min, ImVec2(min.x + 3.0f, min.y + 36.0f),
          ImGui::GetColorU32(ImVec4(0.62f, 0.77f, 0.96f, 1.0f)));
    }
  }

  // ---------------- Main pane ----------------
  void DrawMainPane() {
    ImGui::BeginChild("##Main", ImVec2(0, 0), false);

    DrawTopBar();
    ImGui::Separator();
    ImGui::Spacing();

    if (!EditorAvailable()) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.35f, 1.0f));
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextUnformatted(kEditorMissingText);
      ImGui::PopTextWrapPos();
      ImGui::PopStyleColor();
      ImGui::Spacing();
    }

    switch (currentSection) {
    case Section::Projects:
      DrawProjectList();
      break;
    case Section::Online:
      DrawOnlinePane();
      break;
    case Section::Templates:
      DrawTemplatesPlaceholder();
      break;
    case Section::Learn:
      DrawLearnPlaceholder();
      break;
    case Section::Settings:
      DrawSettingsPane();
      break;
    }

    ImGui::EndChild();
  }

  void DrawTopBar() {
    const float available = ImGui::GetContentRegionAvail().x;
    const bool compact = available < 820.0f;
    ImGui::BeginChild("##Topbar", ImVec2(0, compact ? 116.0f : 76.0f), false);
    const char *title = currentSection == Section::Projects ? "Projects" :
                        currentSection == Section::Online ? "Online Workspaces" :
                        currentSection == Section::Templates ? "Templates" :
                        currentSection == Section::Learn ? "Learn" : "Settings";
    ImGui::SetCursorPos(ImVec2(20, 13));
    ImGui::SetWindowFontScale(1.16f);
    ImGui::TextUnformatted(title);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::SetCursorPos(ImVec2(20, 39));
    ImGui::TextDisabled("Ilmeee Engine workspace");

    if (compact)
      ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 244.0f, 20));
    else
      ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 264.0f, 21));
    if (ImGui::Button("Open Folder")) {
      OpenExistingProject();
    }
    ImGui::SameLine(0, 8);
    if (ImGui::Button("+ New Project")) {
      showNewProjectDialog = true;
      newProjName[0] = '\0';
      // Default location: $HOME/IlmeeeProjects
      std::string home = HomeDir();
      if (!home.empty()) {
        std::string def = home + "/IlmeeeProjects";
        std::snprintf(newProjLocation, sizeof(newProjLocation), "%s",
                      def.c_str());
      } else {
        newProjLocation[0] = '\0';
      }
      newProjError.clear();
    }
    if (compact)
      ImGui::SetCursorPos(ImVec2(20, 76));
    else
      ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 544.0f, 21));
    ImGui::PushItemWidth(compact ? ImGui::GetWindowWidth() - 40.0f : 270.0f);
    ImGui::InputTextWithHint("##Search", "Search projects...", searchBuffer,
                             IM_ARRAYSIZE(searchBuffer));
    ImGui::PopItemWidth();
    ImGui::EndChild();
  }

  void DrawProjectList() {
    if (projects.empty()) {
      ImGui::TextDisabled(
          "No projects yet. Click 'Open Folder' or '+ New Project' to start.");
      return;
    }

    std::string filter = searchBuffer;
    std::transform(filter.begin(), filter.end(), filter.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 16);
    ImGui::TextDisabled("RECENT PROJECTS  /  %zu", projects.size());
    ImGui::Spacing();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 16);
    ImGui::BeginChild("##ProjectScroll",
                      ImVec2(ImGui::GetContentRegionAvail().x - 16, 0), false);
    for (size_t i = 0; i < projects.size(); ++i) {
      auto &p = projects[i];
      if (!filter.empty()) {
        std::string lname = p.name, lpath = p.path;
        std::transform(lname.begin(), lname.end(), lname.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        std::transform(lpath.begin(), lpath.end(), lpath.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        if (lname.find(filter) == std::string::npos &&
            lpath.find(filter) == std::string::npos)
          continue;
      }
      DrawProjectCard(p, i);
    }
    ImGui::EndChild();
  }

  void DrawProjectCard(ProjectEntry &p, size_t index) {
    ImGui::PushID((int)index);
    const bool compact = ImGui::GetContentRegionAvail().x < 590.0f;
    const float cardHeight = compact ? 152.0f : 116.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.125f, 0.135f, 0.151f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.27f, 0.30f, 0.35f, 0.75f));
    ImGui::BeginChild("##Card", ImVec2(0, cardHeight), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const float cardWidth = ImGui::GetWindowWidth();
    const ImVec2 cardMin = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(cardMin.x + 1, cardMin.y + 12),
        ImVec2(cardMin.x + 4, cardMin.y + cardHeight - 12),
        ImGui::GetColorU32(p.mcpEnabled ? ImVec4(0.58f, 0.75f, 0.95f, 1.0f)
                                        : ImVec4(0.34f, 0.39f, 0.45f, 1.0f)));

    ImGui::SetCursorPos(ImVec2(18, 13));
    ImGui::TextUnformatted(p.name.empty() ? "(unnamed)" : p.name.c_str());
    ImGui::SameLine(0, 12);
    ImGui::TextDisabled("v%s", p.engineVersion.empty() ? "0.1" : p.engineVersion.c_str());
    if (!p.lastOpened.empty() && cardWidth > 670.0f && p.name.size() < 32) {
      const std::string opened = "Opened " + p.lastOpened;
      ImGui::SetCursorPos(ImVec2(cardWidth - ImGui::CalcTextSize(opened.c_str()).x - 18, 15));
      ImGui::TextDisabled("%s", opened.c_str());
    }

    ImGui::SetCursorPos(ImVec2(18, 40));
    ImGui::TextDisabled("%s", p.path.c_str());

    ImGui::SetCursorPos(ImVec2(18, 76));
    if (ImGui::Checkbox("Use MCP", &p.mcpEnabled))
      SaveRecentProjects(projects);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Applies the next time this project is opened");
    bool mcpRunning = false;
    std::string token, publicHost;
    for (const auto &child : children) {
      if (!child->alive || !child->mcpEnabled || child->projectPath != p.path)
        continue;
      mcpRunning = true;
      publicHost = child->publicHost;
      std::lock_guard<std::mutex> lock(child->tokenMutex);
      token = child->mcpToken;
      break;
    }
    ImGui::SameLine(0, 12);
    ImGui::TextDisabled(mcpRunning ? "Active: 127.0.0.1:9220"
                                    : p.mcpEnabled ? "On launch: 127.0.0.1:9220"
                                                   : "Off on next launch");
    if (!token.empty() && (compact ? cardWidth >= 335.0f : cardWidth > 760.0f)) {
      if (compact)
        ImGui::SetCursorPos(ImVec2(240, 111));
      else
        ImGui::SameLine(0, 12);
      if (ImGui::SmallButton("Copy token"))
        ImGui::SetClipboardText(token.c_str());
      if (!publicHost.empty() && !compact) {
        ImGui::SameLine(0, 6);
        if (ImGui::SmallButton("Copy URL"))
          ImGui::SetClipboardText(
              ("https://" + publicHost + "/mcp/" + token).c_str());
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Connector URL for claude.ai (contains the token)");
      }
    }

    ImGui::SetCursorPos(compact ? ImVec2(18, 111)
                                : ImVec2(cardWidth - 220, 73));
    if (ImGui::Button("Open project", ImVec2(116, 30))) {
      LaunchAndTouch(index);
    }
    ImGui::SameLine(0, 8);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.19f, 0.21f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.27f, 0.23f, 0.25f, 1.0f));
    const bool remove = ImGui::Button("Remove", ImVec2(78, 30));
    ImGui::PopStyleColor(2);
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopID();
    if (remove) {
      projects.erase(projects.begin() + index);
      SaveRecentProjects(projects);
    }
    ImGui::Dummy(ImVec2(0, 8));
  }

  // ---------------- Online + LAN workspaces ----------------
  //
  // A workspace lives on a realtime server: the online one (Settings below)
  // or a server on this network (Host on LAN, or found by LAN discovery).
  // Everything that joins, downloads or uploads takes the WsTarget it is for.
  WsTarget OnlineTarget() const {
    return {onlineUrl, onlineToken, "", "online server", false, ""};
  }

  WsTarget LanTargetFor(const ilmeee::net::LanServer &s) const {
    return {s.WsUrl(), lanPassword, "lan-" + s.id,
            (s.name.empty() ? s.id : s.name) + " (" + s.host + ")", true, s.id};
  }

  // The LAN server this Hub runs (Host on LAN), reached over loopback.
  WsTarget OwnLanTarget() const {
    ilmeee::net::LanServer s;
    s.id = lanServerId;
    s.name = std::string(onlineName) + " (this computer)";
    s.host = "127.0.0.1";
    s.port = ilmeee::net::kLanServerPort;
    WsTarget t = LanTargetFor(s);
    t.label = "this computer (LAN)";
    return t;
  }

  fs::path CacheDirFor(const WsTarget &t, const std::string &room) const {
    return t.cacheKey.empty() ? ilmeee::net::WorkspaceCacheDir(t.url, room)
                              : ilmeee::net::WorkspaceCacheDirForKey(t.cacheKey, room);
  }

  void LoadOnlineSettings() {
    onlineLoaded = true;
    json j = ReadCollabJson();
    std::snprintf(onlineUrl, sizeof onlineUrl, "%s",
                  j.value("url", std::string(ilmeee::net::kCollabDefaultUrl)).c_str());
    std::snprintf(onlineToken, sizeof onlineToken, "%s", j.value("token", std::string()).c_str());
    std::string name = j.value("name", std::string());
    if (name.empty()) {
      const char *u = std::getenv("USER");
      if (!u || !*u)
        u = std::getenv("USERNAME");
      name = u && *u ? u : "editor";
    }
    std::snprintf(onlineName, sizeof onlineName, "%s", name.c_str());
    std::snprintf(lanPassword, sizeof lanPassword, "%s",
                  j.value("lanPassword", std::string()).c_str());
    lanServerId = j.value("lanServerId", std::string());
    const bool idOk = lanServerId.size() >= 8 &&
                      std::all_of(lanServerId.begin(), lanServerId.end(),
                                  [](unsigned char c) { return std::isalnum(c); });
    if (!idOk) {
      // Stable id of this computer's LAN server: copies downloaded from it
      // are found again after the router hands out a new address.
      static const char *hex = "0123456789abcdef";
      std::random_device rd;
      lanServerId = "h";
      for (int i = 0; i < 15; ++i)
        lanServerId += hex[rd() & 15];
      j["lanServerId"] = lanServerId;
      WriteCollabJson(j);
    }
  }

  void SaveOnlineSettings() {
    json j = ReadCollabJson();
    j["url"] = std::string(onlineUrl);
    j["token"] = std::string(onlineToken);
    j["name"] = std::string(onlineName);
    j["lanPassword"] = std::string(lanPassword);
    WriteCollabJson(j);
  }

  // collab.json links a local folder to a workspace: rooms[path] = room for
  // the online server (the editor uses the same map), lanRooms[path] =
  // "<server id>/<room>" for LAN servers.
  static std::string LinkValue(const WsTarget &t, const std::string &room) {
    return t.lan ? t.serverId + "/" + room : room;
  }
  static const char *LinkMap(const WsTarget &t) { return t.lan ? "lanRooms" : "rooms"; }

  std::string LocalCopyOf(const std::string &room, const WsTarget &t) {
    const json j = ReadCollabJson();
    const std::string want = LinkValue(t, room);
    if (auto r = j.find(LinkMap(t)); r != j.end() && r->is_object())
      for (auto it = r->begin(); it != r->end(); ++it) {
        std::error_code ec;
        if (it->is_string() && it->get<std::string>() == want && fs::is_directory(it.key(), ec))
          return it.key();
      }
    return {};
  }

  void LinkLocalCopy(const std::string &path, const std::string &room, const WsTarget &t) {
    json j = ReadCollabJson();
    const char *map = LinkMap(t);
    const std::string value = LinkValue(t, room);
    if (!j.contains(map) || !j[map].is_object())
      j[map] = json::object();
    // One folder per workspace: linking a new one replaces the old link.
    for (auto it = j[map].begin(); it != j[map].end();)
      it = (it->is_string() && it->get<std::string>() == value && it.key() != path)
               ? j[map].erase(it)
               : std::next(it);
    j[map][path] = value;
    WriteCollabJson(j);
  }

  void RefreshRooms() {
    if (fetching)
      return;
    if (roomFetch.joinable())
      roomFetch.join();
    fetching = true;
    lastFetch = ImGui::GetTime();
    const std::string url = onlineUrl, token = onlineToken;
    roomFetch = std::thread([this, url, token] {
      auto list = ilmeee::net::FetchCollabRooms(url, token);
      {
        std::lock_guard<std::mutex> lock(roomMutex);
        roomList = std::move(list);
        roomListValid = true;
      }
      fetching = false;
    });
  }

  void OpenWorkspace(const std::string &projectPath, const std::string &room,
                     const WsTarget &t) {
    LinkLocalCopy(projectPath, room, t);
    AddOrUpdateProject(projectPath, "");
    const size_t idx = IndexOf(projectPath);
    const bool mcp = idx < projects.size() && projects[idx].mcpEnabled;
    hostProjectIdx = 0; // AddOrUpdateProject moved it to the top
    // The online server is the editor's saved default; a LAN server is
    // handed over for this session only.
    if (LaunchEditor(projectPath, mcp, room, t.lan ? t.url : std::string(),
                     t.lan ? t.token : std::string()) &&
        idx < projects.size()) {
      projects[idx].lastOpened = TodayIsoDate();
      SaveRecentProjects(projects);
    }
  }

  static ImVec4 Rgb(std::uint32_t c) {
    return ImVec4((float)(c >> 16 & 255) / 255.0f, (float)(c >> 8 & 255) / 255.0f,
                  (float)(c & 255) / 255.0f, 1.0f);
  }

  static std::string Ago(std::uint64_t whenUnix) {
    if (!whenUnix)
      return "never";
    const auto now = (std::uint64_t)std::time(nullptr);
    const std::uint64_t d = now > whenUnix ? now - whenUnix : 0;
    if (d < 60) return "just now";
    if (d < 3600) return std::to_string(d / 60) + " min ago";
    if (d < 86400) return std::to_string(d / 3600) + " h ago";
    return std::to_string(d / 86400) + " days ago";
  }

  static std::string HumanBytes(std::uint64_t b) {
    char buf[32];
    if (b < 1024)
      std::snprintf(buf, sizeof buf, "%llu B", (unsigned long long)b);
    else if (b < (1ull << 20))
      std::snprintf(buf, sizeof buf, "%.1f KB", (double)b / 1024.0);
    else if (b < (1ull << 30))
      std::snprintf(buf, sizeof buf, "%.1f MB", (double)b / (1024.0 * 1024.0));
    else
      std::snprintf(buf, sizeof buf, "%.2f GB", (double)b / (1024.0 * 1024.0 * 1024.0));
    return buf;
  }

  // True for folders the Hub downloaded (the workspace cache), as opposed to
  // a project the user made: only those are refreshed from the server on Join.
  bool IsDownloadedCopy(const std::string &path, const std::string &room, const WsTarget &t) {
    std::error_code ec;
    return fs::equivalent(fs::path(path), CacheDirFor(t, room), ec) && !ec;
  }

  // Runs PublishProject / DownloadProject on a worker; PollSyncJob opens the
  // editor when it is done (if openAfter).
  void StartSync(bool upload, const std::string &project, const std::string &room,
                 bool openAfter, const WsTarget &t) {
    if (syncJob.running)
      return;
    if (syncJob.thread.joinable())
      syncJob.thread.join();
    syncJob.upload = upload;
    syncJob.openAfter = openAfter;
    syncJob.room = room;
    syncJob.project = project;
    syncJob.target = t;
    syncJob.cancel = false;
    syncJob.finished = false;
    {
      std::lock_guard<std::mutex> lock(syncJob.mutex);
      syncJob.status = {};
      syncJob.status.phase = upload ? "Preparing upload" : "Connecting";
    }
    syncMessage.clear();
    syncJob.running = true;
    const std::string url = t.url, token = t.token, by = onlineName;
    logStore.Push("Hub", std::string(upload ? "Uploading " : "Downloading ") +
                             "project files for workspace '" + room + "' (" + t.label +
                             ")...");
    // Our own LAN server reads the files from the project folder itself:
    // sharing sends only the list, never a second copy on this disk.
    const bool linked = upload && t.lan && t.serverId == lanServerId && LanHosting();
    syncJob.thread = std::thread([this, upload, linked, url, token, by, room, project] {
      auto progress = [this](const ilmeee::net::SyncStatus &st) {
        std::lock_guard<std::mutex> lock(syncJob.mutex);
        syncJob.status = st;
      };
      const fs::path dir = ilmeee::net::PathFromUtf8(project);
      auto res = linked   ? ilmeee::net::PublishProjectLinked(url, token, room, dir, by,
                                                              progress, &syncJob.cancel)
                 : upload ? ilmeee::net::PublishProject(url, token, room, dir, by, progress,
                                                        &syncJob.cancel)
                          : ilmeee::net::DownloadProject(url, token, room, dir, progress,
                                                         &syncJob.cancel);
      {
        std::lock_guard<std::mutex> lock(syncJob.mutex);
        syncJob.result = std::move(res);
      }
      syncJob.finished = true;
      syncJob.running = false;
    });
  }

  void PollSyncJob() {
    if (!syncJob.finished.exchange(false))
      return;
    ilmeee::net::SyncResult r;
    {
      std::lock_guard<std::mutex> lock(syncJob.mutex);
      r = syncJob.result;
    }
    for (const auto &f : r.failed)
      logStore.Push("Hub", "  not downloaded: " + f);
    for (const auto &w : r.warnings)
      logStore.Push("Hub", "  warning: " + w);
    if (!r.ok) {
      syncMessage = std::string(syncJob.upload ? "Upload" : "Download") + " failed: " + r.error;
      syncMessageError = true;
      logStore.Push("Hub", syncMessage);
      // Some files failed but the rest are here: open anyway, the editor
      // reports each object whose asset is missing.
      if (!syncJob.upload && !r.failed.empty() && r.failed.size() < r.files &&
          syncJob.openAfter) {
        syncMessage += " - opening with the files that did arrive (see Logs)";
        OpenWorkspace(syncJob.project, syncJob.room, syncJob.target);
      }
      return;
    }
    char buf[256];
    if (syncJob.upload && syncJob.target.lan && syncJob.target.serverId == lanServerId)
      std::snprintf(buf, sizeof buf,
                    "Shared '%s' on LAN: %zu files (%s), served from the project folder "
                    "(no copy).",
                    syncJob.room.c_str(), r.files, HumanBytes(r.bytes).c_str());
    else if (syncJob.upload)
      std::snprintf(buf, sizeof buf, "Uploaded '%s': %zu files (%s), %zu new (%s).",
                    syncJob.room.c_str(), r.files, HumanBytes(r.bytes).c_str(), r.transferred,
                    HumanBytes(r.transferredBytes).c_str());
    else
      std::snprintf(buf, sizeof buf, "'%s' is up to date: %zu files (%s), %zu downloaded (%s)%s.",
                    syncJob.room.c_str(), r.files, HumanBytes(r.bytes).c_str(), r.transferred,
                    HumanBytes(r.transferredBytes).c_str(),
                    r.removed ? ", old files removed" : "");
    syncMessage = buf;
    if (!r.warnings.empty())
      syncMessage += " " + std::to_string(r.warnings.size()) +
                     " file(s) may not download on every device (e.g. Windows) - see Logs.";
    syncMessageError = false;
    logStore.Push("Hub", syncMessage);
    lastFetch = -1000.0; // show the new file info right away
    lastLanScan = -1000.0;
    if (syncJob.openAfter)
      OpenWorkspace(syncJob.project, syncJob.room, syncJob.target);
  }

  void DrawSyncProgress() {
    if (syncJob.running) {
      ilmeee::net::SyncStatus st;
      {
        std::lock_guard<std::mutex> lock(syncJob.mutex);
        st = syncJob.status;
      }
      const float frac =
          st.totalBytes ? (float)((double)st.doneBytes / (double)st.totalBytes)
          : st.totalFiles ? (float)st.doneFiles / (float)st.totalFiles
                          : 0.0f;
      char overlay[128];
      if (st.totalBytes)
        std::snprintf(overlay, sizeof overlay, "%s  %zu/%zu  %s / %s", st.phase.c_str(),
                      st.doneFiles, st.totalFiles, HumanBytes(st.doneBytes).c_str(),
                      HumanBytes(st.totalBytes).c_str());
      else
        std::snprintf(overlay, sizeof overlay, "%s...", st.phase.c_str());
      ImGui::ProgressBar(frac, ImVec2(-90, 0), overlay);
      ImGui::SameLine();
      if (ImGui::Button("Cancel##sync", ImVec2(80, 0)))
        syncJob.cancel = true;
      if (!st.file.empty())
        ImGui::TextDisabled("%s", st.file.c_str());
    } else if (!syncMessage.empty()) {
      if (syncMessageError)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", syncMessage.c_str());
      else
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.0f), "%s", syncMessage.c_str());
    }
  }

  // ---- LAN server hosted by this Hub ----------------------------------------

  // ilmeee-realtime next to the Hub (installed), else the cargo build in the
  // repo (development: build/bin -> server/realtime/target/...).
  static std::string FindRealtimeServerBinary() {
    const std::string sib = ilmeee::SiblingExecutable("ilmeee-realtime");
    std::error_code ec;
    if (fs::exists(sib, ec))
      return sib;
    const fs::path exe = ilmeee::ExecutableDir();
#ifdef _WIN32
    const char *name = "ilmeee-realtime.exe";
#else
    const char *name = "ilmeee-realtime";
#endif
    for (const char *profile : {"release", "debug"}) {
      const fs::path p = exe / ".." / ".." / "server" / "realtime" / "target" / profile / name;
      if (fs::exists(p, ec))
        return fs::weakly_canonical(p, ec).string();
    }
    return {};
  }

  bool LanHosting() const { return lanHost && lanHost->alive; }

  void StartLanServer() {
    if (LanHosting())
      return;
    StopLanServer();
    lanHostError.clear();
    const std::string bin = FindRealtimeServerBinary();
    if (bin.empty()) {
      lanHostError = "ilmeee-realtime was not found next to the Hub (or in "
                     "server/realtime/target: run `cargo build --release` there).";
      logStore.Push("Hub", lanHostError);
      return;
    }
    ilmeee::ProcessOptions o;
    o.program = bin;
    o.env = {{"HOST", "0.0.0.0"},
             {"PORT", std::to_string(ilmeee::net::kLanServerPort)},
             {"RT_DISCOVERY_PORT", std::to_string(ilmeee::net::kLanDiscoveryPort)},
             {"RT_SERVER_ID", lanServerId},
             {"RT_SERVER_NAME", std::string(onlineName)},
             {"ILMEEE_RT_TOKEN", std::string(lanPassword)},
             {"RT_DATA_DIR", ilmeee::net::PathToUtf8(ilmeee::UserDataRoot() / "lan-server")},
             // Shared projects are served from their own folders (linked
             // manifests), not copied into the server's data directory.
             {"RT_ALLOW_LINKED", "1"}};
    auto host = std::make_shared<LanHostProc>();
    host->proc = std::make_shared<ilmeee::Process>();
    std::string error;
    if (!host->proc->Start(o, error)) {
      lanHostError = "Could not start the LAN server: " + error;
      logStore.Push("Hub", lanHostError);
      return;
    }
    host->alive = true;
    LogStore *store = &logStore;
    host->reader = std::thread([host, store] {
      // Drain the pipe (a full pipe would block the server) into the log.
      std::string buf;
      char chunk[4096];
      long n;
      while ((n = host->proc->Read(chunk, sizeof chunk)) > 0) {
        buf.append(chunk, (size_t)n);
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
          std::string line = StripAnsi(buf.substr(0, pos));
          buf.erase(0, pos + 1);
          if (!line.empty() && line.back() == '\r')
            line.pop_back();
          if (!line.empty())
            store->Push("LAN server", line);
        }
      }
      int code = 0;
      host->proc->TryWait(code);
      host->exitCode = code;
      host->alive = false;
    });
    lanHost = host;
    logStore.Push("Hub", "LAN server started on port " +
                             std::to_string(ilmeee::net::kLanServerPort) + " (" +
                             (lanPassword[0] ? "password protected" : "open to this network") +
                             ")");
    // Announce on the online server so devices on another subnet of this
    // network find it too (only when an online token is configured).
    lanAnnounceStop = false;
    const std::string url = onlineUrl, token = onlineToken;
    ilmeee::net::LanAnnouncement ann{lanServerId, onlineName, ilmeee::net::kLanServerPort,
                                     lanPassword[0] != '\0', {}};
    lanAnnouncer = std::thread([this, url, token, ann]() mutable {
      if (token.empty() || url.empty()) {
        std::lock_guard<std::mutex> lock(lanMutex);
        lanAnnounceStatus = "not announced online (no online server token)";
        return;
      }
      std::unique_lock<std::mutex> lock(lanMutex);
      while (!lanAnnounceStop) {
        ann.addrs = ilmeee::net::LocalLanAddresses(); // the address may change
        lock.unlock();
        std::string err;
        const bool ok = !ann.addrs.empty() &&
                        ilmeee::net::AnnounceLan(url, token, ann, false, err);
        lock.lock();
        lanAnnounceStatus = ok ? "also announced on the online server (other subnets)"
                          : ann.addrs.empty() ? "no LAN address on this computer"
                                              : "online announce failed: " + err;
        lanCv.wait_for(lock, std::chrono::seconds(60), [this] { return lanAnnounceStop.load(); });
      }
      lock.unlock();
      std::string err;
      ilmeee::net::AnnounceLan(url, token, ann, true, err);
    });
    lastLanScan = ImGui::GetTime() - 8.0; // rescan shortly: show it in the list
  }

  void StopLanServer() {
    {
      std::lock_guard<std::mutex> lock(lanMutex);
      lanAnnounceStop = true;
    }
    lanCv.notify_all();
    if (lanAnnouncer.joinable())
      lanAnnouncer.join();
    if (lanHost) {
      if (lanHost->alive && lanHost->proc)
        lanHost->proc->Terminate();
      if (lanHost->reader.joinable())
        lanHost->reader.join();
      lanHost.reset();
      logStore.Push("Hub", "LAN server stopped.");
    }
  }

  // ---- LAN discovery --------------------------------------------------------

  void ScanLan() {
    if (lanScanning)
      return;
    if (lanScan.joinable())
      lanScan.join();
    lanScanning = true;
    lastLanScan = ImGui::GetTime();
    const std::string url = onlineToken[0] ? std::string(onlineUrl) : std::string();
    const std::string token = onlineToken, password = lanPassword;
    lanScan = std::thread([this, url, token, password] {
      auto found = ilmeee::net::DiscoverLanServers(url, token);
      std::vector<LanEntry> entries;
      for (auto &s : found.servers) {
        LanEntry e;
        e.server = s;
        e.rooms = ilmeee::net::FetchCollabRooms(s.WsUrl(), password, 4);
        entries.push_back(std::move(e));
      }
      {
        std::lock_guard<std::mutex> lock(lanMutex);
        lanFound = std::move(entries);
        lanRendezvousError = found.rendezvousError;
        lanScanned = true;
      }
      lanScanning = false;
    });
  }

  // ---- drawing --------------------------------------------------------------

  // "ONLINE" / "LAN" chip in front of a workspace name.
  static void Badge(bool lan) {
    const ImVec4 bg = lan ? ImVec4(0.16f, 0.42f, 0.28f, 1.0f) : ImVec4(0.18f, 0.33f, 0.55f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, bg);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 1));
    ImGui::SmallButton(lan ? "LAN" : "ONLINE");
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0, 8);
  }

  // Project picker + workspace name + the button that puts it on `t`.
  void DrawShareProject(const char *id, const WsTarget &t, bool enabled,
                        const char *disabledWhy) {
    ImGui::PushID(id);
    if (projects.empty()) {
      ImGui::TextDisabled("Add a project on the Projects page first.");
      ImGui::PopID();
      return;
    }
    hostProjectIdx = std::clamp(hostProjectIdx, 0, (int)projects.size() - 1);
    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo("##hostProject", projects[(size_t)hostProjectIdx].name.c_str())) {
      for (int i = 0; i < (int)projects.size(); ++i)
        if (ImGui::Selectable(projects[(size_t)i].name.c_str(), i == hostProjectIdx)) {
          hostProjectIdx = i;
          hostRoom[0] = '\0';
        }
      ImGui::EndCombo();
    }
    const ProjectEntry &hp = projects[(size_t)hostProjectIdx];
    if (hostRoom[0] == '\0') {
      const json j = ReadCollabJson();
      std::string room = SanitizeRoomName(fs::path(hp.path).filename().string());
      if (auto r = j.find("rooms"); r != j.end() && r->is_object())
        if (auto it = r->find(hp.path); it != r->end() && it->is_string())
          room = it->get<std::string>();
      std::snprintf(hostRoom, sizeof hostRoom, "%s", room.c_str());
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    ImGui::InputText("Workspace name", hostRoom, sizeof hostRoom,
                     ImGuiInputTextFlags_CharsNoBlank);
    ImGui::SameLine();
    const std::string room = SanitizeRoomName(hostRoom);
    ImGui::BeginDisabled(room.empty() || !enabled || syncJob.running);
    if (ImGui::Button(t.lan ? "Share on LAN" : "Go online")) {
      if (hostUploadFiles) {
        LinkLocalCopy(hp.path, room, t);
        StartSync(true, hp.path, room, /*openAfter=*/true, t);
      } else {
        OpenWorkspace(hp.path, room, t);
      }
      lastFetch = ImGui::GetTime() - 3.0; // refresh soon to show it
      lastLanScan = ImGui::GetTime() - 8.0;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("%s", !enabled ? disabledWhy
                                       : "Opens the project and joins the workspace. A new "
                                         "(empty) workspace takes this project's current scene.");
    ImGui::Checkbox("Upload the project files, so people without a copy can join",
                    &hostUploadFiles);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Only files the server does not have yet are sent. Skipped: "
                        "build/, .git/, compiled scripts, and anything listed in the "
                        "project's .ilmeeeignore (one folder/ or *.ext per line).");
    ImGui::PopID();
  }

  // One workspace card (online or LAN).
  void DrawRoomCard(const ilmeee::net::CollabRoomInfo &room, const WsTarget &t) {
    ImGui::PushID((t.url + "|" + room.name).c_str());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, t.lan ? ImVec4(0.115f, 0.145f, 0.13f, 1.0f)
                                                  : ImVec4(0.125f, 0.135f, 0.151f, 1.0f));
    ImGui::BeginChild("##ws", ImVec2(0, 112), true, ImGuiWindowFlags_NoScrollbar);
    Badge(t.lan);
    ImGui::TextUnformatted(room.name.c_str());
    ImGui::SameLine(0, 12);
    ImGui::TextDisabled("%s%zu objects  -  active %s",
                        t.lan ? ("on " + t.label + "  -  ").c_str() : "", room.objects,
                        Ago(room.updatedUnix).c_str());
    if (room.peers.empty()) {
      ImGui::TextDisabled("Nobody online");
    } else {
      for (size_t i = 0; i < room.peers.size(); ++i) {
        if (i)
          ImGui::SameLine(0, 10);
        ImGui::ColorButton("##pc", Rgb(room.peers[i].second), ImGuiColorEditFlags_NoTooltip,
                           ImVec2(10, 10));
        ImGui::SameLine(0, 4);
        ImGui::TextUnformatted(room.peers[i].first.c_str());
      }
    }
    if (room.hasFiles)
      ImGui::TextDisabled("Project files: %zu (%s), uploaded %s", room.fileCount,
                          HumanBytes(room.fileBytes).c_str(), Ago(room.filesUpdatedUnix).c_str());
    else
      ImGui::TextDisabled("No project files on the server: joining needs your own copy");
    const std::string local = LocalCopyOf(room.name, t);
    const bool downloaded = !local.empty() && IsDownloadedCopy(local, room.name, t);
    bool openHere = false;
    for (const auto &c : children)
      openHere |= c->alive && c->collabRoom == room.name &&
                  c->collabServer == (t.lan ? t.url : std::string());
    if (local.empty())
      ImGui::TextDisabled(room.hasFiles ? "Not on this computer yet: Join downloads it"
                                        : "No local copy linked yet");
    else
      ImGui::TextDisabled("%s: %s", downloaded ? "Downloaded copy" : "Your copy", local.c_str());

    const bool busy = syncJob.running;
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 132, 26));
    ImGui::BeginDisabled(openHere || busy);
    if (ImGui::Button(openHere ? "Open" : "Join", ImVec2(110, 30))) {
      if (!local.empty() && downloaded && room.hasFiles)
        StartSync(false, local, room.name, true, t); // fetch what changed, then open
      else if (!local.empty())
        OpenWorkspace(local, room.name, t);
      else if (room.hasFiles)
        StartSync(false, ilmeee::net::PathToUtf8(CacheDirFor(t, room.name)), room.name, true, t);
      else {
        pickForRoom = room.name;
        pickTarget = t;
      }
    }
    ImGui::EndDisabled();
    if (openHere && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("Already open in an editor started from this Hub");
    else if (!openHere && !busy && ImGui::IsItemHovered())
      ImGui::SetTooltip(local.empty() && room.hasFiles
                            ? "Downloads the project into this computer's workspace cache "
                              "and opens it"
                        : downloaded ? "Downloads what changed, then opens it"
                                     : "Opens your copy and joins the workspace");
    // Secondary actions, under the main button.
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 132, 62));
    ImGui::BeginDisabled(busy);
    if (!local.empty() && !downloaded) {
      if (ImGui::SmallButton("Upload files"))
        StartSync(true, local, room.name, false, t);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Send your copy's current files to the server, so people "
                          "who join get them (only changed files are sent)");
    } else if (local.empty() || downloaded) {
      if (ImGui::SmallButton("Use my own copy")) {
        pickForRoom = room.name;
        pickTarget = t;
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Link a project folder you already have instead of downloading");
    }
    ImGui::EndDisabled();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopID();
    ImGui::Dummy(ImVec2(0, 4));
  }

  void DrawOnlinePane() {
    if (!onlineLoaded)
      LoadOnlineSettings();
    ImGui::BeginChild("##OnlineScroll", ImVec2(0, 0), false);
    ImGui::TextWrapped(
        "Edit a scene together in realtime, through the online server or a server on "
        "this network (LAN). The scene is shared live; the project files (models, "
        "textures, audio, scripts) are uploaded once by whoever shares the project and "
        "downloaded by everyone who joins without a copy.");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(420);
    if (ImGui::InputText("Your name", onlineName, sizeof onlineName))
      SaveOnlineSettings(); // the editor reads the same file when it joins
    DrawSyncProgress();
    ImGui::Spacing();

    if (ImGui::BeginTabBar("##wsTabs")) {
      if (ImGui::BeginTabItem("Online server")) {
        DrawOnlineTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("On this network (LAN)")) {
        DrawLanTab();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    ImGui::EndChild();
    DrawPickLocalCopyPopup();
  }

  void DrawOnlineTab() {
    ImGui::SetNextItemWidth(420);
    bool changed = ImGui::InputText("Server", onlineUrl, sizeof onlineUrl);
    ImGui::SetNextItemWidth(420);
    changed |= ImGui::InputText("Token", onlineToken, sizeof onlineToken,
                                showOnlineToken ? 0 : ImGuiInputTextFlags_Password);
    ImGui::SameLine();
    ImGui::Checkbox("Show##onlineToken", &showOnlineToken);
    if (changed)
      SaveOnlineSettings();

    const bool canFetch = onlineToken[0] != '\0' && onlineUrl[0] != '\0';
    ImGui::BeginDisabled(!canFetch || fetching);
    if (ImGui::Button(fetching ? "Refreshing..." : "Refresh"))
      RefreshRooms();
    ImGui::EndDisabled();
    if (canFetch && !fetching && ImGui::GetTime() - lastFetch > 5.0)
      RefreshRooms(); // keep the list live while this page is open

    ilmeee::net::CollabRoomList list;
    bool valid;
    {
      std::lock_guard<std::mutex> lock(roomMutex);
      list = roomList;
      valid = roomListValid;
    }
    ImGui::SameLine();
    if (!canFetch)
      ImGui::TextDisabled("Enter the server token to see the workspaces.");
    else if (valid && !list.ok)
      ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", list.error.c_str());
    else if (valid)
      ImGui::TextDisabled("%zu workspace(s)", list.rooms.size());

    ImGui::Spacing();
    ImGui::SeparatorText("Put a project online");
    DrawShareProject("online", OnlineTarget(), canFetch,
                     "Enter the online server token first");

    ImGui::Spacing();
    ImGui::SeparatorText("Workspaces on the online server");
    if (valid && list.ok && list.rooms.empty())
      ImGui::TextDisabled("No workspaces yet. Put a project online above.");
    const WsTarget online = OnlineTarget();
    for (const auto &room : list.rooms)
      DrawRoomCard(room, online);
  }

  void DrawLanTab() {
    // ---- host ----
    ImGui::SeparatorText("Host a server on this computer");
    ImGui::TextWrapped(
        "Everyone on the same WiFi/LAN can join, without internet. Shared projects are "
        "served straight from their folders on this computer (no extra copy).");
    ImGui::SetNextItemWidth(260);
    ImGui::BeginDisabled(LanHosting());
    if (ImGui::InputText("LAN password (optional)", lanPassword, sizeof lanPassword,
                         ImGuiInputTextFlags_Password))
      SaveOnlineSettings();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("Empty = anyone on this network can join. Others type the same "
                        "password here to see and join its workspaces.");
    if (lanHost && !lanHost->alive) {
      // It stopped by itself: most likely the port is taken.
      lanHostError = "The LAN server stopped (exit " + std::to_string(lanHost->exitCode.load()) +
                     "). Is port " + std::to_string(ilmeee::net::kLanServerPort) +
                     " already used? See Logs.";
      StopLanServer();
    }
    if (!LanHosting()) {
      if (ImGui::Button("Host on LAN", ImVec2(150, 0)))
        StartLanServer();
      if (!lanHostError.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", lanHostError.c_str());
      }
    } else {
      if (ImGui::Button("Stop hosting", ImVec2(150, 0)))
        StopLanServer();
      ImGui::SameLine();
      std::string addrs;
      for (const auto &a : ilmeee::net::LocalLanAddresses())
        addrs += (addrs.empty() ? "" : ", ") + a + ":" +
                 std::to_string(ilmeee::net::kLanServerPort);
      ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.0f), "Hosting at %s",
                         addrs.empty() ? "(no LAN address)" : addrs.c_str());
      std::string status;
      {
        std::lock_guard<std::mutex> lock(lanMutex);
        status = lanAnnounceStatus;
      }
      if (!status.empty())
        ImGui::TextDisabled("%s", status.c_str());
      ImGui::TextDisabled("If others cannot find it: allow TCP %u and UDP %u in this "
                          "computer's firewall.",
                          (unsigned)ilmeee::net::kLanServerPort,
                          (unsigned)ilmeee::net::kLanDiscoveryPort);
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Share a project on this computer's LAN server");
    DrawShareProject("lan", OwnLanTarget(), LanHosting(), "Press 'Host on LAN' first");

    // ---- discovered ----
    ImGui::Spacing();
    ImGui::SeparatorText("Workspaces on this network");
    ImGui::BeginDisabled(lanScanning);
    if (ImGui::Button(lanScanning ? "Searching..." : "Search again"))
      ScanLan();
    ImGui::EndDisabled();
    if (!lanScanning && ImGui::GetTime() - lastLanScan > 10.0)
      ScanLan(); // keep it live while the tab is open

    std::vector<LanEntry> found;
    std::string rendezvousError;
    bool scanned;
    {
      std::lock_guard<std::mutex> lock(lanMutex);
      found = lanFound;
      rendezvousError = lanRendezvousError;
      scanned = lanScanned;
    }
    ImGui::SameLine();
    if (!scanned)
      ImGui::TextDisabled("Looking for servers on this network...");
    else
      ImGui::TextDisabled("%zu server(s) found", found.size());
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Found by broadcast on this subnet%s. Only servers this computer can "
                        "actually reach are listed.",
                        onlineToken[0] ? " and through the online server (other subnets of "
                                         "the same WiFi)"
                                       : "");
    if (!onlineToken[0])
      ImGui::TextDisabled("Tip: with an online server token set, hosts on another subnet of "
                          "this WiFi (e.g. 172.18.9.x vs 172.18.10.x) are found too.");
    else if (!rendezvousError.empty())
      ImGui::TextDisabled("Online lookup unavailable (%s): only this subnet is searched.",
                          rendezvousError.c_str());

    if (scanned && found.empty())
      ImGui::TextDisabled("No LAN server found. Someone has to press 'Host on LAN' (or the "
                          "network blocks device-to-device traffic: use the online server).");
    for (const auto &e : found) {
      const WsTarget t = LanTargetFor(e.server);
      ImGui::Spacing();
      ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.65f, 1.0f), "%s",
                         (e.server.name.empty() ? e.server.id : e.server.name).c_str());
      ImGui::SameLine();
      ImGui::TextDisabled("%s:%u%s%s", e.server.host.c_str(), (unsigned)e.server.port,
                          e.server.id == lanServerId ? "  (this computer)" : "",
                          e.server.viaRendezvous && !e.server.viaBroadcast
                              ? "  (other subnet, via online server)"
                              : "");
      if (!e.rooms.ok) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "  %s",
                           e.rooms.status == 401
                               ? "Password protected: type its password in 'LAN password' above"
                               : e.rooms.error.c_str());
        continue;
      }
      if (e.rooms.rooms.empty())
        ImGui::TextDisabled("  No workspaces on it yet.");
      for (const auto &room : e.rooms.rooms)
        DrawRoomCard(room, t);
    }
  }

  // "Which folder is your copy of <workspace>?"
  void DrawPickLocalCopyPopup() {
    if (!pickForRoom.empty() && !ImGui::IsPopupOpen("Choose your copy"))
      ImGui::OpenPopup("Choose your copy");
    ImGui::SetNextWindowSize(ImVec2(520, 0));
    if (!ImGui::BeginPopupModal("Choose your copy", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
      return;
    ImGui::TextWrapped("Which folder on this computer is your copy of the project for "
                       "workspace '%s' (%s)? Its scene will be replaced by the workspace's.",
                       pickForRoom.c_str(), pickTarget.label.c_str());
    ImGui::Spacing();
    std::string chosen;
    for (const auto &p : projects)
      if (ImGui::Selectable((p.name + "  (" + p.path + ")").c_str()))
        chosen = p.path;
    ImGui::Spacing();
    if (ImGui::Button("Browse folder...")) {
      nfdchar_t *outPath = nullptr;
      if (NFD::PickFolder(outPath, nullptr) == NFD_OKAY && outPath) {
        chosen = outPath;
        NFD::FreePath(outPath);
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      pickForRoom.clear();
      ImGui::CloseCurrentPopup();
    }
    if (!chosen.empty()) {
      OpenWorkspace(chosen, pickForRoom, pickTarget);
      pickForRoom.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  void DrawTemplatesPlaceholder() {
    ImGui::TextDisabled("Templates");
    ImGui::Separator();
    ImGui::TextWrapped("Project templates land here (2D VN, 3D, sandbox). "
                       "For now use '+ New Project' to scaffold an empty "
                       "project structure.");
  }

  void DrawLearnPlaceholder() {
    ImGui::TextDisabled("Learn");
    ImGui::Separator();
    ImGui::TextWrapped("Docs and tutorials will be linked from this section.");
  }

  void DrawSettingsPane() {
    ImGui::TextDisabled("Settings");
    ImGui::Separator();
    ImGui::Text("Hub config: %s", HubConfigPath().c_str());
    ImGui::Text("Editor binary: %s", ResolveEditorBinary().c_str());
    ImGui::Spacing();
    if (ImGui::Button("Reload Project List")) {
      projects = LoadRecentProjects();
    }

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::TextDisabled("MCP");
    ImGui::Separator();
    ImGui::TextWrapped("Used by projects with \"Use MCP\" on, applied the next "
                       "time the editor is opened. An empty token makes the "
                       "editor generate a new one every launch.");
    ImGui::Spacing();
    bool changed = false;
    ImGui::SetNextItemWidth(420);
    changed |= ImGui::InputText("Token", mcpTokenBuf, sizeof(mcpTokenBuf),
                                showMcpToken ? 0 : ImGuiInputTextFlags_Password);
    ImGui::SameLine();
    ImGui::Checkbox("Show", &showMcpToken);
    if (ImGui::Button("Generate")) {
      std::snprintf(mcpTokenBuf, sizeof(mcpTokenBuf), "%s",
                    GenerateMcpToken().c_str());
      changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy token") && mcpTokenBuf[0])
      ImGui::SetClipboardText(mcpTokenBuf);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
      mcpTokenBuf[0] = '\0';
      changed = true;
    }
    ImGui::Spacing();
    ImGui::SetNextItemWidth(420);
    changed |= ImGui::InputTextWithHint("Public host", "mcp.example.com",
                                        mcpHostBuf, sizeof(mcpHostBuf));
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Tunnel / reverse-proxy hostname. Empty = local only.");
    if (changed) {
      mcpSettings.token = Trim(mcpTokenBuf);
      mcpSettings.publicHost = NormalizeHost(mcpHostBuf);
      SaveMcpSettings(mcpSettings);
    }
    if (!mcpSettings.publicHost.empty()) {
      ImGui::Spacing();
      if (mcpSettings.token.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                           "Set a token so the connector URL stays the same.");
      } else {
        ImGui::TextDisabled("Connector URL: https://%s/mcp/<token>",
                            mcpSettings.publicHost.c_str());
        if (ImGui::Button("Copy connector URL"))
          ImGui::SetClipboardText(ConnectorUrl(mcpSettings.token).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Anyone with this URL can edit your scene.");
      }
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Saved to %s", HubConfigPath().c_str());
  }

  std::string ConnectorUrl(const std::string &token) const {
    return "https://" + mcpSettings.publicHost + "/mcp/" + token;
  }

  static std::string Trim(const std::string &text) {
    const size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
      return {};
    return text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
  }

  // Accept a pasted URL ("https://host/...") and keep only the hostname.
  static std::string NormalizeHost(const std::string &text) {
    std::string host = Trim(text);
    if (const size_t scheme = host.find("://"); scheme != std::string::npos)
      host.erase(0, scheme + 3);
    host = host.substr(0, host.find('/'));
    std::transform(host.begin(), host.end(), host.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return host;
  }

  // ---------------- Project actions ----------------
  void OpenExistingProject() {
    nfdchar_t *outPath = nullptr;
    nfdresult_t r = NFD::PickFolder(outPath, nullptr);
    if (r != NFD_OKAY || !outPath)
      return;
    std::string chosen = outPath;
    NFD::FreePath(outPath);
    AddOrUpdateProject(chosen, "");
    LaunchAndTouch(IndexOf(chosen));
  }

  void AddOrUpdateProject(const std::string &absPath,
                          const std::string &nameHint) {
    fs::path p(absPath);
    std::error_code ec;
    if (!fs::is_directory(p, ec)) {
      std::cerr << "Hub: not a directory: " << absPath << "\n";
      return;
    }

    // Move to top if already present, otherwise insert.
    auto it = std::find_if(
        projects.begin(), projects.end(),
        [&](const ProjectEntry &e) { return fs::path(e.path) == p; });
    if (it != projects.end()) {
      ProjectEntry existing = *it;
      projects.erase(it);
      existing.lastOpened = TodayIsoDate();
      ReadProjectMetadata(existing);
      projects.insert(projects.begin(), existing);
      SaveRecentProjects(projects);
      return;
    }

    ProjectEntry e;
    e.path = absPath;
    e.name = nameHint.empty() ? p.filename().string() : nameHint;
    e.engineVersion = "0.1";
    e.lastOpened = TodayIsoDate();
    ReadProjectMetadata(e);
    if (e.name.empty())
      e.name = p.filename().string();
    WriteProjectMetadata(e);
    projects.insert(projects.begin(), e);
    SaveRecentProjects(projects);
  }

  size_t IndexOf(const std::string &absPath) {
    fs::path p(absPath);
    for (size_t i = 0; i < projects.size(); ++i)
      if (fs::path(projects[i].path) == p)
        return i;
    return 0;
  }

  void LaunchAndTouch(size_t index) {
    if (index >= projects.size())
      return;
    if (!LaunchEditor(projects[index].path, projects[index].mcpEnabled))
      return;
    projects[index].lastOpened = TodayIsoDate();
    WriteProjectMetadata(projects[index]);
    SaveRecentProjects(projects);
  }

  // ---------------- New-project dialog ----------------
  void DrawNewProjectDialog() {
    ImGui::OpenPopup("New Project");
    ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("New Project", &showNewProjectDialog,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::InputTextWithHint("Name", "My Awesome Game", newProjName,
                               IM_ARRAYSIZE(newProjName));
      ImGui::InputText("Location", newProjLocation,
                       IM_ARRAYSIZE(newProjLocation));
      ImGui::SameLine();
      if (ImGui::Button("Browse")) {
        nfdchar_t *outPath = nullptr;
        if (NFD::PickFolder(outPath, nullptr) == NFD_OKAY && outPath) {
          std::snprintf(newProjLocation, sizeof(newProjLocation), "%s",
                        outPath);
          NFD::FreePath(outPath);
        }
      }

      if (!newProjError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::TextWrapped("%s", newProjError.c_str());
        ImGui::PopStyleColor();
      }

      ImGui::Separator();
      ImGui::TextDisabled("Hub will create: <Location>/<Name>/.ilmeee/, "
                          "<Location>/<Name>/assets/, scenes/, config/");

      ImGui::Spacing();
      if (ImGui::Button("Create", ImVec2(120, 0))) {
        std::string name = newProjName;
        std::string loc = newProjLocation;
        if (name.empty() || loc.empty()) {
          newProjError = "Name and Location are required.";
        } else {
          fs::path full = fs::path(loc) / name;
          std::error_code ec;
          fs::create_directories(full, ec);
          if (ec) {
            newProjError = "Failed to create directory: " + ec.message();
          } else {
            for (const char *sub :
                 {"assets", "assets/textures", "assets/audio", "assets/models",
                  "assets/scripts", "scenes", "config", ".ilmeee"}) {
              fs::create_directories(full / sub, ec);
            }
            AddOrUpdateProject(full.string(), name);
            showNewProjectDialog = false;
            ImGui::CloseCurrentPopup();
            LaunchAndTouch(IndexOf(full.string()));
          }
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        showNewProjectDialog = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }
};

// ---------------------------------------------------------------------------
// SDL bootstrap
// ---------------------------------------------------------------------------

class HubApplication {
public:
  bool Initialize() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
      std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
      return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    window = SDL_CreateWindow("Ilmeee Hub", 1100, 680,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    if (!window) {
      std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
      return false;
    }
    glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
      std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << "\n";
      return false;
    }
    SDL_GL_MakeCurrent(window, glContext);
    SDL_GL_SetSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
      std::cerr << "Failed to initialize GLAD\n";
      return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    static std::string imguiHubPath =
        (ilmeee::IlmeeeDir() / "imgui_hub.ini").string();
    io.IniFilename = imguiHubPath.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Load MiSansLatin as base font, merge zh-cn.ttf for CJK support
    const std::string latinFontPath = "assets/fonts/MiSansLatin-Regular.ttf";
    const std::string cjkFontPath = "assets/fonts/zh-cn.ttf";
    if (fs::exists(latinFontPath)) {
      ImFontConfig fontCfg;
      fontCfg.MergeMode = false;
      io.Fonts->AddFontFromFileTTF(latinFontPath.c_str(), 14.0f, &fontCfg);
      if (fs::exists(cjkFontPath)) {
        fontCfg.MergeMode = true;
        fontCfg.GlyphOffset.y = 1.0f;
        io.Fonts->AddFontFromFileTTF(
            cjkFontPath.c_str(), 14.0f, &fontCfg,
            io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
      }
    } else if (fs::exists(cjkFontPath)) {
      io.Fonts->AddFontFromFileTTF(cjkFontPath.c_str(), 14.0f);
    }

    ImGui_ImplSDL3_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 330");

    hub.SetupTheme();
    SetWindowIcon();
    return true;
  }

  void Run() {
    while (running) {
      HandleEvents();
      Render();
    }
  }

  ~HubApplication() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    if (glContext)
      SDL_GL_DestroyContext(glContext);
    if (window)
      SDL_DestroyWindow(window);
    SDL_Quit();
  }

private:
  SDL_Window *window = nullptr;
  SDL_GLContext glContext = nullptr;
  IlmeeeHub hub;
  bool running = true;

  void SetWindowIcon() {
    SDL_Surface *icon = SDL_LoadBMP("assets/icons/app_icon.bmp");
    if (icon) {
      SDL_SetWindowIcon(window, icon);
      SDL_DestroySurface(icon);
    }
  }

  void HandleEvents() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      ImGui_ImplSDL3_ProcessEvent(&ev);
      if (ev.type == SDL_EVENT_QUIT)
        running = false;
    }
  }

  void Render() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    hub.Render();

    ImGui::Render();
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.07f, 0.07f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window);
  }
};

int main(int argc, char *argv[]) {
  // Fonts and icons load from assets/ relative to the working directory.
  ilmeee::EnterEngineDataDir();
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--debug") == 0) {
      g_DebugMode = true;
    }
  }
  HubApplication app;
  if (!app.Initialize()) {
    std::cerr << "IlmeeeHub failed to initialize.\n";
    return 1;
  }
  app.Run();
  return 0;
}

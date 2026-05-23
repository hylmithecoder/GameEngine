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
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <core_engine/UserDataDir.hpp>
#include <json.hpp>
#include <nfd.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Data model
// ---------------------------------------------------------------------------

struct ProjectEntry {
  std::string name;
  std::string path;          // absolute path to project root
  std::string engineVersion; // "0.1" if unknown
  std::string lastOpened;    // ISO date, e.g. "2026-05-17"
  std::string lastScene;     // optional, last opened scene filename
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
    std::tm tm{};
    localtime_r(&t, &tm);
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
  pid_t pid = -1;
  std::string label;       // "Editor:<pid>"
  std::string projectPath; // for display
  int readFd = -1;
  std::thread reader;
  std::atomic<bool> alive{false};
};

// ---------------------------------------------------------------------------
// Filesystem / persistence helpers
// ---------------------------------------------------------------------------

namespace {

std::string TodayIsoDate() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_r(&t, &tm);
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
  std::string p = HubConfigPath();
  if (p.empty())
    return;
  EnsureParentDir(p);
  json j;
  j["projects"] = json::array();
  for (const auto &e : projects) {
    j["projects"].push_back({{"name", e.name},
                             {"path", e.path},
                             {"engineVersion", e.engineVersion},
                             {"lastOpened", e.lastOpened},
                             {"lastScene", e.lastScene}});
  }
  std::ofstream f(p);
  if (f.is_open())
    f << j.dump(2);
}

} // namespace

// ---------------------------------------------------------------------------
// Hub UI
// ---------------------------------------------------------------------------

class IlmeeeHub {
public:
  IlmeeeHub() {
    projects = LoadRecentProjects();
    NFD::Init();
    logStore.Push("Hub", "IlmeeeHub started.");
  }

  ~IlmeeeHub() {
    // Coupling editor lifetime to Hub keeps the log capture loop honest
    // — without termination the child's pipe would never EOF and the
    // reader thread would hang forever on shutdown.
    for (auto &c : children) {
      if (c->alive && c->pid > 0)
        kill(c->pid, SIGTERM);
    }
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
  bool LaunchEditor(const std::string &projectPath) {
    if (projectPath.empty()) {
      logStore.Push("Hub", "LaunchEditor: empty project path.");
      return false;
    }

    int fds[2];
    if (pipe(fds) != 0) {
      logStore.Push("Hub",
                    std::string("pipe() failed: ") + std::strerror(errno));
      return false;
    }
    // Set close-on-exec on the read end so the child doesn't inherit it.
    int flags = fcntl(fds[0], F_GETFD);
    if (flags != -1)
      fcntl(fds[0], F_SETFD, flags | FD_CLOEXEC);

    pid_t pid = fork();
    if (pid < 0) {
      close(fds[0]);
      close(fds[1]);
      logStore.Push("Hub",
                    std::string("fork() failed: ") + std::strerror(errno));
      return false;
    }

    if (pid == 0) {
      // Child: redirect stdout + stderr to the pipe write end.
      close(fds[0]);
      dup2(fds[1], STDOUT_FILENO);
      dup2(fds[1], STDERR_FILENO);
      close(fds[1]);
      // Force line buffering even though stdout is now a pipe; otherwise
      // glibc may switch to full buffering and the Hub console would
      // show nothing until the child terminates.
      setvbuf(stdout, nullptr, _IOLBF, 0);
      setvbuf(stderr, nullptr, _IOLBF, 0);
      execlp("./GameEngineSDL", "GameEngineSDL", "--project",
             projectPath.c_str(), (char *)nullptr);
      // exec failed:
      std::fprintf(stderr, "execlp failed: %s\n", std::strerror(errno));
      _exit(127);
    }

    // Parent
    close(fds[1]);

    auto child = std::make_shared<ChildProc>();
    child->pid = pid;
    child->label = "Editor:" + std::to_string(pid);
    child->projectPath = projectPath;
    child->readFd = fds[0];
    child->alive.store(true);

    LogStore *store = &logStore;
    int readFd = fds[0];
    std::string label = child->label;
    pid_t childPid = pid;

    child->reader = std::thread([readFd, label, store, childPid, child]() {
      std::string buf;
      char chunk[4096];
      while (true) {
        ssize_t n = ::read(readFd, chunk, sizeof(chunk));
        if (n > 0) {
          buf.append(chunk, (size_t)n);
          size_t pos;
          while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            store->Push(label, StripAnsi(line));
          }
        } else if (n == 0) {
          break; // EOF — child closed its stdout/stderr
        } else if (errno == EINTR) {
          continue;
        } else {
          break;
        }
      }
      if (!buf.empty())
        store->Push(label, StripAnsi(buf));
      ::close(readFd);

      int status = 0;
      ::waitpid(childPid, &status, 0);
      child->alive.store(false);
      std::string exitMsg =
          "Editor pid=" + std::to_string(childPid) + " exited";
      if (WIFEXITED(status)) {
        exitMsg += " (code " + std::to_string(WEXITSTATUS(status)) + ")";
      } else if (WIFSIGNALED(status)) {
        exitMsg += " (signal " + std::to_string(WTERMSIG(status)) + ")";
      }
      store->Push("Hub", exitMsg);
    });

    children.push_back(child);
    logStore.Push("Hub", "Launched editor pid=" + std::to_string(pid) +
                             " project=" + projectPath);
    return true;
  }

  void SetupTheme() {
    ImGuiStyle &s = ImGui::GetStyle();
    ImVec4 *c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.07f, 0.08f, 1.00f);
    c[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    c[ImGuiCol_PopupBg] = ImVec4(0.13f, 0.13f, 0.15f, 0.98f);
    c[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.25f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.34f, 0.50f, 1.00f);
    c[ImGuiCol_Button] = ImVec4(0.20f, 0.28f, 0.45f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.27f, 0.38f, 0.60f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.32f, 0.46f, 0.72f, 1.00f);
    c[ImGuiCol_Header] = ImVec4(0.18f, 0.22f, 0.30f, 1.00f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.30f, 0.40f, 1.00f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.40f, 0.55f, 1.00f);
    c[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.92f, 1.00f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.55f, 0.58f, 1.00f);
    c[ImGuiCol_Border] = ImVec4(0.25f, 0.25f, 0.30f, 0.50f);
    c[ImGuiCol_Separator] = ImVec4(0.30f, 0.30f, 0.35f, 0.5f);
    s.WindowRounding = 6.0f;
    s.ChildRounding = 6.0f;
    s.FrameRounding = 4.0f;
    s.PopupRounding = 6.0f;
    s.GrabRounding = 4.0f;
    s.WindowPadding = ImVec2(12, 12);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(8, 8);
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
  enum class Section { Projects, Templates, Learn, Settings };

  std::vector<ProjectEntry> projects;
  Section currentSection = Section::Projects;
  char searchBuffer[128] = "";
  bool showNewProjectDialog = false;
  char newProjName[128] = "";
  char newProjLocation[512] = "";
  std::string newProjError;

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
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.05f, 0.07f, 1.0f));
    ImGui::BeginChild("##Logo", ImVec2(0, 90), true);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.65f, 0.95f, 1.0f));
    ImGui::SetCursorPos(ImVec2(16, 22));
    ImGui::Text("ILMEEE");
    ImGui::PopStyleColor();
    ImGui::SetCursorPos(ImVec2(16, 44));
    ImGui::TextDisabled("Hub  ·  v0.1");
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Spacing();
    SidebarButton("Projects", Section::Projects);
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
    if (active)
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.35f, 0.55f, 1.0f));
    else
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    if (ImGui::Button(label, ImVec2(-1, 0)))
      currentSection = sec;
    ImGui::PopStyleColor();
  }

  // ---------------- Main pane ----------------
  void DrawMainPane() {
    ImGui::BeginChild("##Main", ImVec2(0, 0), false);

    DrawTopBar();
    ImGui::Separator();
    ImGui::Spacing();

    switch (currentSection) {
    case Section::Projects:
      DrawProjectList();
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
    ImGui::BeginChild("##Topbar", ImVec2(0, 56), false);
    ImGui::Spacing();
    ImGui::SameLine(0, 8);
    ImGui::PushItemWidth(320);
    ImGui::InputTextWithHint("##Search", "Search projects...", searchBuffer,
                             IM_ARRAYSIZE(searchBuffer));
    ImGui::PopItemWidth();

    ImGui::SameLine();
    if (ImGui::Button("Open Folder")) {
      OpenExistingProject();
    }
    ImGui::SameLine();
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

    ImGui::BeginChild("##ProjectScroll", ImVec2(0, 0), false);
    for (size_t i = 0; i < projects.size(); ++i) {
      const auto &p = projects[i];
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

  void DrawProjectCard(const ProjectEntry &p, size_t index) {
    ImGui::PushID((int)index);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.14f, 0.14f, 0.16f, 1.0f));
    ImGui::BeginChild("##Card", ImVec2(0, 82), true);

    // Project name (big)
    ImGui::Text("%s", p.name.empty() ? "(unnamed)" : p.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(
        "  v%s", p.engineVersion.empty() ? "0.1" : p.engineVersion.c_str());
    if (!p.lastOpened.empty()) {
      ImGui::SameLine();
      ImGui::TextDisabled("  ·  last opened %s", p.lastOpened.c_str());
    }

    ImGui::TextDisabled("%s", p.path.c_str());

    // Action row aligned to right
    float btnW = 90.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - btnW * 2 - 24);
    if (ImGui::Button("Open", ImVec2(btnW, 0))) {
      LaunchAndTouch(index);
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove", ImVec2(btnW, 0))) {
      projects.erase(projects.begin() + index);
      SaveRecentProjects(projects);
      ImGui::EndChild();
      ImGui::PopStyleColor();
      ImGui::PopID();
      return;
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopID();
    ImGui::Spacing();
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
    ImGui::Text("Editor binary: ./GameEngineSDL (working dir)");
    ImGui::Spacing();
    if (ImGui::Button("Reload Project List")) {
      projects = LoadRecentProjects();
    }
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
    projects[index].lastOpened = TodayIsoDate();
    WriteProjectMetadata(projects[index]);
    SaveRecentProjects(projects);
    LaunchEditor(projects[index].path);
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
    // Try to load CJK font if present; otherwise fall back to default.
    const std::string fontPath = "assets/fonts/zh-cn.ttf";
    if (fs::exists(fontPath))
      io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 14.0f);

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

int main(int, char *[]) {
  HubApplication app;
  if (!app.Initialize()) {
    std::cerr << "IlmeeeHub failed to initialize.\n";
    return 1;
  }
  app.Run();
  return 0;
}

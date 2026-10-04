// Build menu and the Console's Build tab: turn the open project into a
// runnable game (ilmeee::BuildGame) and start it.

#include "../../../include/core_engine/UserDataDir.hpp"
#include "../../../include/ui/MainWindow.hpp"
#include <cstring>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

using namespace ImGui;
namespace fs = std::filesystem;

namespace {

// scripts/bundle-deps.sh: next to the repo when running from build/bin, or
// under share/ when the engine is installed.
fs::path FindBundleScript() {
  const fs::path exe = ilmeee::ExecutableDir();
  for (const fs::path &candidate :
       {exe / ".." / ".." / "scripts" / "bundle-deps.sh",
        exe / ".." / "share" / "ilmeeeengine" / "scripts" / "bundle-deps.sh"}) {
    std::error_code ec;
    if (fs::is_regular_file(candidate, ec))
      return fs::weakly_canonical(candidate, ec);
  }
  return {};
}

// Where BuildGame puts this project's game by default.
fs::path DefaultBuildDir(const std::string &project) {
  const std::string safe =
      ilmeee::SafeGameName(fs::path(project).filename().string());
  return fs::path(project) / "build" / "linux" / safe;
}

fs::path DefaultLauncher(const std::string &project) {
  return DefaultBuildDir(project) /
         ilmeee::SafeGameName(fs::path(project).filename().string());
}

bool Spawn(const std::string &program, const std::vector<std::string> &args,
           pid_t &pid, bool searchPath) {
  std::vector<std::string> storage;
  storage.push_back(program);
  storage.insert(storage.end(), args.begin(), args.end());
  std::vector<char *> argv;
  for (auto &a : storage)
    argv.push_back(a.data());
  argv.push_back(nullptr);
  int rc = searchPath
               ? posix_spawnp(&pid, program.c_str(), nullptr, nullptr,
                              argv.data(), environ)
               : posix_spawn(&pid, program.c_str(), nullptr, nullptr,
                             argv.data(), environ);
  errno = rc;
  return rc == 0;
}

} // namespace

void MainWindow::PushBuildLog(const std::string &line) {
  {
    std::lock_guard<std::mutex> lock(gameBuildLogMutex);
    gameBuildLog.push_back(line);
    if (gameBuildLog.size() > 5000)
      gameBuildLog.erase(gameBuildLog.begin(), gameBuildLog.begin() + 1000);
    gameBuildScrollToEnd = true;
  }
  ::Log("[Build] " + line);
}

void MainWindow::StartGameBuild(bool runWhenDone) {
  if (gameBuildRunning)
    return;
  showConsole = true;
  focusBuildTab = true;

  const std::string project = projectHandler.projectPath;
  if (project.empty()) {
    PushBuildLog("Open a project before building.");
    return;
  }
  if (!builder.IsStopped()) {
    PushBuildLog("Stop the game in the editor before building.");
    return;
  }

  // The build reads scenes/main.ilmeeescene from disk: save what is on
  // screen first, so the game is what the editor shows.
  Save3DScene();

  if (gameBuildThread.joinable())
    gameBuildThread.join();
  {
    std::lock_guard<std::mutex> lock(gameBuildLogMutex);
    gameBuildLog.clear();
  }

  ilmeee::GameBuildOptions options;
  options.projectRoot = project;
  options.playerExecutable = ilmeee::SiblingExecutable("IlmeeePlayer");
  // The very shaders this editor renders with.
  options.engineShaderDir = fs::absolute("assets/shaders/vulkan");
  if (gameBuildPortable) {
    options.bundleScript = FindBundleScript();
    if (options.bundleScript.empty())
      PushBuildLog("bundle-deps.sh not found; building for this machine "
                   "only.");
  }

  gameBuildRunAfter = runWhenDone;
  gameBuildFinished = false;
  gameBuildRunning = true;
  gameBuildThread = std::thread([this, options] {
    ilmeee::GameBuildResult result = ilmeee::BuildGame(
        options, [this](const std::string &line) { PushBuildLog(line); });
    gameBuildResult = std::move(result);
    gameBuildRunning = false;
    gameBuildFinished = true; // published last: the UI reads the result now
  });
}

void MainWindow::LaunchBuiltGame() {
  if (lastGameLauncher.empty() && !projectHandler.projectPath.empty())
    lastGameLauncher = DefaultLauncher(projectHandler.projectPath);
  std::error_code ec;
  if (lastGameLauncher.empty() || !fs::exists(lastGameLauncher, ec)) {
    PushBuildLog("There is no build to run yet.");
    return;
  }
  if (gameProcess > 0) {
    PushBuildLog("The game is already running.");
    return;
  }
  pid_t pid = -1;
  if (!Spawn(lastGameLauncher.string(), {}, pid, false)) {
    PushBuildLog("Could not start " + lastGameLauncher.string() + ": " +
                 std::strerror(errno));
    return;
  }
  gameProcess = pid;
  PushBuildLog("Started " + lastGameLauncher.filename().string() + " (pid " +
               std::to_string(pid) + ")");
}

void MainWindow::PollGameBuild() {
  if (gameBuildFinished.exchange(false)) {
    if (gameBuildThread.joinable())
      gameBuildThread.join();
    if (gameBuildResult.ok) {
      lastGameLauncher = gameBuildResult.launcher;
      projectHandler.ShowNotification("Build Complete",
                                      gameBuildResult.outputDir.string(),
                                      ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
      if (gameBuildRunAfter)
        LaunchBuiltGame();
    } else {
      projectHandler.ShowNotification("Build Failed", gameBuildResult.error,
                                      ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    }
  }

  for (auto it = gameProcessHelpers.begin(); it != gameProcessHelpers.end();)
    it = waitpid(*it, nullptr, WNOHANG) == *it ? gameProcessHelpers.erase(it)
                                                : it + 1;

  if (gameProcess > 0) {
    int status = 0;
    if (waitpid(gameProcess, &status, WNOHANG) == gameProcess) {
      if (WIFEXITED(status))
        PushBuildLog("Game exited with code " +
                     std::to_string(WEXITSTATUS(status)));
      else if (WIFSIGNALED(status))
        PushBuildLog("Game was killed by signal " +
                     std::to_string(WTERMSIG(status)));
      gameProcess = -1;
    }
  }
}

void MainWindow::RenderBuildMenu() {
  if (!BeginMenu("Build"))
    return;
  const std::string &project = projectHandler.projectPath;
  const bool busy = gameBuildRunning;
  const bool hasProject = !project.empty();
  std::error_code ec;
  const fs::path launcher =
      !lastGameLauncher.empty()
          ? lastGameLauncher
          : (hasProject ? DefaultLauncher(project) : fs::path());
  const bool hasBuild = !launcher.empty() && fs::exists(launcher, ec);

  if (MenuItem("Build Game", "Ctrl+B", false, !busy && hasProject))
    StartGameBuild(false);
  if (MenuItem("Build and Run", "Ctrl+Shift+B", false, !busy && hasProject))
    StartGameBuild(true);
  if (MenuItem("Run Last Build", nullptr, false,
               !busy && hasBuild && gameProcess <= 0))
    LaunchBuiltGame();
  Separator();
  const bool canBundle = !FindBundleScript().empty();
  MenuItem("Portable (bundle libraries)", nullptr, &gameBuildPortable,
           canBundle && !busy);
  if (IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    SetTooltip(canBundle ? "Copy the libraries the game needs into it, so the "
                           "folder runs on other Linux machines too."
                         : "scripts/bundle-deps.sh was not found.");
  const fs::path folder = hasProject ? DefaultBuildDir(project) : fs::path();
  if (MenuItem("Open Build Folder", nullptr, false,
               hasProject && fs::exists(folder, ec))) {
    pid_t pid;
    if (Spawn("xdg-open", {folder.string()}, pid, true))
      gameProcessHelpers.push_back(pid);
  }
  if (!hasProject)
    TextDisabled("Open a project to build it");
  EndMenu();
}

void MainWindow::RenderBuildTab() {
  const bool busy = gameBuildRunning;
  const bool hasProject = !projectHandler.projectPath.empty();

  BeginDisabled(busy || !hasProject);
  if (Button("Build"))
    StartGameBuild(false);
  SameLine();
  if (Button("Build and Run"))
    StartGameBuild(true);
  EndDisabled();
  SameLine();
  BeginDisabled(busy || gameProcess > 0);
  if (Button("Run"))
    LaunchBuiltGame();
  EndDisabled();
  SameLine();
  Checkbox("Portable", &gameBuildPortable);
  SameLine();
  if (busy)
    TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "Building...");
  else if (gameProcess > 0)
    TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Game running");

  BeginChild("BuildLog", ImVec2(0, 0), true,
             ImGuiWindowFlags_HorizontalScrollbar);
  {
    std::lock_guard<std::mutex> lock(gameBuildLogMutex);
    if (gameBuildLog.empty())
      TextDisabled("Build > Build Game turns the open project into a "
                   "runnable game under build/linux/.");
    for (const std::string &line : gameBuildLog) {
      bool bad = line.rfind("Build failed", 0) == 0 ||
                 line.find("  ! ") == 0 || line.rfind("Warning:", 0) == 0;
      if (bad)
        TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", line.c_str());
      else
        TextUnformatted(line.c_str());
    }
    if (gameBuildScrollToEnd) {
      SetScrollHereY(1.0f);
      gameBuildScrollToEnd = false;
    }
  }
  EndChild();
}

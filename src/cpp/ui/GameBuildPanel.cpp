// Build menu and the Console's Build tab: turn the open project into a
// runnable game (ilmeee::BuildGame) and start it.

#include "../../../include/core_engine/UserDataDir.hpp"
#include "../../../include/ui/MainWindow.hpp"
#include <cstring>
#include <cerrno>
#include <fcntl.h>
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

// A folder of the engine checkout (build/bin/..) or of an installed engine
// (share/ilmeeeengine/), whichever exists.
fs::path FindEngineDir(const fs::path &rel) {
  const fs::path exe = ilmeee::ExecutableDir();
  for (const fs::path &candidate :
       {exe / ".." / ".." / rel, exe / ".." / "share" / "ilmeeeengine" / rel}) {
    std::error_code ec;
    if (fs::exists(candidate, ec))
      return fs::weakly_canonical(candidate, ec);
  }
  return {};
}

// Where BuildGame puts this project's game by default.
fs::path DefaultBuildDir(const std::string &project,
                         ilmeee::BuildTarget target) {
  const std::string safe =
      ilmeee::SafeGameName(fs::path(project).filename().string());
  return fs::path(project) / "build" /
         (target == ilmeee::BuildTarget::Android ? "android" : "linux") / safe;
}

fs::path DefaultLauncher(const std::string &project,
                         ilmeee::BuildTarget target) {
  const std::string safe =
      ilmeee::SafeGameName(fs::path(project).filename().string());
  return DefaultBuildDir(project, target) /
         (target == ilmeee::BuildTarget::Android ? safe + ".apk" : safe);
}

// Installs the APK on the connected device, starts it and keeps printing the
// game's logcat (its stdout/stderr) until the device goes away.
const char *kAndroidRunScript = R"sh(
apk="$1"; app="$2"; activity="$3"
ADB=adb
command -v adb >/dev/null 2>&1 || ADB="${ANDROID_HOME:-$HOME/Android/Sdk}/platform-tools/adb"
if [ "$("$ADB" get-state 2>/dev/null)" != "device" ]; then
  echo "No Android device found. Connect a phone with USB debugging enabled (check: adb devices)."
  exit 1
fi
echo "Installing $(basename "$apk") on $("$ADB" shell getprop ro.product.model | tr -d '') ..."
"$ADB" install -r "$apk" || exit 1
"$ADB" logcat -c
"$ADB" shell am start -n "$app/$activity" || exit 1
exec "$ADB" logcat -v brief -s Ilmeee:V SDL:V SDL/APP:V AndroidRuntime:E DEBUG:E
)sh";

bool Spawn(const std::string &program, const std::vector<std::string> &args,
           pid_t &pid, bool searchPath, int captureWrite = -1,
           int captureRead = -1) {
  std::vector<std::string> storage;
  storage.push_back(program);
  storage.insert(storage.end(), args.begin(), args.end());
  std::vector<char *> argv;
  for (auto &a : storage)
    argv.push_back(a.data());
  argv.push_back(nullptr);
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_t *actionPtr = nullptr;
  if (captureWrite >= 0) {
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, captureWrite, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, captureWrite, STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, captureWrite);
    if (captureRead >= 0)
      posix_spawn_file_actions_addclose(&actions, captureRead);
    actionPtr = &actions;
  }
  int rc = searchPath
               ? posix_spawnp(&pid, program.c_str(), actionPtr, nullptr,
                              argv.data(), environ)
               : posix_spawn(&pid, program.c_str(), actionPtr, nullptr,
                             argv.data(), environ);
  if (actionPtr) posix_spawn_file_actions_destroy(&actions);
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
  options.target = gameBuildTarget;
  options.projectRoot = project;
  if (gameBuildTarget == ilmeee::BuildTarget::Android) {
    options.androidPlayerDir = FindEngineDir("build-android");
    options.androidTemplateDir = FindEngineDir(fs::path("android") / "template");
    if (options.androidPlayerDir.empty())
      PushBuildLog("Android player not built yet: run "
                   "scripts/android/build-player.sh in the engine folder.");
  }
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
  showConsole = true;
  focusBuildTab = true;
  if (lastGameLauncher.empty() && !projectHandler.projectPath.empty()) {
    lastGameTarget = gameBuildTarget;
    lastGameLauncher = DefaultLauncher(projectHandler.projectPath,
                                       gameBuildTarget);
    lastGameApplicationId = ilmeee::DefaultApplicationId(
        fs::path(projectHandler.projectPath).filename().string());
  }
  std::error_code ec;
  if (lastGameLauncher.empty() || !fs::exists(lastGameLauncher, ec)) {
    PushBuildLog("There is no build to run yet.");
    return;
  }
  if (gameProcess > 0) {
    PushBuildLog("The game is already running.");
    return;
  }
  if (gameOutputFd >= 0) {
    close(gameOutputFd);
    gameOutputFd = -1;
    gameOutputPending.clear();
  }
  pid_t pid = -1;
  int pipeFds[2] = {-1, -1};
  if (pipe2(pipeFds, O_CLOEXEC) != 0) {
    PushBuildLog("Could not create game log pipe: " +
                 std::string(std::strerror(errno)));
    return;
  }
  const int readFlags = fcntl(pipeFds[0], F_GETFL);
  if (readFlags < 0 || fcntl(pipeFds[0], F_SETFL,
                            readFlags | O_NONBLOCK) < 0) {
    close(pipeFds[0]);
    close(pipeFds[1]);
    PushBuildLog("Could not configure game log pipe");
    return;
  }
  const bool android = lastGameTarget == ilmeee::BuildTarget::Android;
  const bool started =
      android ? Spawn("bash",
                      {"-c", kAndroidRunScript, "ilmeee-android-run",
                       lastGameLauncher.string(), lastGameApplicationId,
                       ilmeee::kAndroidActivity},
                      pid, true, pipeFds[1], pipeFds[0])
              : Spawn(lastGameLauncher.string(), {}, pid, false, pipeFds[1],
                      pipeFds[0]);
  if (!started) {
    close(pipeFds[0]);
    close(pipeFds[1]);
    PushBuildLog("Could not start " + lastGameLauncher.string() + ": " +
                 std::strerror(errno));
    return;
  }
  close(pipeFds[1]);
  gameOutputFd = pipeFds[0];
  gameOutputPending.clear();
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
      lastGameTarget = gameBuildTarget;
      lastGameApplicationId = gameBuildResult.applicationId;
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

  if (gameOutputFd >= 0) {
    char buffer[4096];
    for (int chunks = 0; chunks < 8; ++chunks) {
      const ssize_t n = read(gameOutputFd, buffer, sizeof(buffer));
      if (n > 0) {
        gameOutputPending.append(buffer, (size_t)n);
        size_t newline;
        while ((newline = gameOutputPending.find('\n')) != std::string::npos) {
          std::string line = gameOutputPending.substr(0, newline);
          if (!line.empty() && line.back() == '\r') line.pop_back();
          PushBuildLog("[GAME] " + line);
          PushMessage("[INFO] [GAME] " + line);
          gameOutputPending.erase(0, newline + 1);
        }
        if (gameOutputPending.size() > 16384) {
          PushBuildLog("[GAME] " + gameOutputPending.substr(0, 16384));
          gameOutputPending.clear();
        }
      } else if (n == 0) {
        if (!gameOutputPending.empty()) {
          PushBuildLog("[GAME] " + gameOutputPending);
          PushMessage("[INFO] [GAME] " + gameOutputPending);
          gameOutputPending.clear();
        }
        close(gameOutputFd);
        gameOutputFd = -1;
        break;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      } else {
        PushBuildLog("Game log read failed: " + std::string(std::strerror(errno)));
        close(gameOutputFd);
        gameOutputFd = -1;
        break;
      }
    }
  }

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
      !lastGameLauncher.empty() && lastGameTarget == gameBuildTarget
          ? lastGameLauncher
          : (hasProject ? DefaultLauncher(project, gameBuildTarget)
                        : fs::path());
  const bool hasBuild = !launcher.empty() && fs::exists(launcher, ec);

  if (MenuItem("Build Game", "Ctrl+B", false, !busy && hasProject))
    StartGameBuild(false);
  if (MenuItem("Build and Run", "Ctrl+Shift+B", false, !busy && hasProject))
    StartGameBuild(true);
  if (MenuItem("Run Last Build", nullptr, false,
               !busy && hasBuild && gameProcess <= 0))
    LaunchBuiltGame();
  Separator();
  if (MenuItem("Target: Linux", nullptr,
               gameBuildTarget == ilmeee::BuildTarget::Linux, !busy))
    gameBuildTarget = ilmeee::BuildTarget::Linux;
  if (MenuItem("Target: Android (APK)", nullptr,
               gameBuildTarget == ilmeee::BuildTarget::Android, !busy))
    gameBuildTarget = ilmeee::BuildTarget::Android;
  if (IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    SetTooltip("Builds an APK; Run installs it on a USB-connected phone "
               "(USB debugging on) and shows its log here.");
  Separator();
  const bool canBundle = !FindBundleScript().empty() &&
                         gameBuildTarget == ilmeee::BuildTarget::Linux;
  MenuItem("Portable (bundle libraries)", nullptr, &gameBuildPortable,
           canBundle && !busy);
  if (IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    SetTooltip(canBundle ? "Copy the libraries the game needs into it, so the "
                           "folder runs on other Linux machines too."
                         : "scripts/bundle-deps.sh was not found.");
  const fs::path folder =
      hasProject ? DefaultBuildDir(project, gameBuildTarget) : fs::path();
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
  if (gameProcess > 0 && lastGameTarget == ilmeee::BuildTarget::Android) {
    SameLine();
    if (Button("Stop Log"))
      kill(gameProcess, SIGTERM); // the app keeps running on the phone
  }
  SameLine();
  BeginDisabled(busy);
  int target = (int)gameBuildTarget;
  SetNextItemWidth(110);
  if (Combo("##target", &target, "Linux\0Android\0"))
    gameBuildTarget = (ilmeee::BuildTarget)target;
  EndDisabled();
  if (gameBuildTarget == ilmeee::BuildTarget::Linux) {
    SameLine();
    Checkbox("Portable", &gameBuildPortable);
  }
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
      TextDisabled(gameBuildTarget == ilmeee::BuildTarget::Android
                       ? "Build makes an APK under build/android/; Run "
                         "installs it on a USB-connected phone."
                       : "Build > Build Game turns the open project into a "
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

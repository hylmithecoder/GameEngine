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

// Installs the APK on the connected device, starts it with live stats on
// (debug.ilmeee.stats) and prints every log line of the game's process,
// device info and battery/thermal state (ILMEEE_* lines, read by
// DeviceMonitor) until the game exits; then any crash report.
const char *kAndroidRunScript = R"sh(
apk="$1"; app="$2"; activity="$3"
ADB=adb
command -v adb >/dev/null 2>&1 || ADB="${ANDROID_HOME:-$HOME/Android/Sdk}/platform-tools/adb"
# Several phones attached: adb needs a serial (ANDROID_SERIAL wins).
if [ -z "$ANDROID_SERIAL" ]; then
  serials="$("$ADB" devices | awk 'NR > 1 && $2 == "device" {print $1}')"
  if [ "$(printf '%s\n' "$serials" | grep -c .)" -gt 1 ]; then
    export ANDROID_SERIAL="$(printf '%s\n' "$serials" | head -n 1)"
    echo "Several devices attached; using $ANDROID_SERIAL (set ANDROID_SERIAL to choose)."
  fi
fi
if [ "$("$ADB" get-state 2>/dev/null)" != "device" ]; then
  echo "No Android device found. Connect a phone with USB debugging enabled (check: adb devices)."
  exit 1
fi
prop() { "$ADB" shell getprop "$1" | tr -d '\r"\\'; }
soc="$(prop ro.soc.model)"; [ -n "$soc" ] || soc="$(prop ro.board.platform)"
echo "ILMEEE_DEVICE_INFO {\"manufacturer\":\"$(prop ro.product.manufacturer)\",\"model\":\"$(prop ro.product.model)\",\"android\":\"$(prop ro.build.version.release)\",\"sdk\":\"$(prop ro.build.version.sdk)\",\"soc\":\"$soc\",\"abi\":\"$(prop ro.product.cpu.abi)\",\"screen\":\"$("$ADB" shell wm size | tr -d '\r' | sed -n 's/.*: //p' | tail -n 1)\"}"
echo "Installing $(basename "$apk") on $(prop ro.product.model) ..."
"$ADB" install -r "$apk" || exit 1
"$ADB" logcat -c
"$ADB" shell setprop debug.ilmeee.stats 1
"$ADB" shell am force-stop "$app"
"$ADB" shell am start -n "$app/$activity" || exit 1
pid=""
for _ in $(seq 50); do
  pid="$("$ADB" shell pidof "$app" | tr -d '\r')"
  [ -n "$pid" ] && break
  sleep 0.1
done
if [ -z "$pid" ]; then
  echo "Could not find the game process; showing its tagged log only."
  exec "$ADB" logcat -v brief -s Ilmeee:V SDL:V SDL/APP:V AndroidRuntime:E DEBUG:E
fi
echo "Game process $pid: streaming its whole log (Info and above)."
"$ADB" logcat -v brief --pid="$pid" '*:I' Ilmeee:V SDL:V SDL/APP:V &
logcat=$!
trap 'kill $logcat 2>/dev/null' EXIT
trap 'exit 0' TERM INT
# Gone only after a few misses in a row: right after the fork pidof can
# briefly miss the process while it gets its name.
misses=0
while [ $misses -lt 3 ]; do
  case " $("$ADB" shell pidof "$app" 2>/dev/null | tr -d '\r') " in
    *" $pid "*) misses=0 ;;
    *) misses=$((misses + 1)); sleep 1; continue ;;
  esac
  "$ADB" shell 'dumpsys battery; dumpsys thermalservice 2>/dev/null | grep -m1 "Thermal Status"' | tr -d '\r' | awk -F': ' 'BEGIN {thermal=-1}
    /^  level:/ {level=$2} /^  temperature:/ {temp=$2} /^  AC powered:/ {ac=$2}
    /^  USB powered:/ {usb=$2} /Thermal Status/ {thermal=$2}
    END { printf "ILMEEE_BATTERY {\"level\":%d,\"tempC\":%.1f,\"charging\":%s,\"thermal\":%d}\n",
          level, temp / 10, (ac == "true" || usb == "true") ? "true" : "false", thermal }'
  sleep 2
done
kill $logcat 2>/dev/null
echo "Game process $pid ended."
"$ADB" logcat -d -v brief -s AndroidRuntime:E DEBUG:E libc:F
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

void MainWindow::HandleGameOutput(const std::string &line) {
  if (deviceMonitor.Feed(line, GetTime()))
    return;
  PushBuildLog("[GAME] " + line);
  // logcat -v brief: "E/Tag ( pid): message".
  const char *level = "[INFO]";
  if (line.size() > 2 && line[1] == '/') {
    if (line[0] == 'E' || line[0] == 'F')
      level = "[ERROR]";
    else if (line[0] == 'W')
      level = "[WARNING]";
  }
  PushMessage(std::string(level) + " [GAME] " + line);
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
  options.includeUnusedAssets = gameBuildIncludeUnused;
  options.androidMaxVideoHeight = gameBuildAndroidVideoHeight;
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
  deviceMonitor.Reset(android ? "Android" : "Linux");
  showDeviceMonitor = true;
  // Desktop games inherit it; on the phone the run script sets a property.
  setenv("ILMEEE_STATS", "1", 1);
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
          HandleGameOutput(line);
          gameOutputPending.erase(0, newline + 1);
        }
        if (gameOutputPending.size() > 16384) {
          PushBuildLog("[GAME] " + gameOutputPending.substr(0, 16384));
          gameOutputPending.clear();
        }
      } else if (n == 0) {
        if (!gameOutputPending.empty()) {
          HandleGameOutput(gameOutputPending);
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
      deviceMonitor.SetEnded();
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
  MenuItem("Include unused assets", nullptr, &gameBuildIncludeUnused, !busy);
  if (IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    SetTooltip("Off: the game gets only the files its scenes use (models with "
               "their textures, images, audio, video).\nOn: the whole "
               "assets/ folder, e.g. for files a script loads by name.");
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
  } else {
    SameLine();
    static const int kHeights[] = {720, 1080, 0};
    static const char *kLabels[] = {"Video 720p", "Video 1080p",
                                    "Video original"};
    int sel = gameBuildAndroidVideoHeight == 1080 ? 1
              : gameBuildAndroidVideoHeight == 0  ? 2
                                                  : 0;
    SetNextItemWidth(130);
    if (Combo("##PhoneVideo", &sel, kLabels, IM_ARRAYSIZE(kLabels)))
      gameBuildAndroidVideoHeight = kHeights[sel];
    if (IsItemHovered())
      SetTooltip("Videos taller than this, or not H.264, are re-encoded to "
                 "H.264 for the APK\n(cached, so only the first build is "
                 "slow). Your project files are not changed.");
  }
  SameLine();
  Checkbox("Include unused assets", &gameBuildIncludeUnused);
  if (IsItemHovered())
    SetTooltip("Off: only the files the scenes use. On: all of assets/.");
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

void MainWindow::RenderDeviceMonitorWindow() {
  if (!Begin("Device Monitor", &showDeviceMonitor)) {
    End();
    return;
  }
  const double now = GetTime();
  const ilmeee::DeviceMonitor &monitor = deviceMonitor;
  const auto &samples = monitor.Samples();
  const auto &device = monitor.Device();
  const auto &battery = monitor.Battery();
  const auto text = [](const nlohmann::json &j, const char *key) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null())
      return std::string();
    return it->is_string() ? it->get<std::string>() : it->dump();
  };

  if (monitor.Target().empty()) {
    TextWrapped("Build > Run (or Build and Run) a game to see its live FPS, "
                "frame times and memory here. Android: USB debugging on, "
                "target Android.");
    End();
    return;
  }
  const bool live = monitor.Live(now);
  const ImVec4 state = live                ? ImVec4(0.3f, 0.9f, 0.4f, 1.0f)
                       : monitor.Running() ? ImVec4(0.9f, 0.8f, 0.3f, 1.0f)
                                           : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
  TextColored(state, "%s",
              live                ? "LIVE"
              : monitor.Running() ? (samples.empty() ? "Waiting for the game..."
                                                     : "No stats (paused or "
                                                       "in background?)")
                                  : "Game ended");
  SameLine();
  TextDisabled("%s", monitor.Target().c_str());

  // Who: phone (from adb) and GPU (from the player).
  std::string who = text(device, "manufacturer") + " " + text(device, "model");
  if (!text(device, "android").empty())
    who += "  |  Android " + text(device, "android") + " (SDK " +
           text(device, "sdk") + ")";
  if (!text(device, "soc").empty())
    who += "  |  " + text(device, "soc");
  if (who.find_first_not_of(' ') != std::string::npos)
    TextWrapped("%s", who.c_str());
  if (!text(device, "gpu").empty())
    TextWrapped("GPU %s  |  Vulkan %s", text(device, "gpu").c_str(),
                text(device, "vulkan").c_str());
  if (!text(device, "width").empty())
    Text("Render %sx%s  |  %s objects%s", text(device, "width").c_str(),
         text(device, "height").c_str(), text(device, "objects").c_str(),
         text(device, "paused") == "true" ? "  |  paused" : "");
  if (!battery.empty()) {
    static const char *kThermal[] = {"none",   "light",     "moderate",
                                     "severe", "critical",  "emergency",
                                     "shutdown"};
    const int thermal = battery.value("thermal", -1);
    const double temp = battery.value("tempC", 0.0);
    TextColored(temp >= 42.0 || thermal >= 2 ? ImVec4(1.0f, 0.5f, 0.3f, 1.0f)
                                             : GetStyleColorVec4(ImGuiCol_Text),
                "Battery %d%%%s  |  %.1f C  |  thermal throttling: %s",
                battery.value("level", 0),
                battery.value("charging", false) ? " (charging)" : "", temp,
                thermal >= 0 && thermal <= 6 ? kThermal[thermal] : "n/a");
  }
  Separator();

  if (samples.empty()) {
    TextDisabled("No frame stats yet. They need a game built by this editor "
                 "version (rebuild the APK / player).");
  } else {
    const ilmeee::DeviceSample &last = samples.back();
    const ImVec4 fpsColor = last.fps >= 55   ? ImVec4(0.3f, 0.9f, 0.4f, 1.0f)
                            : last.fps >= 30 ? ImVec4(0.95f, 0.8f, 0.3f, 1.0f)
                                             : ImVec4(1.0f, 0.4f, 0.3f, 1.0f);
    SetWindowFontScale(2.2f);
    TextColored(fpsColor, "%.1f FPS", last.fps);
    SetWindowFontScale(1.0f);
    Text("Frame %.2f ms (worst %.1f)  |  update %.2f  media %.2f  scene %.2f",
         last.frameMs, last.worstMs, last.updateMs, last.mediaMs,
         last.sceneMs);
    if (last.rssMiB >= 0) {
      SameLine();
      Text(" |  RAM %.0f MiB", last.rssMiB);
    }
    for (double seconds : {10.0, 60.0}) {
      const auto s = monitor.Summary(now, seconds);
      if (s.value("samples", 0) > 0)
        TextDisabled("Last %.0f s: avg %.1f fps, min %.1f fps, avg frame "
                     "%.2f ms, worst frame %.1f ms",
                     seconds, s.value("avgFps", 0.0), s.value("minFps", 0.0),
                     s.value("avgFrameMs", 0.0), s.value("worstFrameMs", 0.0));
    }

    // Last minute at two samples per second.
    const size_t n = std::min<size_t>(samples.size(), 120);
    std::vector<float> fps(n), frame(n), worst(n);
    float maxFps = 60.0f, maxMs = 33.4f;
    for (size_t i = 0; i < n; ++i) {
      const auto &s = samples[samples.size() - n + i];
      fps[i] = (float)s.fps;
      frame[i] = (float)s.frameMs;
      worst[i] = (float)s.worstMs;
      maxFps = std::max(maxFps, fps[i]);
      maxMs = std::max(maxMs, worst[i]);
    }
    PlotLines("##fps", fps.data(), (int)n, 0, "FPS (last minute)", 0.0f,
              maxFps * 1.1f, ImVec2(-1, 80));
    PlotLines("##frame", frame.data(), (int)n, 0, "Frame time ms", 0.0f,
              maxMs * 1.1f, ImVec2(-1, 60));
    PlotHistogram("##worst", worst.data(), (int)n, 0,
                  "Worst frame ms per 0.5 s (spikes = stutter)", 0.0f,
                  maxMs * 1.1f, ImVec2(-1, 60));
  }

  if (CollapsingHeader("Game log")) {
    static char filter[128] = "";
    SetNextItemWidth(-1);
    InputTextWithHint("##devfilter", "Filter...", filter, sizeof filter);
    BeginChild("##devlog", ImVec2(0, 220), true,
               ImGuiWindowFlags_HorizontalScrollbar);
    const auto &log = monitor.Log();
    const size_t first = log.size() > 500 ? log.size() - 500 : 0;
    for (size_t i = first; i < log.size(); ++i) {
      const std::string &line = log[i];
      if (filter[0] && line.find(filter) == std::string::npos)
        continue;
      const bool bad = line.size() > 2 && line[1] == '/' &&
                       (line[0] == 'E' || line[0] == 'F');
      const bool warn = line.size() > 2 && line[1] == '/' && line[0] == 'W';
      if (bad || warn)
        TextColored(bad ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f)
                        : ImVec4(1.0f, 0.8f, 0.35f, 1.0f),
                    "%s", line.c_str());
      else
        TextUnformatted(line.c_str());
    }
    if (GetScrollY() >= GetScrollMaxY() - 5.0f)
      SetScrollHereY(1.0f);
    EndChild();
  }
  End();
}

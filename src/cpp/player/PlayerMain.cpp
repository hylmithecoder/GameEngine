// IlmeeePlayer — runs a built game: opens the game folder, loads its start
// scene and shows it through the scene's camera, filling the window.
//
// A built game looks like
//
//   <Game>/
//     <Game> -> bin/<Game>     (this program, renamed)
//     bin/  lib/               (lib/ only when libraries were bundled)
//     game.json                {"name", "version", "startScene", "window": {...}}
//     scenes/  assets/         (project data + engine shaders)
//
// On Android the same folder is packed into the APK (assets/game/) and
// extracted to internal storage on first start; see AndroidAssets.hpp.
//
// Usage: <Game> [--game <dir>] [--screenshot <out.png> [--frames N]]
//   --game        game folder (default: the folder holding game.json next to
//                 or one above this binary)
//   --screenshot  render N frames (default 5), write the last one as PNG and
//                 exit — used by the build tests to check what a game shows.

#include "../../../include/core_engine/IlmeeeScene.hpp"
#include "../../../include/core_engine/MovementScript.hpp"
#include "../../../include/core_engine/NativeScriptScene.hpp"
#include "../../../include/core_engine/DialogSystem.hpp"
#include "../../../include/core_engine/GameUI.hpp"
#include "../../../include/core_engine/MediaSystem.hpp"
#include "../../../include/core_engine/SceneLoader.hpp"
#include "../../../include/core_engine/SceneRenderer.hpp"
#include "../../../include/core_engine/UserDataDir.hpp"
#include "../../../include/vulkan/VulkanBase.hpp"
#include "../../../vendor/nlohmann/json.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#if defined(__ANDROID__)
#include "AndroidAssets.hpp"
#include <SDL3/SDL_main.h> // turns main() into SDL_main for SDLActivity
#include <sys/system_properties.h>
#endif
#if defined(__linux__)
#include <unistd.h>
#endif

using namespace ImGui;

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

// Live stats for the editor's Device Monitor, on only when the editor runs
// the game: it sets ILMEEE_STATS=1 (desktop) or the debug.ilmeee.stats
// property over adb (Android).
bool LiveStatsRequested() {
#if defined(__ANDROID__)
  char value[PROP_VALUE_MAX] = {};
  return __system_property_get("debug.ilmeee.stats", value) > 0 &&
         value[0] == '1';
#else
  const char *value = std::getenv("ILMEEE_STATS");
  return value && value[0] == '1';
#endif
}

// Resident memory of this process in MiB, -1 when unknown.
double ResidentMiB() {
#if defined(__linux__)
  long pages = 0, resident = 0;
  if (FILE *f = std::fopen("/proc/self/statm", "r")) {
    const int read = std::fscanf(f, "%ld %ld", &pages, &resident);
    std::fclose(f);
    if (read == 2)
      return resident * (double)sysconf(_SC_PAGESIZE) / (1024.0 * 1024.0);
  }
#endif
  return -1.0;
}

struct GameManifest {
  std::string name = "Ilmeee Game";
  std::string startScene = "scenes/main.ilmeeescene";
  int width = 1280;
  int height = 720;
  bool fullscreen = false;
};

bool ReadManifest(const fs::path &file, GameManifest &out, std::string &error) {
  std::ifstream in(file);
  if (!in) {
    error = "cannot open " + file.string();
    return false;
  }
  json j = json::parse(in, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    error = file.string() + " is not valid JSON";
    return false;
  }
  out.name = j.value("name", out.name);
  out.startScene = j.value("startScene", out.startScene);
  if (auto w = j.find("window"); w != j.end() && w->is_object()) {
    out.width = w->value("width", out.width);
    out.height = w->value("height", out.height);
    out.fullscreen = w->value("fullscreen", out.fullscreen);
  }
  return true;
}

// The folder with game.json: next to the binary, or one up (bin/<Game>).
fs::path FindGameRoot() {
  fs::path exe = ilmeee::ExecutableDir();
  for (const fs::path &candidate : {exe, exe.parent_path()}) {
    std::error_code ec;
    if (fs::exists(candidate / "game.json", ec))
      return candidate;
  }
  return {};
}

class GamePlayer : public vkhandler::VulkanBase {
public:
  GamePlayer(fs::path root, GameManifest manifest, std::string screenshotPath,
             int screenshotFrames)
      : root_(std::move(root)), manifest_(std::move(manifest)),
        screenshotPath_(std::move(screenshotPath)),
        screenshotFrames_(screenshotFrames) {}

  int ExitCode() const { return exitCode_; }

protected:
  void OnInit() override {
    ilmeee::RegisterUserMovementScripts();
    // The player has no UI layout to remember; don't touch the editor's.
    GetIO().IniFilename = nullptr;

    renderer_ = std::make_unique<SceneRenderer>(windowWidth, windowHeight);
    renderer_->SetVulkanContext(ctx.device, ctx.physicalDevice,
                                ctx.graphicsQueue, ctx.commandPool,
                                ctx.descriptorPool);
    // A game creates/destroys nothing mid-frame (video textures wait for the
    // device themselves), so let CPU and GPU overlap.
    waitIdleEachFrame = false;
    renderer_->SetAsyncSubmit(true);
    liveStats_ = LiveStatsRequested();
    if (liveStats_)
      ReportDeviceInfo();

    fs::path scenePath = root_ / manifest_.startScene;
    ilmeee::IlmeeeScene scene;
    if (!ilmeee::LoadScene(scenePath.string(), scene)) {
      error_ = "Cannot load scene " + scenePath.string() +
               " (missing, damaged, or made by a newer engine than this "
               "player; rebuild the player)";
      ::Log(error_, Debug::LogLevel::ERROR);
      return;
    }
    ilmeee::SceneLoadReport report =
        ilmeee::InstantiateScene(*renderer_, scene, root_);
    media_.SetProjectRoot(root_);
    native_.log = [](const std::string &line) { SDL_Log("%s", line.c_str()); };
    nativeContext_ = {renderer_.get(), &input_, &media_, native_.log,
                      &gameUi_, &dialog_, &physics_};
    // The Canvas is loaded before scripts so their Start() can use it.
    gameUi_ = ilmeee::GameUI{};
    if (!ilmeee::GameUIFromScene(scene, gameUi_)) {
      // Older projects stored the canvas as a separate asset.
      const fs::path uiPath = root_ / "assets/ui/main.json";
      std::error_code uiEc;
      if (fs::exists(uiPath, uiEc)) {
        std::string uiError;
        if (!ilmeee::LoadGameUI(uiPath.string(), gameUi_, &uiError))
          ::Log("Game UI: " + uiError, Debug::LogLevel::WARNING);
      }
    }
    dialog_.log = native_.log;
    dialog_.SetProjectRoot(root_);
    dialog_.Start(*renderer_, gameUi_);
    try {
      std::ifstream modulesFile(root_ / "native-scripts.json");
      if (modulesFile) {
        const auto modules = json::parse(modulesFile);
        for (const auto &entry : modules) {
          ilmeee::CompiledScript compiled;
          compiled.ok = true;
          compiled.source = entry.at("source").get<std::string>();
          compiled.className = entry.at("className").get<std::string>();
          const auto library = entry.at("library").get<std::string>();
#ifdef __ANDROID__
          if (!entry.value("androidNativeLibrary", false) ||
              fs::path(library).filename().string() != library ||
              !library.starts_with("libilmeee_script_") ||
              !library.ends_with(".so"))
            throw std::runtime_error("Invalid Android script module; rebuild this game for Android.");
          // dlopen resolves the packaged library in libmain's Android namespace.
          compiled.library = library;
#else
          compiled.library = root_ / library;
#endif
          std::string error;
          if (!native_.Load(compiled, error)) throw std::runtime_error(error);
        }
      }
      for (const auto &mesh : renderer_->meshes3d)
        for (const auto &binding : mesh.scripts)
          if (binding.enabled && !native_.Ready(binding.source))
            throw std::runtime_error("Native script module missing: " + binding.source + "; rebuild this game.");
      native_.Start(ilmeee::ScriptSceneHost(nativeContext_), ilmeee::ScriptSceneOwners(*renderer_, &gameUi_));
    } catch (const std::exception &e) {
      error_ = std::string("Script loading failed: ") + e.what();
      ::Log(error_, Debug::LogLevel::ERROR);
      return;
    }
    ::Log("Loaded " + std::to_string(report.loaded) + " object(s) from " +
              scenePath.string(),
          Debug::LogLevel::SUCCESS);
    if (report.failed > 0)
      ::Log(std::to_string(report.failed) + " object(s) could not be loaded",
            Debug::LogLevel::WARNING);
    if (!renderer_->HasPlayerCamera()) {
      error_ = "This scene has no camera. Add a Camera in the editor.";
      ::Log(error_, Debug::LogLevel::ERROR);
    }

    if (manifest_.fullscreen)
      SDL_SetWindowFullscreen(window, true);
  }

  void OnEvent(const SDL_Event &event) override {
    // Multi-touch: DrawGameUI hands fingers to joysticks/buttons; the rest
    // swipe the camera like right-drag does with a mouse.
    const ImVec2 display = GetIO().DisplaySize;
    switch (event.type) {
    case SDL_EVENT_FINGER_DOWN:
      fingers_[(int64_t)event.tfinger.fingerID] = {
          ImVec2(event.tfinger.x * display.x, event.tfinger.y * display.y)};
      break;
    case SDL_EVENT_FINGER_MOTION: {
      auto it = fingers_.find((int64_t)event.tfinger.fingerID);
      if (it == fingers_.end()) break;
      it->second.pos =
          ImVec2(event.tfinger.x * display.x, event.tfinger.y * display.y);
      if (!it->second.fresh && it->second.element.empty())
        touchLook_ += glm::vec2(event.tfinger.dx * display.x,
                                event.tfinger.dy * display.y);
      break;
    }
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
      fingers_.erase((int64_t)event.tfinger.fingerID);
      break;
    default:
      break;
    }
  }

  // Frame timing, logged every few seconds (logcat on Android): where a
  // slow frame goes — game logic, the scene pass (CPU record + GPU, since it
  // waits for the GPU) or everything else (UI, present, vsync).
  struct FrameStats {
    using Clock = std::chrono::steady_clock;
    Clock::time_point windowStart = Clock::now();
    int frames = 0;
    double updateMs = 0.0, mediaMs = 0.0, sceneMs = 0.0, worstMs = 0.0;
    Clock::time_point lastFrame = Clock::now();
  } stats_, live_;
  bool liveStats_ = false;
  bool liveWarm_ = false; // first window measured scene loading, not play

  void AddTiming(double FrameStats::*field, FrameStats::Clock::time_point start) {
    const double ms = std::chrono::duration<double, std::milli>(
                          FrameStats::Clock::now() - start)
                          .count();
    stats_.*field += ms;
    live_.*field += ms;
  }

  // Twice a second, one machine-readable line the editor parses (and keeps
  // out of its Console): ILMEEE_STATS {json}.
  void ReportLiveStats(double frameMs, FrameStats::Clock::time_point now) {
    live_.worstMs = std::max(live_.worstMs, frameMs);
    ++live_.frames;
    const double windowS =
        std::chrono::duration<double>(now - live_.windowStart).count();
    if (windowS < 0.5)
      return;
    if (!liveWarm_) {
      liveWarm_ = true;
      live_ = FrameStats{};
      return;
    }
    const double n = live_.frames;
    char line[320];
    std::snprintf(line, sizeof line,
                  "ILMEEE_STATS {\"fps\":%.2f,\"frameMs\":%.2f,"
                  "\"worstMs\":%.2f,\"updateMs\":%.2f,\"mediaMs\":%.2f,"
                  "\"sceneMs\":%.2f,\"rssMiB\":%.1f,\"width\":%u,"
                  "\"height\":%u,\"objects\":%zu,\"paused\":%s}",
                  n / windowS, windowS * 1000.0 / n, live_.worstMs,
                  live_.updateMs / n, live_.mediaMs / n, live_.sceneMs / n,
                  ResidentMiB(), swapChainExtent.width, swapChainExtent.height,
                  renderer_ ? renderer_->GetMesh3DCount() : size_t{0},
                  paused_ ? "true" : "false");
    SDL_Log("%s", line);
    live_ = FrameStats{};
  }

  void ReportDeviceInfo() {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(ctx.physicalDevice, &props);
    json info = {{"gpu", props.deviceName},
                 {"vulkan", std::to_string(VK_API_VERSION_MAJOR(props.apiVersion)) +
                                "." +
                                std::to_string(VK_API_VERSION_MINOR(props.apiVersion)) +
                                "." +
                                std::to_string(VK_API_VERSION_PATCH(props.apiVersion))},
                 {"driver", props.driverVersion},
                 {"game", manifest_.name}};
    SDL_Log("ILMEEE_GAME_INFO %s", info.dump().c_str());
  }

  void ReportFrameStats() {
    using namespace std::chrono;
    const auto now = FrameStats::Clock::now();
    const double frameMs =
        duration<double, std::milli>(now - stats_.lastFrame).count();
    stats_.lastFrame = now;
    if (liveStats_)
      ReportLiveStats(frameMs, now);
    stats_.worstMs = std::max(stats_.worstMs, frameMs);
    ++stats_.frames;
    const double windowS =
        duration<double>(now - stats_.windowStart).count();
    if (windowS < 5.0)
      return;
    char line[160];
    std::snprintf(line, sizeof line,
                  "[Perf] %.1f fps | per frame: update %.1f ms (media %.1f), "
                  "scene %.1f ms, worst frame %.1f ms",
                  stats_.frames / windowS, stats_.updateMs / stats_.frames,
                  stats_.mediaMs / stats_.frames,
                  stats_.sceneMs / stats_.frames, stats_.worstMs);
    SDL_Log("%s", line); // straight to logcat / stderr, unbuffered
    stats_ = FrameStats{};
  }

  void OnUpdate(float deltaTime) override {
    const auto updateStart = FrameStats::Clock::now();
    if (IsKeyPressed(ImGuiKey_F11, false)) {
      bool fs = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
      SDL_SetWindowFullscreen(window, !fs);
    }
    // Input gathered while drawing the previous frame (keyboard + sticks).
    if (renderer_ && !paused_ && error_.empty()) {
      movement_.Update(*renderer_,
                       dialog_.FreezesPlayer()
                           ? ilmeee::FrozenMovementInput(input_)
                           : input_,
                       deltaTime);
      physics_.Update(*renderer_, std::max(0.0f, deltaTime), &movement_);
      dialog_.Update(*renderer_, gameUi_, input_, deltaTime);
    }
    if (!paused_ && error_.empty()) {
      ilmeee::ScriptSceneDispatchPhysics(nativeContext_, native_);
      native_.Update(deltaTime);
      ilmeee::ScriptSceneEndFrame(nativeContext_);
      if (renderer_) renderer_->UpdateAnimations(std::max(0.0f, deltaTime));
    }
    // A game is always playing; sounds and videos start on the first frame.
    const auto mediaStart = FrameStats::Clock::now();
    if (renderer_ && error_.empty())
      media_.Update(*renderer_,
                    paused_ ? ilmeee::MediaSystem::State::Paused
                            : ilmeee::MediaSystem::State::Playing,
                    deltaTime);
    AddTiming(&FrameStats::mediaMs, mediaStart);
    AddTiming(&FrameStats::updateMs, updateStart);
  }

  void OnRender(VkCommandBuffer) override {
    ImGuiViewport *vp = GetMainViewport();
    SetNextWindowPos(vp->Pos);
    SetNextWindowSize(vp->Size);
    PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    Begin("##game", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoBackground);

    bool rendered = false;
    if (renderer_ && error_.empty()) {
      int w = (int)swapChainExtent.width, h = (int)swapChainExtent.height;
      if (w > 0 && h > 0) {
        renderer_->SetViewportSize(w, h);
        if (!paused_ || !hasFrame_ || w != lastRenderWidth_ ||
            h != lastRenderHeight_) {
          const auto sceneStart = FrameStats::Clock::now();
          hasFrame_ = renderer_->RenderGameView();
          AddTiming(&FrameStats::sceneMs, sceneStart);
          lastRenderWidth_ = w;
          lastRenderHeight_ = h;
        }
        rendered = hasFrame_;
      }
      if (rendered) {
        // V flipped exactly like the editor's viewport.
        Image((ImTextureID)renderer_->GetViewportDescriptorSet(),
                     vp->Size, ImVec2(0, 1), ImVec2(1, 0));
        const ImVec2 imagePos = GetItemRectMin();
        const auto texture = [&](const std::string &asset) -> ImTextureID {
          if (asset.empty()) return ImTextureID{};
          return (ImTextureID)renderer_->GetUiTextureDescriptor(
              (root_ / asset).string());
        };
        input_ = paused_ ? ilmeee::MovementInput{}
                         : ilmeee::ReadKeyboardMouseInput();
        if (!paused_) input_.lookDelta += touchLook_;
        touchLook_ = glm::vec2(0.0f);
        // Same flow as the editor's Game tab: sticks mirror the keyboard,
        // a held stick overrides it.
        ilmeee::EchoKeyboardOnJoysticks(*renderer_, input_);
        const std::string action =
            ilmeee::DrawGameUI(gameUi_, imagePos, vp->Size, texture, true,
                               paused_, &input_.axes, &fingers_,
                               &input_.uiPressed);
        if (action == "TogglePause")
          paused_ = !paused_;
        else if (action == "Interact" && !paused_)
          input_.interact = true;
        if (paused_)
          GetWindowDrawList()->AddText(
              ImVec2(imagePos.x + vp->Size.x * 0.5f - 35,
                     imagePos.y + vp->Size.y * 0.5f),
              IM_COL32(255, 255, 255, 255), "PAUSED");
      }
    }
    if (!error_.empty()) {
      ImVec2 ts = CalcTextSize(error_.c_str());
      SetCursorPos(
          ImVec2((vp->Size.x - ts.x) * 0.5f, (vp->Size.y - ts.y) * 0.5f));
      TextUnformatted(error_.c_str());
    }
    End();
    PopStyleVar(2);

    ReportFrameStats();
    if (!screenshotPath_.empty() && ++frame_ >= screenshotFrames_) {
      exitCode_ = rendered && WriteScreenshot() ? 0 : 1;
      isRunning = false;
    }
  }

  void OnCleanup() override {
    native_.Stop();
    if (renderer_)
      media_.Shutdown(*renderer_);
    renderer_.reset();
  }

private:
  bool WriteScreenshot() {
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    if (!renderer_->ReadbackViewport(rgba, w, h))
      return false;
    // The target is stored bottom-up relative to what the window shows (the
    // display flips V); flip rows so the PNG matches the screen.
    std::vector<uint8_t> flipped(rgba.size());
    const size_t row = (size_t)w * 4;
    for (int y = 0; y < h; ++y)
      std::copy_n(rgba.data() + (size_t)(h - 1 - y) * row, row,
                  flipped.data() + (size_t)y * row);
    bool ok = stbi_write_png(screenshotPath_.c_str(), w, h, 4, flipped.data(),
                             (int)row) != 0;
    ::Log(ok ? "Screenshot written to " + screenshotPath_
             : "Failed to write " + screenshotPath_,
          ok ? Debug::LogLevel::SUCCESS : Debug::LogLevel::ERROR);
    return ok;
  }

  fs::path root_;
  GameManifest manifest_;
  ilmeee::GameUI gameUi_;
  ilmeee::DialogSystem dialog_;
  bool paused_ = false;
  ilmeee::MovementSystem movement_;
  ilmeee::PhysicsSystem physics_;
  ilmeee::NativeScripts native_;
  ilmeee::NativeScriptSceneContext nativeContext_;
  ilmeee::MediaSystem media_;
  ilmeee::MovementInput input_;
  ilmeee::UiFingers fingers_;
  glm::vec2 touchLook_{0.0f};
  bool hasFrame_ = false;
  int lastRenderWidth_ = 0;
  int lastRenderHeight_ = 0;
  std::unique_ptr<SceneRenderer> renderer_;
  std::string error_;
  std::string screenshotPath_;
  int screenshotFrames_ = 5;
  int frame_ = 0;
  int exitCode_ = 0;
};

} // namespace

int main(int argc, char *argv[]) {
#if defined(__ANDROID__)
  // No command line or game folder next to a binary: the game comes out of
  // the APK, always fullscreen landscape.
  ilmeee::android::RedirectStdioToLogcat();
  std::string androidError;
  fs::path androidRoot = ilmeee::android::PrepareGameFiles(androidError);
  if (androidRoot.empty()) {
    std::fprintf(stderr, "%s\n", androidError.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Ilmeee",
                             androidError.c_str(), nullptr);
    return 2;
  }
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  argc = 1; // ignore whatever the activity passed
#endif
  fs::path root;
  std::string screenshot;
  int frames = 5;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--game" && i + 1 < argc)
      root = argv[++i];
    else if (a == "--screenshot" && i + 1 < argc)
      screenshot = argv[++i];
    else if (a == "--frames" && i + 1 < argc)
      frames = std::max(1, std::atoi(argv[++i]));
  }
#if defined(__ANDROID__)
  root = androidRoot;
#endif
  if (root.empty())
    root = FindGameRoot();
  if (root.empty()) {
    std::fprintf(stderr, "No game.json found next to this program; pass "
                         "--game <folder>\n");
    return 2;
  }
  std::error_code ec;
  root = fs::absolute(root, ec);
  if (!screenshot.empty())
    screenshot = fs::absolute(screenshot, ec).string();

  GameManifest manifest;
  std::string error;
  if (!ReadManifest(root / "game.json", manifest, error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 2;
  }

  // Engine resources (shaders) are looked up relative to the working
  // directory, and the build puts them under <game>/assets/.
  fs::current_path(root, ec);
  if (ec) {
    std::fprintf(stderr, "Cannot enter %s: %s\n", root.c_str(),
                 ec.message().c_str());
    return 2;
  }

#if defined(__ANDROID__)
  manifest.fullscreen = true;
#endif
  GamePlayer player(root, manifest, screenshot, frames);
  if (!player.Init(manifest.name, manifest.width, manifest.height))
    return 1;
  player.Run();
  int code = player.ExitCode();
  player.Cleanup();
  return code;
}

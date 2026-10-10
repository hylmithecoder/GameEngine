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
#include "../../../include/core_engine/GameUI.hpp"
#include "../../../include/core_engine/SceneLoader.hpp"
#include "../../../include/core_engine/SceneRenderer.hpp"
#include "../../../include/core_engine/UserDataDir.hpp"
#include "../../../include/vulkan/VulkanBase.hpp"
#include "../../../vendor/nlohmann/json.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#if defined(__ANDROID__)
#include "AndroidAssets.hpp"
#include <SDL3/SDL_main.h> // turns main() into SDL_main for SDLActivity
#endif

using namespace ImGui;

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

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

    fs::path scenePath = root_ / manifest_.startScene;
    ilmeee::IlmeeeScene scene;
    if (!ilmeee::LoadScene(scenePath.string(), scene)) {
      error_ = "Cannot load scene " + scenePath.string();
      ::Log(error_, Debug::LogLevel::ERROR);
      return;
    }
    ilmeee::SceneLoadReport report =
        ilmeee::InstantiateScene(*renderer_, scene, root_);
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

  void OnUpdate(float deltaTime) override {
    if (IsKeyPressed(ImGuiKey_F11, false)) {
      bool fs = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
      SDL_SetWindowFullscreen(window, !fs);
    }
    // Input gathered while drawing the previous frame (keyboard + sticks).
    if (renderer_ && !paused_ && error_.empty())
      movement_.Update(*renderer_, input_, deltaTime);
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
          hasFrame_ = renderer_->RenderGameView();
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
        if (ilmeee::DrawGameUI(gameUi_, imagePos, vp->Size, texture, true,
                               paused_, &input_.axes, &fingers_) ==
            "TogglePause")
          paused_ = !paused_;
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

    if (!screenshotPath_.empty() && ++frame_ >= screenshotFrames_) {
      exitCode_ = rendered && WriteScreenshot() ? 0 : 1;
      isRunning = false;
    }
  }

  void OnCleanup() override { renderer_.reset(); }

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
  bool paused_ = false;
  ilmeee::MovementSystem movement_;
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

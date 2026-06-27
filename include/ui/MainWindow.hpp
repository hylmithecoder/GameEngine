#include "../core_engine/Builder.hpp"
#include "../core_engine/NetworkManager.hpp"
#include "../core_engine/SceneRenderer.hpp"
#include "../core_engine/core_editor/panels/PanelManager.hpp"
#include "../vulkan/vulkanhandler.hpp"
#include "HandlerProject.hpp"
#include "SvgIconManager.hpp"
#include "VideoPlayer.hpp"
#include "assets.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <list>
#include <vector>
#define IMGUI_HAS_DOCK
#define IMGUI_HAS_VIEWPORT
using namespace ImGui;
using namespace Debug;

#include "../vulkan/VulkanBase.hpp"

class MainWindow : public vkhandler::VulkanBase {
private:
  bool showSecondary;

  bool showConfirmDialog(const char *message, const char *title);
  void set_window_icon();
  void setTheme(bool dark);
  bool openVideo(const char *filePath);
  bool updateVideoFrame();
  bool updateVideoFrameWithVulkan();
  bool openAudio();
  void updateAudio();
  void updateMedia();
  void updateMediaFixedGlitch();
  void renderVideoPlayer();
  void set_extension_icon();
  void renderVideoFrame();
  void set_mainbackground();

  VulkanHandler vulkanHandler;

  // Vulkan members are now inherited from VulkanBase (ctx, window, etc.)

  bool fullscreen;
  bool darkTheme;
  int currentTab;

  // Object properties for Inspector
  char objectName[64] = "Player";
  float position[3] = {0.0f, 0.0f, 0.0f};
  float rotation[3] = {0.0f, 0.0f, 0.0f};
  float scale[3] = {1.0f, 1.0f, 1.0f};
  // Surface (submesh) clicked in the viewport, targeted by the Inspector's
  // per-surface texture binding. -1 = none yet.
  int selectedSurface = -1;
  static constexpr float MIN_PANEL_WIDTH = 100.0f;
  float explorerSplitPosition = 200.0f;

  int audioStream = -1;
  int videoStream = -1;
  AVCodecContext *audioCodecContext = nullptr;
  AVChannelLayout audioChannelLayout;
  SDL_AudioDeviceID audioDeviceID = 0;
  SDL_AudioStream *sdlAudioStream = nullptr;
  SDL_AudioSpec audioSpec;
  SDL_Texture *convertFrameToTexture(AVFrame *frame, SDL_Renderer *renderer,
                                     SwsContext *swsCtx);

  // void InitRocket();
  // void LoadDocument(const char* url);
  VideoPlayer *videoPlayer; // Assuming VideoPlayer is a class that handles
                            // video playback

public:
  MainWindow(const char *title, int width = 1280, int height = 720);
  ~MainWindow();

  SceneRenderer *sceneRenderer = nullptr;
  HandlerProject projectHandler;

  // Standalone-debug fallback selector. When true and no project is
  // loaded, RenderSceneWindow boots a 2D sprite scene (testimage.png)
  // instead of the default 3D OBJ. Set by --2d CLI flag; ignored once
  // a project is opened.
  bool debug2D = false;

  // Modular editor panels (new architecture). Existing Render*Window
  // members are still rendered directly; over time they should be
  // migrated to Panel subclasses and registered here.
  Ilmeee::PanelManager panelManager;
  Ilmeee::Builder builder;
  SvgIconManager svgIcons;
  TextureData backgroundTexture;
  Color backgroundColor;
  SwrContext *swrContext = nullptr;
  bool isOnlyAudio = false;
  bool isOnlyRenderImage = false;
  bool firstOpenProject;
  int currentBgInt = 0;
  Assets assets;
  float volume = 1.0f;
  bool isBackgroundChanged = false;
  bool isBackgroundActived = false;
  bool isLoadScene = false;
  bool showExplorer = true;
  bool showInspector = true;
  bool showScene = true;
  bool showConsole = true;
  bool showHierarchy = true;
  bool showMainView = true;
  string currentFilter = "";
  vector<string> logFromIlmeeeEditor = {
      "Welcome to Ilmeee Editor", "This is a log message",
      "You can see logs here", "Enjoy your game dev experience!"};
  std::vector<std::string> messages;
  std::mutex messagesMutex;
  static const size_t MAX_MESSAGES = 1000;
  string currentMessageFrom27015 = "";
  std::unique_ptr<NetworkManager> networkManager;
  // list<string> currentName = {"Shiroko", "Shun_Small"};

  enum CurrentBackground {
    Shiroko,
    Shun_Small,
    Hanako_Swimsuit,
    Background_Count
  };
  CurrentBackground currentBg = static_cast<CurrentBackground>(currentBgInt);
  const char *backgroundOptions[3] = {"Shiroko", "Shun (Small)",
                                      "Hanako (Swimsuit)"};
  GLuint blurFBO, blurTexture;
  int screenWidth, screenHeight;
  int blurWidth = screenWidth / 4;
  int blurHeight = screenHeight / 4;

  bool processVideoPacket(AVPacket *pkt);
  void processAudioPacket(AVPacket *pkt);
  void HandleUpdateBackground(CurrentBackground currentBg);
  // Lifecycle overrides from VulkanBase
  void OnInit() override;
  void OnUpdate(float deltaTime) override;
  void OnRender(VkCommandBuffer cmd) override;
  void OnCleanup() override;
  void OnResize(int width, int height) override;
  void log(const char *message);
  void RenderExplorerWindow(HandlerProject::AssetFile projectRoot,
                            HandlerProject::AssetFile assetFolder,
                            const std::string &assetPath, bool isOpenProject);
  void RenderInspectorWindow();
  void RenderMainViewWindow();
  void RenderConsoleWindow();
  void RenderMenuBar();
  void HandleBackground(const ImVec2 &windowPos, const ImVec2 &windowSize);
  void HandleSearch();
  void RenderViewportToolbar();
  void RenderGameViewport();
  void HandleViewportInteraction(ImVec2 viewportPos, ImVec2 viewportSize);
  void RenderSceneWindow();
  void RenderHierarchyWindow();
  void Save3DScene();
  void RenderSceneToolbarView(ImVec2 viewportPos, ImVec2 viewportSize);
  void RenderPlayMenu();
  void PushMessage(const std::string &message);

  const std::vector<std::string> &getMessages() const { return messages; }
  void ClearMessages();
};
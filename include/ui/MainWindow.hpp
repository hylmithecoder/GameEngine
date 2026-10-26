#include "../core_engine/Builder.hpp"
#include "../core_engine/EditorSessionHistory.hpp"
#include "../core_engine/GameBuilder.hpp"
#include "../core_engine/net/MessageBus.hpp"
#include "../core_engine/DeviceMonitor.hpp"
#include "../core_engine/net/McpServer.hpp"
#include "../core_engine/SceneRenderer.hpp"
#include "../core_engine/GameUI.hpp"
#include "../core_engine/DialogSystem.hpp"
#include "../core_engine/MediaSystem.hpp"
#include "../core_engine/MovementScript.hpp"
#include "../core_engine/NativeScriptScene.hpp"
#include "../core_engine/core_editor/panels/PanelManager.hpp"
#include "../vulkan/vulkanhandler.hpp"
#include "HandlerProject.hpp"
#include "SvgIconManager.hpp"
#include "VideoPlayer.hpp"
#include "ViewportGizmo.hpp"
#include "assets.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <list>
#include <memory>
#include <sys/types.h>
#include <vector>
#define IMGUI_HAS_DOCK
#define IMGUI_HAS_VIEWPORT
using namespace ImGui;
using namespace Debug;

#include "../vulkan/VulkanBase.hpp"

// Asset reference as stored in the scene: relative to the project when the
// file is inside it, absolute otherwise (HandlerChildWindow.cpp).
std::string ProjectMediaPath(const std::string &project,
                             const std::string &source);

class AnimationEditor;
class MainWindow : public vkhandler::VulkanBase {
  friend class AnimationEditor;
private:
  std::unique_ptr<AnimationEditor> animationEditor;
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
  void LoadGameUiForProject();
  void RenderGameUiEditor();
  void EnsureSceneLoaded();
  bool BindUiImageAsset(int index, const std::string &sourcePath);
  void CreateUiImageAsset(const std::string &sourcePath, float x, float y);
  void set_mainbackground();

  VulkanHandler vulkanHandler;
  ilmeee::GameUI gameUi = ilmeee::DefaultGameUI();
  std::string gameUiProject;
  std::string loadedSceneProject;
  bool hasGameUiCanvas = false;
  ilmeee::GameUI gameUiBeforePlay; // restored on Stop
  ilmeee::DialogSystem dialogSystem;
  // -1 = world object/none, -2 = Canvas, >=0 = UI child index.
  int selectedUiElement = -1;
  bool focusGameTab = false;
  bool focusSceneTab = false;
  ilmeee::MovementInput gameMovementInput;
  ilmeee::MovementSystem movementSystem;
  ilmeee::NativeScripts nativeScripts;
  ilmeee::NativeScriptSceneContext nativeScriptContext;
  std::string nativeScriptsProject;
  void RenderNativeScriptInspector(size_t meshIndex);
  void RenderNativeScriptAddMenu(size_t meshIndex);
  void RenderNativeScriptBindings(std::vector<ilmeee::NativeScriptBinding> &bindings);
  void RenderNativeScriptChoices(std::vector<ilmeee::NativeScriptBinding> &bindings);
  // One Inspector widget per script value; recurses into List items.
  bool RenderScriptValueField(ilmeee::ScriptValue &value, const char *label,
                              bool &finished);
  bool RenderScriptObjectField(uint64_t &object, const char *label);
  // Combo listing the Canvas elements; true when `objectId` changed.
  bool UiElementCombo(const char *label, uint64_t &objectId);
  void RenderDialogInspector(size_t meshIndex);
  // Adds the Box / Text / Name / Talk-button elements `d` is missing.
  void CreateDialogUi(ilmeee::DialogComponent &d);
  // Audio Source / Video Player playback (Play mode + editor posters).
  ilmeee::MediaSystem media;
  bool gameWasStopped = true;

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
  EditorSessionHistory editorSessionHistory;
  bool viewportDragHistoryActive = false;
  ilmeee::TransformGizmoState transformGizmo;
  ilmeee::OrientationGizmoState orientationGizmo;
  bool viewportCameraHistoryActive = false;
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
  bool focusOutputTab = false;
  bool showHierarchy = true;
  bool showAnimation = true;
  void RenderAnimationWindow();
  void OpenAnimationEditor(size_t meshIndex);
  void OpenAnimationController(size_t meshIndex);
  void RenderAnimationInspector(size_t meshIndex);
  void RenderSpringBoneInspector(size_t meshIndex);
  void OpenMotionPreview(const std::string &path);
  void RenderMotionLibrary();
  string currentFilter = "";
  vector<string> logFromIlmeeeEditor = {
      "Welcome to Ilmeee Editor", "This is a log message",
      "You can see logs here", "Enjoy your game dev experience!"};
  std::vector<std::string> messages;
  std::mutex messagesMutex;
  static const size_t MAX_MESSAGES = 1000;
  // Owned by ApplicationManager; null until it is wired in, and again once
  // shutdown has torn the bus down. Polled once per frame in OnUpdate().
  ilmeee::net::MessageBus *ipcBus = nullptr;
  ilmeee::net::McpServer *mcpServer = nullptr;
  ilmeee::net::json HandleMcpTool(const std::string &name,
                                  const ilmeee::net::json &arguments);
  // list_components / add_component / remove_component (McpTools.cpp).
  ilmeee::net::json HandleMcpComponentTool(const std::string &name,
                                           const ilmeee::net::json &arguments);
  ilmeee::net::json McpComponentList(size_t index);
  // Ask the engine process to run its scene picker (File > Load Scene).
  void RequestEngineSceneLoad();
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
  // Explorer draws from this copy of projectHandler.rootAsset, refreshed only
  // when the tree is rebuilt (copying the whole tree every frame was the
  // editor's biggest CPU cost). A copy, not a reference: file actions inside
  // the tree view may rebuild rootAsset while it is being drawn.
  HandlerProject::AssetFile explorerTree;
  uint64_t explorerTreeVersion = ~0ull;
  void RenderExplorerWindow(const HandlerProject::AssetFile &projectRoot,
                            const HandlerProject::AssetFile &assetFolder,
                            const std::string &assetPath, bool isOpenProject);
  void RenderInspectorWindow();
  void RenderConsoleWindow();
  void RenderMenuBar();
  void HandleBackground(const ImVec2 &windowPos, const ImVec2 &windowSize);
  void HandleSearch();
  void RenderViewportToolbar();
  void RenderGameViewport();
  void HandleViewportInteraction(ImVec2 viewportPos, ImVec2 viewportSize);
  void RenderSceneWindow();
  void RenderHierarchyWindow();

  // ---- Game build (Build menu + Console > Build tab) -------------------
  // Saves the scene, then builds <project>/build/<linux|android>/<Game> on a
  // worker thread; with runWhenDone the game is started once it succeeds
  // (Android: installed on the USB device with adb, logcat streamed back).
  void StartGameBuild(bool runWhenDone);
  void LaunchBuiltGame();
  // Per frame: pick up a finished build, start the game, reap it on exit.
  void PollGameBuild();
  void RenderBuildMenu();
  void RenderBuildTab();
  void PushBuildLog(const std::string &line);
  // One line of the running game's output: telemetry to the Device Monitor,
  // the rest to the Build log and Console.
  void HandleGameOutput(const std::string &line);
  // Windows > Device Monitor: live FPS/frame time/RAM of the game started
  // with Run (desktop or the phone), plus device and battery state.
  void RenderDeviceMonitorWindow();
  void RenderLightingMenu();   // LightingPanel.cpp
  void RenderLightingWindow();
  ilmeee::DeviceMonitor deviceMonitor;
  bool showDeviceMonitor = false;
  bool showLighting = false;
  std::thread gameBuildThread;
  std::atomic<bool> gameBuildRunning{false};
  std::atomic<bool> gameBuildFinished{false};
  std::mutex gameBuildLogMutex;
  std::vector<std::string> gameBuildLog;
  bool gameBuildScrollToEnd = false;
  // Written by the worker; read only after gameBuildFinished is seen.
  ilmeee::GameBuildResult gameBuildResult;
  bool gameBuildRunAfter = false;
  bool gameBuildPortable = false;
  // Ship the whole assets/ folder instead of only what the scenes use.
  bool gameBuildIncludeUnused = false;
  // Android: re-encode videos taller than this to H.264 (0 = unchanged).
  int gameBuildAndroidVideoHeight = 720;
  ilmeee::BuildTarget gameBuildTarget = ilmeee::BuildTarget::Linux;
  std::filesystem::path lastGameLauncher; // binary, or .apk for Android
  ilmeee::BuildTarget lastGameTarget = ilmeee::BuildTarget::Linux;
  std::string lastGameApplicationId;
  pid_t gameProcess = -1;
  int gameOutputFd = -1;
  std::string gameOutputPending;
  // Short-lived helpers (xdg-open) waiting to be reaped.
  std::vector<pid_t> gameProcessHelpers;
  bool focusBuildTab = false;
  // Scene-object operations shared by the Hierarchy panel and the viewport.
  // Selection is by name (objectName), as everywhere else in the editor.
  int SelectedMeshIndex() const;
  // Hierarchy rename (F2, double-click, or right-click > Rename). Names are
  // kept unique and under 64 bytes because selection is by name.
  int renamingMesh = -1, renamingUi = -1;
  bool renameFocus = false;
  char hierarchyRename[64] = "";
  void BeginHierarchyRename(int mesh, int ui);
  void DrawHierarchyRenameField();
  bool RenameMesh(size_t index, std::string name);
  bool RenameUiElement(size_t index, std::string name);
  // kind: 0 Cube, 1 Sphere, 2 Plane, 3 Light, 4 Camera. `localPosition` is
  // relative to `parent` (-1 = scene root). Selects and records history.
  // Returns the new mesh index, or -1.
  int SpawnSceneObject(int kind, int parent, const glm::vec3 &localPosition);
  // Deletes the object and everything parented under it.
  void DeleteSceneObject(size_t index);
  // Adding/removing objects is blocked while playing: Stop restores the
  // scene by index and could not undo a structural change.
  bool SceneStructureLocked() const;
  // Deleting this subtree would leave the scene without a player camera
  // (which the Hierarchy would immediately recreate).
  bool DeletingRemovesLastCamera(size_t index) const;
  bool Save3DScene();
  EditorSessionHistory::Snapshot CaptureEditorSnapshot() const;
  void StartEditorSession();
  void RecordEditorHistory();
  void ApplyEditorSnapshot(const EditorSessionHistory::Snapshot &snapshot);
  void UndoEditor();
  void RedoEditor();
  void RenderSceneToolbarView(ImVec2 viewportPos, ImVec2 viewportSize);
  void RenderPlayMenu();
  void PushMessage(const std::string &message);

  const std::vector<std::string> &getMessages() const { return messages; }
  void ClearMessages();
};

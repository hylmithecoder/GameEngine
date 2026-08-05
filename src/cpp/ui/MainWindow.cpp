#include "../../../include/ui/MainWindow.hpp"
#include "../../../include/audio/FFmpegWrapper.hpp"
#include "../../../include/core_engine/Debugger.hpp"
#include "../../../include/core_engine/InspectMode.hpp"
#include "../../../include/core_engine/core_editor/EditorDockSpace.hpp"
#include "../../../include/core_engine/core_editor/EditorTheme.hpp"
#include "../../../include/core_engine/core_editor/panels/VideoPlayerPanel.hpp"
#include "../../../include/ui/SecondaryWindow.hpp"
#include "../../../include/ui/assets.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <stb/stb_image.h>
#include <string>
#include <vector>

using namespace std;
namespace ui = ImGui;

MainWindow::MainWindow(const char *title, int width, int height)
    : VulkanBase(), showSecondary(false), fullscreen(false), darkTheme(true),
      currentTab(0), videoPlayer(new VideoPlayer()) {}

MainWindow::~MainWindow() {
  if (videoPlayer) {
    videoPlayer->cleanup();
    delete videoPlayer;
  }
}

EditorSessionHistory::Snapshot MainWindow::CaptureEditorSnapshot() const {
  EditorSessionHistory::Snapshot snapshot;
  if (sceneRenderer)
    snapshot.renderer = sceneRenderer->CaptureEditorSnapshot();
  snapshot.selectedName = objectName;
  snapshot.selectedSurface = selectedSurface;
  snapshot.sceneName = projectHandler.currentScene.sceneName;
  snapshot.sceneObjects = projectHandler.currentScene.objects;
  return snapshot;
}

void MainWindow::StartEditorSession() {
  const std::string project = projectHandler.projectPath.empty()
                                  ? std::string("standalone")
                                  : projectHandler.projectPath;
  editorSessionHistory.Start(project, CaptureEditorSnapshot());
  viewportDragHistoryActive = false;
  viewportCameraHistoryActive = false;
  ::Log("Editor session history: " +
            editorSessionHistory.FilePath().string(),
        Debug::LogLevel::INFO);
}

void MainWindow::RecordEditorHistory() {
  if (sceneRenderer && editorSessionHistory.IsActive())
    editorSessionHistory.Push(CaptureEditorSnapshot());
}

void MainWindow::ApplyEditorSnapshot(
    const EditorSessionHistory::Snapshot &snapshot) {
  if (!sceneRenderer)
    return;

  const bool restored = sceneRenderer->RestoreEditorSnapshot(snapshot.renderer);
  projectHandler.currentScene.sceneName = snapshot.sceneName;
  projectHandler.currentScene.objects = snapshot.sceneObjects;
  sceneRenderer->currentScene = projectHandler.currentScene;

  std::snprintf(objectName, sizeof(objectName), "%s",
                snapshot.selectedName.c_str());
  selectedSurface = snapshot.selectedSurface;

  if (!restored) {
    ::Log("Some editor session assets could not be restored.",
          Debug::LogLevel::WARNING);
    projectHandler.ShowNotification(
        "Undo/Redo Warning", "One or more assets could not be restored",
        ImVec4(1.0f, 0.7f, 0.2f, 1.0f));
  }
}

void MainWindow::UndoEditor() {
  EditorSessionHistory::Snapshot snapshot;
  if (editorSessionHistory.Undo(snapshot)) {
    ApplyEditorSnapshot(snapshot);
    projectHandler.ShowNotification("Undo", "Viewport state restored",
                                    ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
  }
}

void MainWindow::RedoEditor() {
  EditorSessionHistory::Snapshot snapshot;
  if (editorSessionHistory.Redo(snapshot)) {
    ApplyEditorSnapshot(snapshot);
    projectHandler.ShowNotification("Redo", "Viewport state restored",
                                    ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
  }
}

void MainWindow::OnInit() {
  ::Log("MainWindow::OnInit - Initializing Project Specific Resources");

  set_window_icon();

  ImGuiIO &io = ui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  ImFontConfig fontCfg;
  fontCfg.MergeMode = false;
  io.Fonts->AddFontFromFileTTF("assets/fonts/MiSansLatin-Regular.ttf", 16.0f,
                               &fontCfg);
  fontCfg.MergeMode = true;
  fontCfg.GlyphOffset.y = 1.0f;
  io.Fonts->AddFontFromFileTTF(
      "assets/fonts/zh-cn.ttf", 13.0f, &fontCfg,
      io.Fonts->GetGlyphRangesChineseSimplifiedCommon());

  Ilmeee::EditorTheme::Apply(darkTheme ? Ilmeee::EditorTheme::Variant::Dark
                                       : Ilmeee::EditorTheme::Variant::Light);

  sceneRenderer = new SceneRenderer(800, 600);
  sceneRenderer->SetVulkanContext(ctx.device, ctx.physicalDevice,
                                  ctx.graphicsQueue, ctx.commandPool,
                                  ctx.descriptorPool);

  // Initialize VulkanHandler with the context
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(ctx.physicalDevice, &memProperties);
  vulkanHandler.setCurrentDeviceAndPhysic(
      ctx.device, ctx.physicalDevice, ctx.graphicsQueue,
      ctx.graphicsQueueFamily, ctx.commandPool, memProperties);

  // Link VulkanHandler to ProjectHandler
  projectHandler.SetVulkanHandler(&vulkanHandler);

  // Initialize SVG icon manager for the editor UI
  svgIcons.Init(&vulkanHandler);
  projectHandler.SetSvgIconManager(&svgIcons);

  // Initialize Builder for Play/Pause/Stop controls
  builder.Init(sceneRenderer);

  networkManager = std::make_unique<NetworkManager>();
  networkManager->connectToServer();

  projectHandler.fileWatcherRunning = false;
  projectHandler.fileWatcherInterval = std::chrono::milliseconds(1000);
  projectHandler.fileChangesDetected = false;
  projectHandler.StartFileWatcher();

  // Modular panels — VideoPlayerPanel now owns its own ImGui window
  // (transport + seek bar + frame), replacing the old monolithic
  // renderVideoPlayer() implementation.
  panelManager.Register<Ilmeee::VideoPlayerPanel>(&vulkanHandler);

  ::Log("MainWindow::OnInit - Completed");
}

void MainWindow::OnUpdate(float deltaTime) {
  if (projectHandler.fileChangesDetected) {
    projectHandler.fileChangesDetected = false;
  }

  if (videoPlayer && videoPlayer->isPlaying) {
    updateMedia();
  }
}

void MainWindow::OnRender(VkCommandBuffer cmd) {
  ImGuiIO &io = ImGui::GetIO();

  // Keep text fields' native Ctrl+Z behavior intact. Outside text input,
  // Ctrl+Z/Ctrl+Y operate on the current editor session history.
  if (!io.WantTextInput && io.KeyCtrl) {
    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
      if (io.KeyShift)
        RedoEditor();
      else
        UndoEditor();
    } else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
      RedoEditor();
    }
  }

  // Check for Ctrl+S keyboard shortcut
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
    Save3DScene();
  }

  // F2 toggles Inspect Mode. Honored even when --debug wasn't passed so
  // a running session can flip the overlay on for ad-hoc UI archaeology.
  if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
    Debug::g_InspectModeActive = !Debug::g_InspectModeActive;
    ::Log(Debug::g_InspectModeActive ? "Inspect Mode: ON" : "Inspect Mode: OFF",
          Debug::LogLevel::INFO);
  }
  Debug::Inspect::BeginFrame();

  // Fullscreen dockspace wraps every panel below. RenderMenuBar() must
  // stay outside the host window so the OS-style menubar sits at the
  // top of the actual main viewport, not inside a docked window.
  RenderMenuBar();
  RenderPlayMenu();

  Ilmeee::EditorDockSpace::Begin("IlmeeeDockSpace", 32.0f);

  if (showMainView)
    RenderMainViewWindow();
  if (showScene)
    RenderSceneWindow();
  if (showHierarchy)
    RenderHierarchyWindow();
  if (showExplorer) {
    RenderExplorerWindow(projectHandler.rootAsset, projectHandler.rootAsset,
                         "assets", true);
  }
  if (showInspector)
    RenderInspectorWindow();
  if (showConsole)
    RenderConsoleWindow();

  // Modular panels (e.g. VideoPlayerPanel). Each panel renders into
  // its own ImGui window via PanelManager — dockable like the rest.
  panelManager.RenderAll();

  Ilmeee::EditorDockSpace::End();

  // Top-most overlay: outline + tooltip for whatever the user is hovering.
  // Must come after all panels so the trace registry is fully populated.
  Debug::Inspect::RenderHoverOverlay();
}

void MainWindow::OnCleanup() {
  ::Log("MainWindow::OnCleanup");
  projectHandler.StopFileWatcher();

  if (sceneRenderer) {
    delete sceneRenderer;
    sceneRenderer = nullptr;
  }

  if (sdlAudioStream) {
    SDL_DestroyAudioStream(sdlAudioStream);
    sdlAudioStream = nullptr;
  }
}

void MainWindow::OnResize(int width, int height) {
  if (sceneRenderer) {
    sceneRenderer->SetViewportSize(width, height);
  }
}

// Add this helper function at the top of your file
bool MainWindow::showConfirmDialog(const char *message, const char *title) {
  GtkWidget *dialog;
  gint response;

  // Initialize GTK if not already done
  if (!gtk_init_check(0, nullptr)) {
    Log("Failed to initialize GTK", Debug::LogLevel::CRASH);
    return false;
  }

  dialog =
      gtk_message_dialog_new(nullptr, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
                             GTK_BUTTONS_YES_NO, "%s", message);
  gtk_window_set_title(GTK_WINDOW(dialog), title);

  response = gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);

  return response == GTK_RESPONSE_YES;
}

void MainWindow::set_window_icon() {
  SDL_Surface *icon = SDL_LoadBMP("assets/icons/app_icon.bmp");
  if (icon) {
    SDL_SetWindowIcon(window, icon);
    SDL_DestroySurface(icon);
  } else {
    cerr << "Warning: Could not load icon: " << SDL_GetError() << endl;
  }
}

void MainWindow::HandleUpdateBackground(CurrentBackground currentBg) {
  const char *path = nullptr;

  switch (currentBg) {
  case Shiroko:
    path = "assets/images/backgrounds/shiroko_bluearchive.jpg";
    cout << "Current Background is: Shiroko" << endl;
    break;
  case Shun_Small:
    path = "assets/images/backgrounds/shun_small.webp";
    cout << "Current Background is: Shun" << endl;
    break;
  case Hanako_Swimsuit:
    path = "assets/images/backgrounds/Hanako_Swimsuit.png";
    cout << "Current Background is: Hanako" << endl;
    break;
  default:
    cout << "Unknown Background" << endl;
    return;
  }

  if (path)
    assets.LoadTextureFromFile(path, &backgroundTexture);
}

void MainWindow::setTheme(bool dark) {
  if (dark) {
    // Dark theme dengan warna yang lebih modern
    ui::StyleColorsDark();
    ImGuiStyle &style = ui::GetStyle();

    style.WindowPadding = ImVec2(12, 12);
    style.FramePadding = ImVec2(8, 6);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 6);
    style.ScrollbarSize = 16;
    style.GrabMinSize = 12;

    style.WindowRounding = 8.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 12.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 8.0f;

    ImVec4 *colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.16f, 0.21f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.24f, 0.26f, 0.32f, 0.50f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.13f, 0.17f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.18f, 0.21f, 1.00f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.15f, 0.18f, 1.00f);

    colors[ImGuiCol_Button] = ImVec4(0.28f, 0.56f, 1.00f, 0.54f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.56f, 1.00f, 0.86f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);

    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.42f, 0.78f, 0.45f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);

    colors[ImGuiCol_Tab] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
    colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.41f, 0.68f, 1.00f);
  } else {
    // Light theme yang elegan
    ui::StyleColorsLight();
    ImGuiStyle &style = ui::GetStyle();

    style.WindowPadding = ImVec2(12, 12);
    style.FramePadding = ImVec2(8, 6);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 6);
    style.ScrollbarSize = 16;
    style.GrabMinSize = 12;

    style.WindowRounding = 8.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 12.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 8.0f;

    ImVec4 *colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.70f, 0.70f, 0.70f, 0.50f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.78f, 0.78f, 0.78f, 1.00f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.86f, 0.86f, 0.86f, 1.00f);

    colors[ImGuiCol_Button] = ImVec4(0.00f, 0.55f, 0.83f, 0.40f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.00f, 0.55f, 0.83f, 0.60f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.00f, 0.55f, 0.83f, 0.80f);

    colors[ImGuiCol_Header] = ImVec4(0.00f, 0.55f, 0.83f, 0.30f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.55f, 0.83f, 0.50f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.00f, 0.55f, 0.83f, 0.70f);

    colors[ImGuiCol_Tab] = ImVec4(0.84f, 0.84f, 0.84f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.00f, 0.55f, 0.83f, 0.50f);
    colors[ImGuiCol_TabActive] = ImVec4(0.00f, 0.55f, 0.83f, 0.70f);
  }
}

bool MainWindow::openVideo(const char *filePath) {
  if (vulkanHandler.OpenFileVideo(filePath)) {
    vulkanHandler.openAudio();
    vulkanHandler.isPlaying = true;
    return true;
  }
  return false;
}

bool MainWindow::updateVideoFrameWithVulkan() {
  if (!videoPlayer->isPlaying)
    return false;

  int frameFinished = 0;
  int readAttempts = 0;
  const float MAX_READ_ATTEMPTS = 1.0f;

  while (!frameFinished && readAttempts < MAX_READ_ATTEMPTS) {
    readAttempts++;
    int readResult =
        av_read_frame(videoPlayer->formatContext, videoPlayer->packet);
    if (readResult < 0) {
      if (readResult == AVERROR_EOF) {
        av_seek_frame(videoPlayer->formatContext, videoPlayer->videoStream, 0,
                      AVSEEK_FLAG_BACKWARD);
        continue;
      } else {
        SDL_Delay(5);
        continue;
      }
    }

    if (videoPlayer->packet->stream_index == videoPlayer->videoStream) {
      int sendResult =
          avcodec_send_packet(videoPlayer->codecContext, videoPlayer->packet);
      av_packet_unref(videoPlayer->packet);

      if (sendResult < 0)
        continue;

      int receiveResult =
          avcodec_receive_frame(videoPlayer->codecContext, videoPlayer->frame);
      if (receiveResult < 0) {
        if (receiveResult == AVERROR(EAGAIN))
          continue;
        else if (receiveResult == AVERROR_EOF)
          break;
        else
          continue;
      } else {
        frameFinished = 1;
        break;
      }
    } else {
      av_packet_unref(videoPlayer->packet);
    }
  }

  if (frameFinished) {
    sws_scale(videoPlayer->swsContext,
              (uint8_t const *const *)videoPlayer->frame->data,
              videoPlayer->frame->linesize, 0, videoPlayer->height,
              videoPlayer->frameRGB->data, videoPlayer->frameRGB->linesize);

    // SDL_UpdateTexture(videoPlayer->texture, NULL,
    // videoPlayer->frameRGB->data[0],
    //                   videoPlayer->frameRGB->linesize[0]);

    // glBindTexture(GL_TEXTURE_2D, videoPlayer->glTextureID);
    // glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, videoPlayer->width,
    //                 videoPlayer->height, GL_RGB, GL_UNSIGNED_BYTE,
    //                 videoPlayer->frameRGB->data[0]);

    // Update current time using frame PTS
    if (videoPlayer->frame->pts != AV_NOPTS_VALUE) {
      AVRational timeBase =
          videoPlayer->formatContext->streams[videoPlayer->videoStream]
              ->time_base;
      videoPlayer->currentTime = videoPlayer->frame->pts * av_q2d(timeBase);
    }

    return true;
  }

  return false;
}

bool MainWindow::updateVideoFrame() {
  if (!videoPlayer->isPlaying)
    return false;

  int frameFinished = 0;
  int readAttempts = 0;
  const int MAX_READ_ATTEMPTS = 10;

  while (!frameFinished && readAttempts < MAX_READ_ATTEMPTS) {
    readAttempts++;
    int readResult =
        av_read_frame(videoPlayer->formatContext, videoPlayer->packet);
    if (readResult < 0) {
      if (readResult == AVERROR_EOF) {
        av_seek_frame(videoPlayer->formatContext, videoPlayer->videoStream, 0,
                      AVSEEK_FLAG_BACKWARD);
        continue;
      } else {
        SDL_Delay(5);
        continue;
      }
    }

    if (videoPlayer->packet->stream_index == videoPlayer->videoStream) {
      int sendResult =
          avcodec_send_packet(videoPlayer->codecContext, videoPlayer->packet);
      av_packet_unref(videoPlayer->packet);

      if (sendResult < 0)
        continue;

      int receiveResult =
          avcodec_receive_frame(videoPlayer->codecContext, videoPlayer->frame);
      if (receiveResult < 0) {
        if (receiveResult == AVERROR(EAGAIN))
          continue;
        else if (receiveResult == AVERROR_EOF)
          break;
        else
          continue;
      } else {
        frameFinished = 1;
        break;
      }
    } else {
      av_packet_unref(videoPlayer->packet);
    }
  }

  if (frameFinished) {
    sws_scale(videoPlayer->swsContext,
              (uint8_t const *const *)videoPlayer->frame->data,
              videoPlayer->frame->linesize, 0, videoPlayer->height,
              videoPlayer->frameRGB->data, videoPlayer->frameRGB->linesize);

    // SDL_UpdateTexture(videoPlayer->texture, NULL,
    // videoPlayer->frameRGB->data[0],
    //                   videoPlayer->frameRGB->linesize[0]);

    // Update current time using frame PTS
    if (videoPlayer->frame->pts != AV_NOPTS_VALUE) {
      AVRational timeBase =
          videoPlayer->formatContext->streams[videoPlayer->videoStream]
              ->time_base;
      videoPlayer->currentTime = videoPlayer->frame->pts * av_q2d(timeBase);
    }

    return true;
  }

  return false;
}

bool MainWindow::openAudio() {
  // Find the best audio stream
  audioStream = av_find_best_stream(videoPlayer->formatContext,
                                    AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  if (audioStream < 0) {
    cerr << "Could not find audio stream" << endl;
    return false;
  }

  // Get the stream and find decoder
  AVStream *st = videoPlayer->formatContext->streams[audioStream];
  const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
  if (!dec) {
    cerr << "Could not find audio decoder" << endl;
    return false;
  }

  // Allocate codec context
  audioCodecContext = avcodec_alloc_context3(dec);
  if (!audioCodecContext) {
    cerr << "Could not allocate audio codec context" << endl;
    return false;
  }

  // Copy parameters to context
  if (avcodec_parameters_to_context(audioCodecContext, st->codecpar) < 0) {
    cerr << "Could not copy audio codec parameters to context" << endl;
    avcodec_free_context(&audioCodecContext);
    return false;
  }

  // Open the codec
  if (avcodec_open2(audioCodecContext, dec, nullptr) < 0) {
    cerr << "Could not open audio codec" << endl;
    avcodec_free_context(&audioCodecContext);
    return false;
  }

  // Initialize audio resampler
  swrContext = swr_alloc();
  if (!swrContext) {
    cerr << "Could not allocate resampler context" << endl;
    avcodec_free_context(&audioCodecContext);
    return false;
  }

  // Set up input channel layout
  AVChannelLayout in_layout = audioCodecContext->ch_layout;
  AVChannelLayout out_layout;
  av_channel_layout_default(&out_layout, 2); // stereo
  av_channel_layout_copy(&audioChannelLayout, &out_layout);

  // Configure the resampler
  av_opt_set_chlayout(swrContext, "in_chlayout", &in_layout, 0);
  av_opt_set_chlayout(swrContext, "out_chlayout", &out_layout, 0);
  av_opt_set_int(swrContext, "in_sample_rate", audioCodecContext->sample_rate,
                 0);
  av_opt_set_int(swrContext, "out_sample_rate", audioCodecContext->sample_rate,
                 0);
  av_opt_set_sample_fmt(swrContext, "in_sample_fmt",
                        audioCodecContext->sample_fmt, 0);
  av_opt_set_sample_fmt(swrContext, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

  // Initialize the resampler
  int ret = swr_init(swrContext);
  if (ret < 0) {
    swr_free(&swrContext);
    avcodec_free_context(&audioCodecContext);
    return false;
  }
  videoPlayer->swrContext = swrContext;

  // SDL3 Audio Setup
  SDL_AudioSpec src_spec;
  src_spec.format = SDL_AUDIO_S16;
  src_spec.channels = 2;
  src_spec.freq = audioCodecContext->sample_rate;

  sdlAudioStream = SDL_CreateAudioStream(&src_spec, &src_spec);
  if (!sdlAudioStream) {
    cerr << "Failed to create SDL audio stream: " << SDL_GetError() << endl;
    return false;
  }

  audioDeviceID =
      SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &src_spec);
  if (audioDeviceID == 0) {
    cerr << "Failed to open audio device: " << SDL_GetError() << endl;
    SDL_DestroyAudioStream(sdlAudioStream);
    sdlAudioStream = nullptr;
    return false;
  }

  SDL_BindAudioStream(audioDeviceID, sdlAudioStream);
  SDL_ResumeAudioDevice(audioDeviceID);

  cout << "Audio initialized successfully" << endl;
  videoPlayer->isPlayingAudio = true;

  return true;
}

void MainWindow::updateAudio() {
  if (!audioCodecContext || !swrContext || audioDeviceID == 0) {
    return; // Audio not initialized
  }

  if (!videoPlayer->isPlaying) {
    return; // Video not playing
  }

  // cout << "Updating audio..." << endl;

  // Check if audio queue is getting too full
  const int MAX_AUDIO_QUEUE_SIZE = 8192 * 64; // Lebih besar
  const int IDEAL_QUEUE_SIZE = 8192 * 32;     // Target size

  // Check if we need more audio
  Uint32 currentQueueSize = SDL_GetAudioStreamAvailable(sdlAudioStream);
  if (currentQueueSize > IDEAL_QUEUE_SIZE) {
    return; // Cukup audio dalam buffer
  }

  AVPacket *pkt = av_packet_alloc();
  if (!pkt) {
    cerr << "Could not allocate packet" << endl;
    return;
  }

  AVFrame *frame = av_frame_alloc();
  if (!frame) {
    cerr << "Could not allocate frame" << endl;
    av_packet_free(&pkt);
    return;
  }

  bool packetProcessed = false;
  int readResult;

  // Keep reading frames until we process an audio packet or reach end of file
  while (!packetProcessed &&
         (readResult = av_read_frame(videoPlayer->formatContext, pkt)) >= 0) {
    if (pkt->stream_index == audioStream) {
      int sendResult = avcodec_send_packet(audioCodecContext, pkt);
      if (sendResult < 0) {
        char errbuf[256];
        av_strerror(sendResult, errbuf, sizeof(errbuf));
        cerr << "Error sending audio packet to decoder: " << errbuf << endl;
        av_packet_unref(pkt);
        continue;
      }

      while (true) {
        int receiveResult = avcodec_receive_frame(audioCodecContext, frame);
        if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
          // Need more packets or end of file
          break;
        } else if (receiveResult < 0) {
          // Error
          char errbuf[256];
          av_strerror(receiveResult, errbuf, sizeof(errbuf));
          cerr << "Error receiving audio frame: " << errbuf << endl;
          break;
        }

        // Calculate output channel count (always 2 for stereo)
        int outChannels = 2;

        // Calculate buffer size needed
        int outSamples = av_rescale_rnd(
            swr_get_delay(swrContext, audioCodecContext->sample_rate) +
                frame->nb_samples,
            44100, // output rate
            audioCodecContext->sample_rate, AV_ROUND_UP);

        // Allocate output buffer
        uint8_t *outBuf = nullptr;
        int outLinesize = 0;
        int allocResult = av_samples_alloc(&outBuf, &outLinesize, outChannels,
                                           outSamples, AV_SAMPLE_FMT_S16, 0);

        if (allocResult < 0) {
          char errbuf[256];
          av_strerror(allocResult, errbuf, sizeof(errbuf));
          cerr << "Could not allocate audio output buffer: " << errbuf << endl;
          break;
        }

        // Convert audio samples
        int convertedSamples =
            swr_convert(swrContext, &outBuf, outSamples,
                        (const uint8_t **)frame->data, frame->nb_samples);

        if (convertedSamples < 0) {
          char errbuf[256];
          av_strerror(convertedSamples, errbuf, sizeof(errbuf));
          cerr << "Error converting audio: " << errbuf << endl;
          av_freep(&outBuf);
          break;
        }

        // Calculate size of converted data in bytes
        int outBufSize = av_samples_get_buffer_size(
            nullptr, outChannels, convertedSamples, AV_SAMPLE_FMT_S16, 1);

        if (outBufSize > 0) {
          // Queue to SDL audio
          SDL_PutAudioStreamData(sdlAudioStream, outBuf, outBufSize);
          packetProcessed = true;
        }

        // Free the output buffer
        av_freep(&outBuf);
      }
    }
    av_packet_unref(pkt);
  }

  // Handle end of file or error
  if (readResult < 0 && readResult != AVERROR_EOF) {
    char errbuf[256];
    av_strerror(readResult, errbuf, sizeof(errbuf));
    cerr << "Error reading frame: " << errbuf << endl;
  } else if (readResult == AVERROR_EOF) {
    // End of file - seek back to beginning (for looping)
    int seekResult =
        av_seek_frame(videoPlayer->formatContext, -1, 0, AVSEEK_FLAG_BACKWARD);
    if (seekResult < 0) {
      char errbuf[256];
      av_strerror(seekResult, errbuf, sizeof(errbuf));
      cerr << "Error seeking to beginning: " << errbuf << endl;
    } else {
      // Flush the codec buffers
      avcodec_flush_buffers(audioCodecContext);
    }
  }
  const int AUDIO_QUEUE_SIZE = SDL_GetAudioStreamAvailable(sdlAudioStream);
  cout << "AUDIO_QUEUE_SIZE: " << AUDIO_QUEUE_SIZE << endl;
  // log("Audio Queue Size: "+AUDIO_QUEUE_SIZE);
  // Clean up
  av_frame_free(&frame);
  av_packet_free(&pkt);
}

// Extract video processing into a dedicated function for clarity
bool MainWindow::processVideoPacket(AVPacket *pkt) {
  int sendResult = avcodec_send_packet(videoPlayer->codecContext, pkt);
  if (sendResult < 0) {
    return false;
  }

  int receiveResult =
      avcodec_receive_frame(videoPlayer->codecContext, videoPlayer->frame);
  if (receiveResult < 0) {
    return false;
  }

  // Successfully got a video frame - convert it
  sws_scale(videoPlayer->swsContext,
            (uint8_t const *const *)videoPlayer->frame->data,
            videoPlayer->frame->linesize, 0, videoPlayer->height,
            videoPlayer->frameRGB->data, videoPlayer->frameRGB->linesize);

  // Update both SDL texture and OpenGL texture
  // SDL_UpdateTexture(videoPlayer->texture, NULL,
  //                 videoPlayer->frameRGB->data[0],
  //                 videoPlayer->frameRGB->linesize[0]);

  glBindTexture(GL_TEXTURE_2D, videoPlayer->glTextureID);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, videoPlayer->width,
                  videoPlayer->height, GL_RGB, GL_UNSIGNED_BYTE,
                  videoPlayer->frameRGB->data[0]);

  // Update current time using frame PTS
  if (videoPlayer->frame->pts != AV_NOPTS_VALUE) {
    AVRational timeBase =
        videoPlayer->formatContext->streams[videoPlayer->videoStream]
            ->time_base;
    videoPlayer->currentTime = videoPlayer->frame->pts * av_q2d(timeBase);
  }

  return true;
}

// Extract audio processing into a dedicated function
void MainWindow::processAudioPacket(AVPacket *pkt) {
  AVFrame *audioFrame = av_frame_alloc();
  if (!audioFrame) {
    cerr << "Could not allocate audio frame" << endl;
    return;
  }

  int sendResult = avcodec_send_packet(audioCodecContext, pkt);
  if (sendResult < 0) {
    av_frame_free(&audioFrame);
    return;
  }

  // Try to receive multiple frames from this packet if available
  while (true) {
    int receiveResult = avcodec_receive_frame(audioCodecContext, audioFrame);
    if (receiveResult < 0) {
      // No more frames or error
      break;
    }

    // Process audio frame
    int outChannels = 2; // Stereo output

    // Calculate buffer size needed
    int outSamples = av_rescale_rnd(
        swr_get_delay(swrContext, audioCodecContext->sample_rate) +
            audioFrame->nb_samples,
        44100, // output rate
        audioCodecContext->sample_rate, AV_ROUND_UP);

    uint8_t *outBuf = nullptr;
    int outLinesize = 0;
    int allocResult = av_samples_alloc(&outBuf, &outLinesize, outChannels,
                                       outSamples, AV_SAMPLE_FMT_S16, 0);

    if (allocResult >= 0) {
      int convertedSamples = swr_convert(swrContext, &outBuf, outSamples,
                                         (const uint8_t **)audioFrame->data,
                                         audioFrame->nb_samples);

      if (convertedSamples >= 0) {
        int outBufSize = av_samples_get_buffer_size(
            nullptr, outChannels, convertedSamples, AV_SAMPLE_FMT_S16, 1);

        if (outBufSize > 0) {
          SDL_PutAudioStreamData(sdlAudioStream, outBuf, outBufSize);
        }
      }

      av_freep(&outBuf);
    }

    // Reset frame for reuse
    av_frame_unref(audioFrame);
  }

  av_frame_free(&audioFrame);
}

void MainWindow::updateMedia() {
  if (!videoPlayer->isPlaying || !videoPlayer->formatContext) {
    return; // Video not playing or not initialized
  }

  // Ensure audio device and context are ready if we have audio
  bool hasAudio = (audioCodecContext && swrContext && audioDeviceID != 0);

  // Check audio buffer status
  bool needMoreAudio = false;
  // Increase buffer sizes
  // const int MAX_AUDIO_QUEUE_SIZE = 8192 * 32;  // Increase from 8 to 32
  // const int IDEAL_AUDIO_QUEUE_SIZE = 8192 * 24; // Add ideal size
  // const int MIN_AUDIO_QUEUE_SIZE = 8192 * 16;   // Add minimum threshold
  const int IDEAL_AUDIO_QUEUE_SIZE =
      8192 * 16; // Increase buffer size for smoother playback
  const int MIN_AUDIO_QUEUE_SIZE =
      8192 * 4; // Minimum threshold to start refilling

  if (hasAudio) {
    const int AUDIO_QUEUE_SIZE = SDL_GetAudioStreamAvailable(sdlAudioStream);
    needMoreAudio = (AUDIO_QUEUE_SIZE < MIN_AUDIO_QUEUE_SIZE);
  }
  // const int MAX_AUDIO_QUEUE_SIZE = 8192 * 64;
  // const int IDEAL_AUDIO_QUEUE_SIZE = 8192 * 32;
  // const int MIN_AUDIO_QUEUE_SIZE = 8192 * 16;

  // if (hasAudio && SDL_GetAudioStreamAvailable(sdlAudioStream) <
  // MIN_AUDIO_QUEUE_SIZE) {
  //     for (int i = 0; i < 5; i++) {  // Process multiple packets
  //         updateAudio();
  //     }
  // }

  // Read multiple packets for audio to ensure buffer stays filled
  int packetsProcessed = 0;
  const int MAX_PACKETS_PER_CALL = hasAudio && needMoreAudio ? 5 : 1;
  bool videoFrameProcessed = false;

  while (packetsProcessed < MAX_PACKETS_PER_CALL && !videoFrameProcessed) {
    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
      cerr << "Could not allocate packet" << endl;
      break;
    }

    // Try to read a packet
    int readResult = av_read_frame(videoPlayer->formatContext, pkt);

    // Handle read errors or EOF
    if (readResult < 0) {
      av_packet_free(&pkt);

      if (readResult == AVERROR_EOF) {
        // End of file - seek back to beginning (for looping)
        int seekResult = av_seek_frame(videoPlayer->formatContext, -1, 0,
                                       AVSEEK_FLAG_BACKWARD);
        if (seekResult < 0) {
          char errbuf[256];
          av_strerror(seekResult, errbuf, sizeof(errbuf));
          cerr << "Error seeking to beginning: " << errbuf << endl;
        } else {
          // Flush the codec buffers
          if (videoPlayer->codecContext) {
            avcodec_flush_buffers(videoPlayer->codecContext);
          }
          if (audioCodecContext) {
            avcodec_flush_buffers(audioCodecContext);
          }
        }
      } else {
        char errbuf[256];
        av_strerror(readResult, errbuf, sizeof(errbuf));
        cerr << "Error reading frame: " << errbuf << endl;
      }
      break;
    }

    packetsProcessed++;

    // Process video packet
    if (pkt->stream_index == videoPlayer->videoStream && !videoFrameProcessed) {
      int sendResult = avcodec_send_packet(videoPlayer->codecContext, pkt);

      if (sendResult >= 0) {
        int receiveResult = avcodec_receive_frame(videoPlayer->codecContext,
                                                  videoPlayer->frame);

        if (receiveResult >= 0) {
          // Successfully got a video frame
          sws_scale(videoPlayer->swsContext,
                    (uint8_t const *const *)videoPlayer->frame->data,
                    videoPlayer->frame->linesize, 0, videoPlayer->height,
                    videoPlayer->frameRGB->data,
                    videoPlayer->frameRGB->linesize);

          // SDL_UpdateTexture(videoPlayer->texture, NULL,
          //                 videoPlayer->frameRGB->data[0],
          //                 videoPlayer->frameRGB->linesize[0]);

          glBindTexture(GL_TEXTURE_2D, videoPlayer->glTextureID);
          glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, videoPlayer->width,
                          videoPlayer->height, GL_RGB, GL_UNSIGNED_BYTE,
                          videoPlayer->frameRGB->data[0]);

          // Update current time using frame PTS
          if (videoPlayer->frame->pts != AV_NOPTS_VALUE) {
            AVRational timeBase =
                videoPlayer->formatContext->streams[videoPlayer->videoStream]
                    ->time_base;
            videoPlayer->currentTime =
                videoPlayer->frame->pts * av_q2d(timeBase);
          }

          videoFrameProcessed = true;
        }
      }
    }
    // Process audio packet
    else if (hasAudio && pkt->stream_index == audioStream) {
      // Always process audio packets when we encounter them,
      // but check if we need to keep processing more packets afterward
      AVFrame *audioFrame = av_frame_alloc();
      if (!audioFrame) {
        av_packet_unref(pkt);
        av_packet_free(&pkt);
        cerr << "Could not allocate audio frame" << endl;
        continue;
        // return;
      }

      int sendResult = avcodec_send_packet(audioCodecContext, pkt);
      // cout << "Send Result: " << sendResult << endl;
      if (sendResult >= 0) {
        // Try to receive multiple frames from this packet if available
        bool frameReceived = false;

        while (true) {
          int receiveResult =
              avcodec_receive_frame(audioCodecContext, audioFrame);
          // cout << "Terima Hasil: " << receiveResult << endl;
          if (receiveResult == AVERROR_EOF ||
              receiveResult == AVERROR(EAGAIN)) {
            // No more frames or error
            break;
          } else if (receiveResult < 0) {
            char errbuf[256];
            av_strerror(receiveResult, errbuf, sizeof(errbuf));
            cerr << "Error receiving audio frame: " << errbuf << endl;
            break;
          }

          frameReceived = true;

          // Process audio frame
          int outChannels = 2; // Stereo output

          // Calculate buffer size needed
          int outSamples = av_rescale_rnd(
              swr_get_delay(swrContext, audioCodecContext->sample_rate) +
                  audioFrame->nb_samples,
              44100, // output rate
              audioCodecContext->sample_rate, AV_ROUND_UP);

          uint8_t *outBuf = nullptr;
          int outLinesize = 0;
          int allocResult = av_samples_alloc(&outBuf, &outLinesize, outChannels,
                                             outSamples, AV_SAMPLE_FMT_S16, 0);
          // cout << "allocresult: " << allocResult << endl;

          if (allocResult >= 0) {
            int convertedSamples = swr_convert(
                swrContext, &outBuf, outSamples,
                (const uint8_t **)audioFrame->data, audioFrame->nb_samples);

            if (convertedSamples >= 0) {
              int outBufSize = av_samples_get_buffer_size(
                  nullptr, outChannels, convertedSamples, AV_SAMPLE_FMT_S16, 1);

              if (outBufSize > 0) {
                SDL_PutAudioStreamData(sdlAudioStream, outBuf, outBufSize);

                // Check if we've filled the audio buffer enough
                if (SDL_GetAudioStreamAvailable(sdlAudioStream) >=
                    IDEAL_AUDIO_QUEUE_SIZE) {
                  needMoreAudio = false;
                }
              }
            }

            av_freep(&outBuf);
          }

          // Reset frame for potential reuse
          av_frame_unref(audioFrame);
        }

        // If no frames were received, this is normal but uncommon
        if (!frameReceived) {
          // Sometimes packets don't yield frames immediately
        }
      }

      av_frame_free(&audioFrame);
    }

    av_packet_unref(pkt);
    av_packet_free(&pkt);

    // Check if we have enough audio data now
    if (hasAudio && !needMoreAudio && videoFrameProcessed) {
      cout << "Break Audio !!!" << endl;
      // We've both filled audio and processed a video frame
      break;
    }
  }

  // If audio is still needed, prioritize it on next call
  if (hasAudio) {
    const int AUDIO_QUEUE_SIZE = SDL_GetAudioStreamAvailable(sdlAudioStream);
    if (AUDIO_QUEUE_SIZE < MIN_AUDIO_QUEUE_SIZE) {
      cout << "AUDIO_QUEUE_SIZE: " << AUDIO_QUEUE_SIZE << endl;
      // Consider calling updateMedia again immediately or soon
      // via a callback/timer if your architecture supports it
    }
  }
}

void MainWindow::updateMediaFixedGlitch() {
  if (!videoPlayer->isPlaying || !videoPlayer->formatContext) {
    return; // Video not playing or not initialized
  }

  // Ensure audio device and context are ready if we have audio
  bool hasAudio = (audioCodecContext && swrContext && audioDeviceID != 0);

  // Audio buffer constants - increased for smoother playback
  const int MAX_AUDIO_QUEUE_SIZE = 8192 * 64;   // Maximum buffer size
  const int IDEAL_AUDIO_QUEUE_SIZE = 8192 * 32; // Target buffer level
  const int MIN_AUDIO_QUEUE_SIZE = 8192 * 16;   // Threshold to refill

  // Track if we need to prioritize audio processing
  bool needMoreAudio = false;
  if (hasAudio) {
    needMoreAudio =
        (SDL_GetAudioStreamAvailable(sdlAudioStream) < MIN_AUDIO_QUEUE_SIZE);
  }

  // Always ensure we have at least one video frame processed per call
  // to prevent video stuttering/glitching
  bool videoFrameProcessed = false;

  // Process more audio packets when buffer is low
  if (hasAudio && needMoreAudio) {
    // Pre-fill audio buffer if it's critically low
    for (int i = 0; i < 5 && SDL_GetAudioStreamAvailable(sdlAudioStream) <
                                 MIN_AUDIO_QUEUE_SIZE;
         i++) {
      updateAudio();
    }
  }

  // Determine how many packets to process this call
  // When audio buffer is low, process more packets to fill it
  // Otherwise, focus on consistent video frame timing
  int maxPacketsToProcess = hasAudio && needMoreAudio ? 5 : 2;
  int packetsProcessed = 0;

  while (packetsProcessed < maxPacketsToProcess || !videoFrameProcessed) {
    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
      cerr << "Could not allocate packet" << endl;
      break;
    }

    // Try to read a packet
    int readResult = av_read_frame(videoPlayer->formatContext, pkt);

    // Handle read errors or EOF
    if (readResult < 0) {
      av_packet_free(&pkt);

      if (readResult == AVERROR_EOF) {
        // End of file - seek back to beginning (for looping)
        int seekResult = av_seek_frame(videoPlayer->formatContext, -1, 0,
                                       AVSEEK_FLAG_BACKWARD);
        if (seekResult < 0) {
          char errbuf[256];
          av_strerror(seekResult, errbuf, sizeof(errbuf));
          cerr << "Error seeking to beginning: " << errbuf << endl;
        } else {
          // Flush the codec buffers
          if (videoPlayer->codecContext) {
            avcodec_flush_buffers(videoPlayer->codecContext);
          }
          if (audioCodecContext) {
            avcodec_flush_buffers(audioCodecContext);
          }
        }
      } else {
        char errbuf[256];
        av_strerror(readResult, errbuf, sizeof(errbuf));
        cerr << "Error reading frame: " << errbuf << endl;
      }
      break;
    }

    packetsProcessed++;

    // Process video packet - prioritize this to prevent glitching
    if (pkt->stream_index == videoPlayer->videoStream && !videoFrameProcessed) {
      if (processVideoPacket(pkt)) {
        videoFrameProcessed = true;
      }
    }
    // Process audio packet
    else if (hasAudio && pkt->stream_index == audioStream) {
      processAudioPacket(pkt);

      // Check if we have enough audio data now
      if (SDL_GetAudioStreamAvailable(sdlAudioStream) >=
          IDEAL_AUDIO_QUEUE_SIZE) {
        needMoreAudio = false;
      }
    }

    av_packet_unref(pkt);
    av_packet_free(&pkt);

    // Stop processing packets if we've processed a video frame and have
    // enough audio
    if (videoFrameProcessed &&
        (!hasAudio || !needMoreAudio ||
         SDL_GetAudioStreamAvailable(sdlAudioStream) >= MIN_AUDIO_QUEUE_SIZE)) {
      break;
    }

    // Safety measure - if we've processed too many packets without getting a
    // video frame, break to prevent potential freezing (usually indicates a
    // problem with the stream)
    if (packetsProcessed > 20 && !videoFrameProcessed) {
      cerr << "Warning: Processed 20 packets without finding a video frame"
           << endl;
      break;
    }
  }

  // Debug output for audio buffer status
  if (hasAudio) {
    const int AUDIO_QUEUE_SIZE = SDL_GetAudioStreamAvailable(sdlAudioStream);
    if (AUDIO_QUEUE_SIZE < MIN_AUDIO_QUEUE_SIZE) {
      cout << "Low audio buffer: " << AUDIO_QUEUE_SIZE << " bytes" << endl;
    }
  }
}

void MainWindow::renderVideoPlayer() {
  static char videoPath[256] = "";
  static bool loopVideo = true;
  static bool paused = false;
  static bool isOnlyRender = isOnlyRenderImage;
  static bool isOnlyAd = isOnlyAudio;

  ui::Begin("Video Player", nullptr, ImGuiWindowFlags_NoCollapse);

  // Input file video
  ui::InputText("Video File", videoPath, IM_ARRAYSIZE(videoPath));
  ui::SameLine();
  if (ui::Button("Open")) {
    if (strlen(videoPath) > 0) {
      openVideo(videoPath);
      paused = false;
    }
  }

  ui::Checkbox("Loop Video", &loopVideo);
  ui::SameLine();
  ui::Checkbox("Only Render Image", &isOnlyRender);
  ui::SameLine();
  ui::Checkbox("Only Audio", &isOnlyAd);

  if (vulkanHandler.isPlaying) {
    // Update frame and audio if not paused
    if (!paused) {
      vulkanHandler.updateBothVideoAndAudio();
    }

    // Render video frame
    renderVideoFrame();

    // Kontrol video
    ui::Separator();
    if (ui::Button(paused ? "Play" : "Pause")) {
      paused = !paused;
    }
    ui::SameLine();
    if (ui::Button("Stop")) {
      // Logic for stop (e.g. seek to 0 and pause)
      paused = true;
    }

    ui::Text("Time: %.2f / %.2f", (float)vulkanHandler.currentTime,
             (float)vulkanHandler.duration);

    // Informasi video
    ui::Text("Resolution: %dx%d | FPS: %.2f", vulkanHandler.width,
             vulkanHandler.height, (float)vulkanHandler.fps);
    if (vulkanHandler.audioCodecContext) {
      ui::Separator();
      ui::Text("Frequency: %d | Channels: %d",
               vulkanHandler.audioCodecContext->sample_rate,
               vulkanHandler.audioCodecContext->ch_layout.nb_channels);
    }

  } else {
    ui::Text("No video loaded. Please open a video file.");
  }

  ui::End();
}

// Tambahkan ini di renderVideoFrame() untuk debug
void MainWindow::renderVideoFrame() {
  ImTextureID tex = (ImTextureID)vulkanHandler.getVideoDescriptorSet();
  if (tex) {
    // Hitung rasio aspek
    float aspectRatio = static_cast<float>(vulkanHandler.width) /
                        static_cast<float>(vulkanHandler.height);

    // Hitung dimensi tampilan
    ImVec2 contentSize = ui::GetContentRegionAvail();
    float displayWidth = contentSize.x;
    float displayHeight = displayWidth / aspectRatio;

    if (displayHeight > contentSize.y) {
      displayHeight = contentSize.y;
      displayWidth = displayHeight * aspectRatio;
    }

    // Tampilkan texture menggunakan ImGui Vulkan
    ui::Image(tex, ImVec2(displayWidth, displayHeight));
  } else {
    ui::Text("Failed to render video frame.");
  }
}

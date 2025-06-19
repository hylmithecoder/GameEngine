#pragma once
// #include <glad/glad.h>
// #define WIN32_LEAN_AND_MEAN
// #include <winsock2.h>
// #include <windows.h>
// #include <ws2tcpip.h>

#include <SDL.h>
#include <SDL_syswm.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <imgui_impl_opengl3.h>
#include <SDL_ttf.h>
#include <vector>
#include <SceneRenderer2D.hpp>
#include <NetworkManager.hpp>
#include <gtk/gtk.h>
#include <glib.h>
// #include <Application.hpp>
#include "VideoPlayer.hpp" // Include the header file for VideoPlayer
#include <assets.hpp> // Include the header file for assets
// #include <SDL_opengl.h>
// #include <GLFW/glfw3.h>
// #include <GLES2/gl2.h>
// #include <FFmpegWrapper.hpp>
#include <list>
#include "IconsFontAwesome6.h"
#include <HandlerProject.hpp> // Include the header file for HandlerProject
#define IMGUI_HAS_DOCK
#define IMGUI_HAS_VIEWPORT
// #include "SimpleRenderer.hpp"

class MainWindow {
private:
    bool showSecondary;
    // char objectName[128] = "Player";
    // float position[3] = {0.0f, 0.0f, 0.0f};
    
    bool showConfirmDialog(const char* message, const char* title);
    void set_window_icon();
    void setTheme(bool dark);
    bool openVideo(const char* filePath);
    bool updateVideoFrame();
    bool updateVideoFrameWithOpenGL();
    bool openAudio();
    void updateAudio();
    void updateMedia();
    void updateMediaFixedGlitch();
    void renderVideoPlayer();
    void set_extension_icon();
    void renderVideoFrame();
    void set_mainbackground();

    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_GLContext glContext;
    TTF_Font* font;

    bool isRunning;
    bool fullscreen;
    int windowWidth;
    int windowHeight;
    bool darkTheme;
    int currentTab;

    // Opengl 
    GLuint textureID;
    GLuint vertexArrayID;
    GLuint vertexBufferID;
    GLuint uvBufferID;
    GLuint programID;
    GLuint textureUniformID;
    
    // Object properties for Inspector
    char objectName[64] = "Player";
    float position[3] = { 0.0f, 0.0f, 0.0f };
    float rotation[3] = { 0.0f, 0.0f, 0.0f };
    float scale[3] = { 1.0f, 1.0f, 1.0f };
    static constexpr float MIN_PANEL_WIDTH = 100.0f;
    float explorerSplitPosition = 200.0f;

    int audioStream = -1;
    int videoStream = -1;
    AVCodecContext* audioCodecContext = nullptr;
    AVCodecContext* videoCodecContext = nullptr;
    AVChannelLayout audioChannelLayout;
    SDL_AudioDeviceID audioDeviceID = 0;
    SDL_AudioSpec audioSpec;
    SDL_Texture* convertFrameToTexture(AVFrame* frame, SDL_Renderer* renderer, SwsContext* swsCtx);

    // void InitRocket();
    // void LoadDocument(const char* url);
    VideoPlayer* videoPlayer; // Assuming VideoPlayer is a class that handles video playback

public:
    MainWindow(const char* title, int width = 1280, int height = 720);
    ~MainWindow();
        
    SceneRenderer2D* sceneRenderer2D = nullptr;
    HandlerProject projectHandler;
    TextureData backgroundTexture;
    Color backgroundColor;
    SwrContext* swrContext = nullptr;
    bool isOnlyAudio = false;
    bool isOnlyRenderImage = false;
    bool firstOpenProject;
    bool init(const char* title);
    bool running() const { return isRunning; }    
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
        "Welcome to Ilmeee Editor",
        "This is a log message",
        "You can see logs here",
        "Enjoy your game dev experience!"
    };
    std::vector<std::string> messages;
    std::mutex messagesMutex;
    static const size_t MAX_MESSAGES = 1000;
    string currentMessageFrom27015 = "";
    std::unique_ptr<NetworkManager> networkManager;
    // list<string> currentName = {"Shiroko", "Shun_Small"};

    enum CurrentBackground 
    {
        Shiroko, 
        Shun_Small,
        Hanako_Swimsuit,
        Background_Count
    };
    CurrentBackground currentBg = static_cast<CurrentBackground>(currentBgInt);
    const char* backgroundOptions[3] = {
        "Shiroko",
        "Shun (Small)",
        "Hanako (Swimsuit)"
    };
    GLuint blurFBO, blurTexture;
    int screenWidth, screenHeight;
    int blurWidth = screenWidth / 4;
    int blurHeight = screenHeight / 4;
   
    bool processVideoPacket(AVPacket* pkt);
    void processAudioPacket(AVPacket* pkt);
    void HandleUpdateBackground(CurrentBackground currentBg);
    void handleEvents();
    void update();
    void render();
    void clean();
    void log(const char* message);
    void RenderExplorerWindow(HandlerProject::AssetFile projectRoot, HandlerProject::AssetFile assetFolder, const std::string& assetPath , bool isOpenProject);
    void RenderInspectorWindow();
    void RenderMainViewWindow();
    void RenderConsoleWindow();
    void RenderMenuBar();
    void HandleBackground(const ImVec2& windowPos, const ImVec2& windowSize);
    void HandleSearch();
    void RenderViewportToolbar();
    void RenderGameViewport();
    void HandleViewportInteraction(ImVec2 viewportPos, ImVec2 viewportSize);
    void RenderSceneWindow();
    void RenderHierarchyWindow();
    void RenderSceneToolbarView(ImVec2 viewportPos, ImVec2 viewportSize);
    void RenderPlayMenu();
    void PushMessage(const std::string& message);
    
    const std::vector<std::string>& getMessages() const {
        return messages;
    }
    void ClearMessages();
};
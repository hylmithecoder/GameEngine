#pragma once
#include <SDL.h>
#include <SDL_syswm.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <SDL_ttf.h>
#include "VideoPlayer.hpp" // Include the header file for VideoPlayer
#include <assets.hpp>
#include <SDL_opengl.h>
#include <GLFW/glfw3.h>
#include <GLES2/gl2.h>
#include <list>
#include "IconsFontAwesome6.h"
#include <HandlerProject.hpp>
#define IMGUI_HAS_DOCK
#define IMGUI_HAS_VIEWPORT
// #include "SimpleRenderer.hpp"

class MainWindow {
private:
    bool showSecondary;
    // char objectName[128] = "Player";
    // float position[3] = {0.0f, 0.0f, 0.0f};
    
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
    HandlerProject projectHandler;

public:
    MainWindow(const char* title, int width = 1280, int height = 720);
    ~MainWindow();
        
    TextureData backgroundTexture;
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
    // list<string> currentName = {"Shiroko", "Shun_Small"};

    enum CurrentBackground 
    {
        Shiroko, 
        Shun_Small,
        Background_Count
    };
    CurrentBackground currentBg = static_cast<CurrentBackground>(currentBgInt);
    const char* backgroundOptions[2] = {
        "Shiroko",
        "Shun (Small)"
    };
   
    bool processVideoPacket(AVPacket* pkt);
    void processAudioPacket(AVPacket* pkt);
    void HandleUpdateBackground(CurrentBackground currentBg);
    void handleEvents();
    void update();
    void render();
    void clean();
    void log(const char* message);
    void RenderExplorerWindow(HandlerProject::AssetFile assetRoot, bool isOpenProject);
    void RenderInspectorWindow();
    void RenderMainViewWindow();
    void RenderConsoleWindow();
    void RenderMenuBar();
    void HandleBackground();
    void HandleSearch();
};
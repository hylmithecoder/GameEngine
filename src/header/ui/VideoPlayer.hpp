#pragma once
#include <string>
#include <SDL.h>
// #include <GLFW/glfw3.h>
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "imgui_impl_opengl3.h"
// #include <SDL_opengl.h>
// #include <TextureManager.hpp>
#include <glad/glad.h>
#include <SDL_audio.h>

// FFmpeg includes
extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
    #include <libavutil/imgutils.h>
    #include <libswresample/swresample.h>
    #include <libavutil/channel_layout.h>   // untuk av_get_default_channel_layout
    #include <libavutil/samplefmt.h> 
    #include <libavutil/opt.h>
    #include <libavutil/time.h>
    // #include <libavutil/timestamp.h>
    // #include <libavcodec/aptx.h>
}

class VideoPlayer {
public:
    // Video-related members
    AVFormatContext* formatContext = nullptr;
    AVCodecContext* codecContext = nullptr;
    SwsContext* swsContext = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* frameRGB = nullptr;
    AVFrame* hw_frame = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* lastGoodFrameRGB;
    bool hasValidFrame = false;
    SDL_Texture* texture = nullptr;
    int videoStream = -1;
    bool isPlaying = false;
    bool isPlayingAudio = false;
    std::string filePath;
    int width = 0;
    int height = 0;
    float duration = 0;
    float currentTime = 0;
    float volume = 0;
    double fps = 0;
    uint8_t* buffer = nullptr;

    // Audio-related members
    AVCodecContext* audioCodecContext = nullptr;
    // AVBufferRef* hw_device_ctx;
    int audioStream = -1;
    SDL_AudioDeviceID audioDeviceID = 0;
    SwrContext* swrContext = nullptr;
    SDL_AudioSpec audioSpec;
    SDL_Texture* videoTexture = nullptr;    
    GLuint glTextureID;
    
    VideoPlayer();
    ~VideoPlayer();
    
    bool open(const std::string& filePath, SDL_Renderer* renderer);
    bool updateFrame();
    void render(SDL_Renderer* renderer);
    void cleanup();
    SDL_Texture* convertFrameToTexture(AVFrame* frame, SDL_Renderer* renderer, SwsContext* swsCtx);
    bool openAudio();
    void updateAudio(); 
    
    // New audio control methods
    void setVolume(int volume); // 0-100 range
    void pauseAudio(bool pause);
};
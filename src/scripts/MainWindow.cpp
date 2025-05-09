#include "../header/MainWindow.hpp"
#include "../header/SecondaryWindow.hpp"
#include <string>
#include <vector>
#include <iostream>
#include <memory>
#include "FFmpegWrapper.hpp"
#include <fstream>
#include <libavformat/avformat.h>
#include <stb_image.h>
#include <SDL_opengl.h>
#include "assets.hpp"
#include <backends/imgui_impl_opengl3.h> // Include the OpenGL3 backend header
using namespace std;

MainWindow::MainWindow(const char* title, int width, int height)
    : window(nullptr), renderer(nullptr), glContext(nullptr), font(nullptr),
      isRunning(false), windowWidth(width), windowHeight(height),
      fullscreen(false), showSecondary(false),
      darkTheme(true), currentTab(0), videoPlayer(new VideoPlayer()) {
    
    if (init(title)) {
        isRunning = true;
    }
}

MainWindow::~MainWindow() {
    if (videoPlayer) {
        videoPlayer->cleanup();
        delete videoPlayer;
    }
    clean();
}

void MainWindow::set_window_icon() {
    SDL_Surface* icon = SDL_LoadBMP("assets/icons/app_icon.bmp");
    if (icon) {
        SDL_SetWindowIcon(window, icon);
        SDL_FreeSurface(icon);
    } else {
        cerr << "Warning: Could not load icon: " << SDL_GetError() << endl;
    }
}

bool MainWindow::init(const char* title) {
    // Inisialisasi SDL dengan dukungan video dan audio
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        cerr << "SDL could not initialize! SDL Error: " << SDL_GetError() << endl;
        return false;
    }

    // Inisialisasi FFmpeg
    #if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58, 9, 100)
        av_register_all();
    #endif

    // Membuat window dengan judul yang diberikan
    window = SDL_CreateWindow(title ? title : "GameEngine Studio",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        windowWidth, windowHeight, 
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    
    if (!window) {
        cerr << "Window could not be created! SDL Error: " << SDL_GetError() << endl;
        return false;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        cerr << "Renderer could not be created! SDL Error: " << SDL_GetError() << endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    // Aktifkan OpenGL
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        std::cerr << "OpenGL context could not be created! SDL Error: " << SDL_GetError() << std::endl;
        return false;
    }
    SDL_GL_SetSwapInterval(1);  // VSync

    // AVHWDeviceType type = av_hwdevice_find_type_by_name("cuda");
    // av_hwdevice_ctx_create(&videoPlayer->hw_device_ctx, type, NULL, NULL, 0);
    // videoPlayer->codecContext->hw_device_ctx = av_buffer_ref(videoPlayer->hw_device_ctx);


    // Set window icon
    set_window_icon();

    // set_mainbackground();

    // Inisialisasi ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    
    string fontPath = "assets/fonts/zh-cn.ttf";
    io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 16.0f);

    ifstream fontCheck(fontPath.c_str());
    if (!fontCheck.good()){
        cerr << "Font not found at: "<< fontPath <<"\n";
    }
    else {
        cout << "Font found at:"<< fontPath <<"\n";
    }

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    #ifdef ImGuiConfigFlags_DockingEnable
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    #else
        cerr << "Warning: Docking is not supported in this version of Dear ImGui." << endl;
    #endif
    
    // Set theme
    // ImGui::StyleColorsDark();
    setTheme(darkTheme);
    
    // ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    // ImGui_ImplSDLRenderer2_Init(renderer);
    cout << glContext << endl;
    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 130");

    CurrentBackground currentBg = Shiroko;
    // HandleBackground(currentBg);
    assets.LoadTextureFromFile("assets/images/backgrounds/shiroko_bluearchive.jpg", &backgroundTexture);
    // set_mainbackground();

    return true;
}

void MainWindow::HandleUpdateBackground(CurrentBackground currentBg)
{
    const char* path = nullptr;

    switch (currentBg)
    {
        case Shiroko:
            path = "assets/images/backgrounds/shiroko_bluearchive.jpg";
            cout << "Current Background is: Shiroko" << endl;
            break;
        case Shun_Small:
            path = "assets/images/backgrounds/shun_small.webp";
            cout << "Current Background is: Shun" << endl;
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
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        
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
        
        ImVec4* colors = style.Colors;
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
        ImGui::StyleColorsLight();
        ImGuiStyle& style = ImGui::GetStyle();
        
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
        
        ImVec4* colors = style.Colors;
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

bool MainWindow::openVideo(const char* filePath) {
    // Bersihkan video sebelumnya
    if (videoPlayer->isPlaying) {
        videoPlayer->cleanup();
    }
    
    videoPlayer->filePath = filePath;
    // AVHWDeviceType type = av_hwdevice_find_type_by_name("cuda");
    // av_hwdevice_ctx_create(&videoPlayer->hw_device_ctx, type, NULL, NULL, 0);
    // videoPlayer->codecContext->hw_device_ctx = av_buffer_ref(videoPlayer->hw_device_ctx);
    
    // Buka file video
    if (avformat_open_input(&videoPlayer->formatContext, filePath, NULL, NULL) != 0) {
        cerr << "[MainWindow] Could not open video file: " << filePath << endl;
        return false;
    }
    else {
        cout << "[MainWindow] Video file opened: " << filePath << endl;
    }
    
    // Mendapatkan informasi stream
    if (avformat_find_stream_info(videoPlayer->formatContext, NULL) < 0) {
        cerr << "Could not find stream information" << endl;
        videoPlayer->cleanup();
        return false;
    }
    else {
        cout << "[MainWindow] Video stream information found " << videoPlayer->formatContext << endl;
    }
    
    // Cari video stream
    cout << "[MainWindow] Searching for video stream" << endl;
    videoPlayer->videoStream = -1;
    for (unsigned int i = 0; i < videoPlayer->formatContext->nb_streams; i++) {
        if (videoPlayer->formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoPlayer->videoStream = i;
            break;
        }
    }
    
    cout << "[MainWindow] Video stream found: " << videoPlayer->videoStream << endl;
    if (videoPlayer->videoStream == -1) {
        cerr << "Could not find video stream" << endl;
        videoPlayer->cleanup();
        return false;
    }
    
    // Mendapatkan codec
    cout << "[MainWindow] Getting codec" << endl;
    const AVCodec *codec = avcodec_find_decoder(
        videoPlayer->formatContext->streams[videoPlayer->videoStream]->codecpar->codec_id);
    if (!codec) {
        cerr << "Unsupported codec" << endl;
        videoPlayer->cleanup();
        return false;
    }

    // Mendapatkan codec dengan prioritas mendapatkan codec CUDA untuk GTX 1070
    // cout << "[MainWindow] Getting codec with GPU acceleration" << endl;
    // const AVCodec *codec = nullptr;
    // void *iter = nullptr;
    // // Cari decoder hardware CUDA yang kompatibel dengan GTX 1070
    // while (const AVCodec *c = av_codec_iterate(&iter)) {
    //     if (av_codec_is_decoder(c) && c->id == videoPlayer->formatContext->streams[videoPlayer->videoStream]->codecpar->codec_id) {
    //         if (strstr(c->name, "_cuvid")) { // cari decoder NVIDIA CUDA/NVDEC
    //             codec = c;
    //             cout << "[MainWindow] Found CUDA decoder: " << c->name << endl;
    //             break;
    //         }
    //     }
    // }
    
    // // Fallback ke decoder software jika tidak ditemukan decoder CUDA
    // if (!codec) {
    //     cout << "[MainWindow] CUDA decoder not found, falling back to software decoder" << endl;
    //     codec = avcodec_find_decoder(
    //         videoPlayer->formatContext->streams[videoPlayer->videoStream]->codecpar->codec_id);
    //     if (!codec) {
    //         cerr << "Unsupported codec" << endl;
    //         videoPlayer->cleanup();
    //         return false;
    //     }
    // }
    // cout << "Using Devices: " << codec->name << endl;
    
    // Alokasi context codec
    cout << "[MainWindow] Allocating codec context" << endl;
    videoPlayer->codecContext = avcodec_alloc_context3(codec);
    if (!videoPlayer->codecContext) {
        cerr << "Could not allocate codec context" << endl;
        videoPlayer->cleanup();
        return false;
    }
    
    // Copy parameters dari stream ke codec context
    cout << "[MainWindow] Copying codec parameters" << endl;
    if (avcodec_parameters_to_context(videoPlayer->codecContext, 
        videoPlayer->formatContext->streams[videoPlayer->videoStream]->codecpar) < 0) {
        cerr << "Could not copy codec parameters" << endl;
        videoPlayer->cleanup();
        return false;
    }
    
    // Buka codec
    cout << "[MainWindow] Opening codec" << endl;
    if (avcodec_open2(videoPlayer->codecContext, codec, NULL) < 0) {
        cerr << "Could not open codec" << endl;
        videoPlayer->cleanup();
        return false;
    }

    // Atur context untuk hardware acceleration
    // if (videoPlayer->hw_device_ctx) {
    //     videoPlayer->codecContext->hw_device_ctx = av_buffer_ref(videoPlayer->hw_device_ctx);
    //     cout << "[MainWindow] Hardware acceleration context set" << endl;
    // }
    
    // Alokasi frame
    cout << "[MainWindow] Allocating frames" << endl;
    videoPlayer->frame = av_frame_alloc();
    videoPlayer->frameRGB = av_frame_alloc();
    // videoPlayer->hw_frame = av_frame_alloc();

    if (!videoPlayer->frame || !videoPlayer->frameRGB /*|| !videoPlayer->hw_frame*/) {
        cerr << "Could not allocate frames" << endl;
        videoPlayer->cleanup();
        return false;
    }
    
    // Determine required buffer size and allocate buffer
    cout << "[MainWindow] Determining buffer size and allocating buffer" << endl;
    videoPlayer->width = videoPlayer->codecContext->width;
    videoPlayer->height = videoPlayer->codecContext->height;
    
    cout << "[MainWindow] Video size: " << videoPlayer->width << "x" << videoPlayer->height << endl;
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, videoPlayer->width, 
                                          videoPlayer->height, 1);
    videoPlayer->buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
    
    av_image_fill_arrays(videoPlayer->frameRGB->data, videoPlayer->frameRGB->linesize, 
                        videoPlayer->buffer, AV_PIX_FMT_RGB24,
                        videoPlayer->width, videoPlayer->height, 1);
    
    videoPlayer->lastGoodFrameRGB = av_frame_alloc();
    av_image_alloc(videoPlayer->lastGoodFrameRGB->data, videoPlayer->lastGoodFrameRGB->linesize,
                  videoPlayer->width, videoPlayer->height, AV_PIX_FMT_RGB24, 1);
                  videoPlayer->hasValidFrame = false;
    
    // Initialize SWS context for software scaling
    cout << "[MainWindow] Initializing SWS context" << endl;
    videoPlayer->swsContext = sws_getContext(
        videoPlayer->width, videoPlayer->height, videoPlayer->codecContext->pix_fmt,
        videoPlayer->width, videoPlayer->height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, NULL, NULL, NULL);
    
    if (!videoPlayer->swsContext) {
        cerr << "Could not initialize SWS context" << endl;
        videoPlayer->cleanup();
        return false;
    }
    
    // Buat texture untuk render di SDL
    cout << "[MainWindow] Creating SDL texture" << endl;
    videoPlayer->texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
        videoPlayer->width, videoPlayer->height);
    
    if (!videoPlayer->texture) {
        cerr << "Could not create SDL texture: " << SDL_GetError() << endl;
        videoPlayer->cleanup();
        return false;
    }
    
    glGenTextures(1, &videoPlayer->glTextureID);
    glBindTexture(GL_TEXTURE_2D, videoPlayer->glTextureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, videoPlayer->width, videoPlayer->height,
                0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    
    
    // Baca frame rate
    AVRational frameRate = videoPlayer->formatContext->streams[videoPlayer->videoStream]->avg_frame_rate;
    videoPlayer->fps = (double)frameRate.num / (double)frameRate.den;

    videoPlayer->currentTime = 0;
    
    // Baca durasi video
    videoPlayer->duration = videoPlayer->formatContext->duration / 1000000.0;
    
    
    // Alokasi packet
    cout << "[MainWindow] Allocating packet" << endl;
    videoPlayer->packet = av_packet_alloc();
    if (!videoPlayer->packet) {
        cerr << "Could not allocate packet" << endl;
        videoPlayer->cleanup();
        return false;
    }

    // glGenTextures(1, &videoPlayer->glTextureID);
    // glBindTexture(GL_TEXTURE_2D, videoPlayer->glTextureID);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, videoPlayer->width, videoPlayer->height,
    //             0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);


    // Note Kalau Asal Ubah bisa error tanpa Log
    cout << "[MainWindow] Opening audio" << endl;
    videoPlayer->audioStream = av_find_best_stream(videoPlayer->formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    cout << "[MainWindow] Audio stream: " << videoPlayer->audioStream << endl;
    // videoPlayer->openAudio();
    // videoPlayer->updateAudio();
    cout << "[MainWindow] Audio opened\n";
    audioStream = av_find_best_stream(videoPlayer->formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (audioStream >= 0) {
        if (!openAudio()) {
            cerr << "[MainWindow]Warning: Gagal inisialisasi audio\n";
        }
        else {            
            videoPlayer->openAudio();
        }
    } else {
        cerr << "No audio stream found\n";
    }


    videoPlayer->isPlaying = true;
    cout << "[MainWindow]Video player ready\n";
    return true;
}

bool MainWindow::updateVideoFrameWithOpenGL() {
    if (!videoPlayer->isPlaying) return false;

    int frameFinished = 0;
    int readAttempts = 0;
    const int MAX_READ_ATTEMPTS = 10;

    while (!frameFinished && readAttempts < MAX_READ_ATTEMPTS) {
        readAttempts++;
        int readResult = av_read_frame(videoPlayer->formatContext, videoPlayer->packet);
        if (readResult < 0) {
            if (readResult == AVERROR_EOF) {
                av_seek_frame(videoPlayer->formatContext, videoPlayer->videoStream, 0, AVSEEK_FLAG_BACKWARD);
                continue;
            } else {
                SDL_Delay(5);
                continue;
            }
        }

        if (videoPlayer->packet->stream_index == videoPlayer->videoStream) {
            int sendResult = avcodec_send_packet(videoPlayer->codecContext, videoPlayer->packet);
            av_packet_unref(videoPlayer->packet);

            if (sendResult < 0) continue;

            int receiveResult = avcodec_receive_frame(videoPlayer->codecContext, videoPlayer->frame);
            if (receiveResult < 0) {
                if (receiveResult == AVERROR(EAGAIN)) continue;
                else if (receiveResult == AVERROR_EOF) break;
                else continue;
            } else {
                frameFinished = 1;
                break;
            }
        } else {
            av_packet_unref(videoPlayer->packet);
        }
    }

    if (frameFinished) {
        sws_scale(videoPlayer->swsContext, (uint8_t const* const*)videoPlayer->frame->data,
                  videoPlayer->frame->linesize, 0, videoPlayer->height,
                  videoPlayer->frameRGB->data, videoPlayer->frameRGB->linesize);

        SDL_UpdateTexture(videoPlayer->texture, NULL, videoPlayer->frameRGB->data[0],
                          videoPlayer->frameRGB->linesize[0]);

        glBindTexture(GL_TEXTURE_2D, videoPlayer->glTextureID);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, videoPlayer->width, videoPlayer->height,
                GL_RGB, GL_UNSIGNED_BYTE, videoPlayer->frameRGB->data[0]);


        // Update current time using frame PTS
        if (videoPlayer->frame->pts != AV_NOPTS_VALUE) {
            AVRational timeBase = videoPlayer->formatContext->streams[videoPlayer->videoStream]->time_base;
            videoPlayer->currentTime = videoPlayer->frame->pts * av_q2d(timeBase);
        }

        return true;
    }

    return false;
}

bool MainWindow::updateVideoFrame() {
    if (!videoPlayer->isPlaying) return false;

    int frameFinished = 0;
    int readAttempts = 0;
    const int MAX_READ_ATTEMPTS = 10;

    while (!frameFinished && readAttempts < MAX_READ_ATTEMPTS) {
        readAttempts++;
        int readResult = av_read_frame(videoPlayer->formatContext, videoPlayer->packet);
        if (readResult < 0) {
            if (readResult == AVERROR_EOF) {
                av_seek_frame(videoPlayer->formatContext, videoPlayer->videoStream, 0, AVSEEK_FLAG_BACKWARD);
                continue;
            } else {
                SDL_Delay(5);
                continue;
            }
        }

        if (videoPlayer->packet->stream_index == videoPlayer->videoStream) {
            int sendResult = avcodec_send_packet(videoPlayer->codecContext, videoPlayer->packet);
            av_packet_unref(videoPlayer->packet);

            if (sendResult < 0) continue;

            int receiveResult = avcodec_receive_frame(videoPlayer->codecContext, videoPlayer->frame);
            if (receiveResult < 0) {
                if (receiveResult == AVERROR(EAGAIN)) continue;
                else if (receiveResult == AVERROR_EOF) break;
                else continue;
            } else {
                frameFinished = 1;
                break;
            }
        } else {
            av_packet_unref(videoPlayer->packet);
        }
    }

    if (frameFinished) {
        sws_scale(videoPlayer->swsContext, (uint8_t const* const*)videoPlayer->frame->data,
                  videoPlayer->frame->linesize, 0, videoPlayer->height,
                  videoPlayer->frameRGB->data, videoPlayer->frameRGB->linesize);

        SDL_UpdateTexture(videoPlayer->texture, NULL, videoPlayer->frameRGB->data[0],
                          videoPlayer->frameRGB->linesize[0]);

        // Update current time using frame PTS
        if (videoPlayer->frame->pts != AV_NOPTS_VALUE) {
            AVRational timeBase = videoPlayer->formatContext->streams[videoPlayer->videoStream]->time_base;
            videoPlayer->currentTime = videoPlayer->frame->pts * av_q2d(timeBase);
        }

        return true;
    }

    return false;
}

bool MainWindow::openAudio() {
    // Find the best audio stream
    audioStream = av_find_best_stream(videoPlayer->formatContext, AVMEDIA_TYPE_AUDIO,
                                      -1, -1, nullptr, 0);
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

    // Set up input channel layout (using the codec context)
    AVChannelLayout in_layout = audioCodecContext->ch_layout;
    
    // Set up output channel layout (stereo)
    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, 2); // stereo
    
    // Store the output layout for later use
    av_channel_layout_copy(&audioChannelLayout, &out_layout);

    // Configure the resampler
    av_opt_set_chlayout(swrContext, "in_chlayout", &in_layout, 0);
    av_opt_set_chlayout(swrContext, "out_chlayout", &out_layout, 0);
    av_opt_set_int(swrContext, "in_sample_rate", audioCodecContext->sample_rate, 0);
    av_opt_set_int(swrContext, "out_sample_rate", 44100, 0); // Standard output rate
    av_opt_set_sample_fmt(swrContext, "in_sample_fmt", audioCodecContext->sample_fmt, 0);
    av_opt_set_sample_fmt(swrContext, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

    // Initialize the resampler
    int ret = swr_init(swrContext);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        cerr << "Could not initialize the resampler: " << errbuf << endl;
        swr_free(&swrContext);
        avcodec_free_context(&audioCodecContext);
        return false;
    }
    cout << "SWR Context: " << swrContext << endl;
    videoPlayer->swrContext = swrContext;

    // Open SDL audio device
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = 44100; // Match our resampler output rate
    want.format = AUDIO_S16SYS;
    want.channels = 2; // Stereo output
    want.samples = 1024;
    want.callback = nullptr; // We'll use SDL_QueueAudio instead

    audioDeviceID = SDL_OpenAudioDevice(nullptr, 0, &want, &audioSpec, 
                                       SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | 
                                       SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    
    if (audioDeviceID == 0) {
        cerr << "Failed to open audio device: " << SDL_GetError() << endl;
        swr_free(&swrContext);
        avcodec_free_context(&audioCodecContext);
        return false;
    }

    // Start audio playback
    SDL_PauseAudioDevice(audioDeviceID, 0);
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
    const int MAX_AUDIO_QUEUE_SIZE = 8192 * 8; // About 0.5 seconds of stereo audio
    if (SDL_GetQueuedAudioSize(audioDeviceID) > MAX_AUDIO_QUEUE_SIZE) {
        return; // Enough audio is queued, don't add more yet
    }

    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        cerr << "Could not allocate packet" << endl;
        return;
    }

    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        cerr << "Could not allocate frame" << endl;
        av_packet_free(&pkt);
        return;
    }

    bool packetProcessed = false;
    int readResult;

    // Keep reading frames until we process an audio packet or reach end of file
    while (!packetProcessed && (readResult = av_read_frame(videoPlayer->formatContext, pkt)) >= 0) {
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
                    swr_get_delay(swrContext, audioCodecContext->sample_rate) + frame->nb_samples,
                    44100, // output rate
                    audioCodecContext->sample_rate,
                    AV_ROUND_UP
                );

                // Allocate output buffer
                uint8_t* outBuf = nullptr;
                int outLinesize = 0;
                int allocResult = av_samples_alloc(
                    &outBuf, &outLinesize,
                    outChannels,
                    outSamples,
                    AV_SAMPLE_FMT_S16, 0
                );
                
                if (allocResult < 0) {
                    char errbuf[256];
                    av_strerror(allocResult, errbuf, sizeof(errbuf));
                    cerr << "Could not allocate audio output buffer: " << errbuf << endl;
                    break;
                }

                // Convert audio samples
                int convertedSamples = swr_convert(
                    swrContext,
                    &outBuf, outSamples,
                    (const uint8_t**)frame->data, frame->nb_samples
                );
                
                if (convertedSamples < 0) {
                    char errbuf[256];
                    av_strerror(convertedSamples, errbuf, sizeof(errbuf));
                    cerr << "Error converting audio: " << errbuf << endl;
                    av_freep(&outBuf);
                    break;
                }

                // Calculate size of converted data in bytes
                int outBufSize = av_samples_get_buffer_size(
                    nullptr, outChannels,
                    convertedSamples,
                    AV_SAMPLE_FMT_S16, 1
                );
                
                if (outBufSize > 0) {
                    // Queue to SDL audio
                    SDL_QueueAudio(audioDeviceID, outBuf, outBufSize);
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
        int seekResult = av_seek_frame(videoPlayer->formatContext, -1, 0, AVSEEK_FLAG_BACKWARD);
        if (seekResult < 0) {
            char errbuf[256];
            av_strerror(seekResult, errbuf, sizeof(errbuf));
            cerr << "Error seeking to beginning: " << errbuf << endl;
        } else {
            // Flush the codec buffers
            avcodec_flush_buffers(audioCodecContext);
        }
    }

    // Clean up
    av_frame_free(&frame);
    av_packet_free(&pkt);
}

void MainWindow::updateMedia() {
    if (!videoPlayer->isPlaying || !videoPlayer->formatContext) {
        return; // Video not playing or not initialized
    }

    // Ensure audio device and context are ready if we have audio
    bool hasAudio = (audioCodecContext && swrContext && audioDeviceID != 0);
    
    // Check audio buffer status
    bool needMoreAudio = false;
    const int IDEAL_AUDIO_QUEUE_SIZE = 8192 * 16; // Increase buffer size for smoother playback
    const int MIN_AUDIO_QUEUE_SIZE = 8192 * 4;   // Minimum threshold to start refilling
    
    if (hasAudio) {
        const int AUDIO_QUEUE_SIZE = SDL_GetQueuedAudioSize(audioDeviceID);
        needMoreAudio = (AUDIO_QUEUE_SIZE < MIN_AUDIO_QUEUE_SIZE);
    }

    // Read multiple packets for audio to ensure buffer stays filled
    int packetsProcessed = 0;
    const int MAX_PACKETS_PER_CALL = hasAudio && needMoreAudio ? 5 : 1;
    bool videoFrameProcessed = false;

    while (packetsProcessed < MAX_PACKETS_PER_CALL && !videoFrameProcessed) {
        AVPacket* pkt = av_packet_alloc();
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
                int seekResult = av_seek_frame(videoPlayer->formatContext, -1, 0, AVSEEK_FLAG_BACKWARD);
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
                int receiveResult = avcodec_receive_frame(videoPlayer->codecContext, videoPlayer->frame);
                
                if (receiveResult >= 0) {
                    // Successfully got a video frame
                    sws_scale(videoPlayer->swsContext, 
                            (uint8_t const* const*)videoPlayer->frame->data,
                            videoPlayer->frame->linesize, 0, videoPlayer->height,
                            videoPlayer->frameRGB->data, videoPlayer->frameRGB->linesize);

                    SDL_UpdateTexture(videoPlayer->texture, NULL, 
                                    videoPlayer->frameRGB->data[0],
                                    videoPlayer->frameRGB->linesize[0]);
                                    
                    glBindTexture(GL_TEXTURE_2D, videoPlayer->glTextureID);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, videoPlayer->width, videoPlayer->height,
                            GL_RGB, GL_UNSIGNED_BYTE, videoPlayer->frameRGB->data[0]);

                    // Update current time using frame PTS
                    if (videoPlayer->frame->pts != AV_NOPTS_VALUE) {
                        AVRational timeBase = videoPlayer->formatContext->streams[videoPlayer->videoStream]->time_base;
                        videoPlayer->currentTime = videoPlayer->frame->pts * av_q2d(timeBase);
                    }
                    
                    videoFrameProcessed = true;
                }
            }
        }
        // Process audio packet
        else if (hasAudio && pkt->stream_index == audioStream) {
            // Always process audio packets when we encounter them, 
            // but check if we need to keep processing more packets afterward
            AVFrame* audioFrame = av_frame_alloc();
            if (!audioFrame) {
                av_packet_unref(pkt);
                av_packet_free(&pkt);
                cerr << "Could not allocate audio frame" << endl;
                continue;
            }
            
            int sendResult = avcodec_send_packet(audioCodecContext, pkt);
            
            if (sendResult >= 0) {
                // Try to receive multiple frames from this packet if available
                bool frameReceived = false;
                
                while (true) {
                    int receiveResult = avcodec_receive_frame(audioCodecContext, audioFrame);
                    
                    if (receiveResult < 0) {
                        // No more frames or error
                        break;
                    }
                    
                    frameReceived = true;
                    
                    // Process audio frame
                    int outChannels = 2; // Stereo output
                    
                    // Calculate buffer size needed
                    int outSamples = av_rescale_rnd(
                        swr_get_delay(swrContext, audioCodecContext->sample_rate) + audioFrame->nb_samples,
                        44100, // output rate
                        audioCodecContext->sample_rate,
                        AV_ROUND_UP
                    );

                    uint8_t* outBuf = nullptr;
                    int outLinesize = 0;
                    int allocResult = av_samples_alloc(
                        &outBuf, &outLinesize,
                        outChannels,
                        outSamples,
                        AV_SAMPLE_FMT_S16, 0
                    );
                    
                    if (allocResult >= 0) {
                        int convertedSamples = swr_convert(
                            swrContext,
                            &outBuf, outSamples,
                            (const uint8_t**)audioFrame->data, audioFrame->nb_samples
                        );
                        
                        if (convertedSamples >= 0) {
                            int outBufSize = av_samples_get_buffer_size(
                                nullptr, outChannels,
                                convertedSamples,
                                AV_SAMPLE_FMT_S16, 1
                            );
                            
                            if (outBufSize > 0) {
                                SDL_QueueAudio(audioDeviceID, outBuf, outBufSize);
                                
                                // Check if we've filled the audio buffer enough
                                if (SDL_GetQueuedAudioSize(audioDeviceID) >= IDEAL_AUDIO_QUEUE_SIZE) {
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
            // We've both filled audio and processed a video frame
            break;
        }
    }
    
    // If audio is still needed, prioritize it on next call
    if (hasAudio) {
        const int AUDIO_QUEUE_SIZE = SDL_GetQueuedAudioSize(audioDeviceID);
        if (AUDIO_QUEUE_SIZE < MIN_AUDIO_QUEUE_SIZE) {
            cout << "AUDIO_QUEUE_SIZE: " << AUDIO_QUEUE_SIZE << endl;
            // Consider calling updateMedia again immediately or soon
            // via a callback/timer if your architecture supports it
        }
    }
}

// jangan lupa ganti nama fungsi
void MainWindow::renderVideoPlayer() {
    static char videoPath[256] = "";
    static bool loopVideo = true;
    static bool paused = false;
    // enum RenderMode currentMode;
    static bool isOnlyRender = isOnlyRenderImage;
    static bool isOnlyAd = isOnlyAudio;

    ImGui::Begin("Video Player", nullptr, ImGuiWindowFlags_NoCollapse);

    // Input file video
    ImGui::InputText("Video File", videoPath, IM_ARRAYSIZE(videoPath));
    ImGui::SameLine();
    if (ImGui::Button("Open")) {
        if (strlen(videoPath) > 0) {
            openVideo(videoPath);
            paused = false;
        }
    }

    ImGui::Checkbox("Loop Video", &loopVideo);
    ImGui::SameLine();
    ImGui::Checkbox("Only Render Image", &isOnlyRender);
    ImGui::SameLine();
    ImGui::Checkbox("Only Audio", &isOnlyAd);

    if (videoPlayer->isPlaying) {     
        cout << "Is Audio Play " << isOnlyAd << endl;   
        cout << "Is Image Play " << isOnlyRender << endl;
        // updateAudio();
        // Update frame dan audio jika tidak paused
        if (!paused) {
            if (isOnlyRender == true && isOnlyAd == false) {
                // updateVideoFrame();
                updateVideoFrameWithOpenGL();
            }
            else if (isOnlyRender == false && isOnlyAd == true) {
                updateAudio();
            }
            else {     
                updateMedia();
            }
        }

        // Render video frame
        renderVideoFrame();

        // Kontrol video
        ImGui::Separator();
        if (ImGui::Button(paused ? "Play" : "Pause")) {
            paused = !paused;
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            av_seek_frame(videoPlayer->formatContext, videoPlayer->videoStream, 0, AVSEEK_FLAG_BACKWARD);
            videoPlayer->currentTime = 0;
            paused = true;
        }

        ImGui::Text("Time: %.2f / %.2f", videoPlayer->currentTime, videoPlayer->duration);

        // Informasi video
        ImGui::Text("Resolution: %dx%d | FPS: %.2f", 
                    videoPlayer->width, videoPlayer->height, videoPlayer->fps);
        if (!isOnlyRender | isOnlyAd) {
            ImGui::Separator();
            ImGui::Text("Frequency: %d | Channels: %d", 
                    videoPlayer->audioCodecContext->sample_rate, videoPlayer->audioCodecContext->ch_layout.nb_channels);
        }
        
    } else {
        ImGui::Text("No video loaded. Please open a video file.");
    }

    ImGui::End();
}

void MainWindow::renderVideoFrame() {
    if (videoPlayer->texture) {
        // cout << "Texture ID: " << videoPlayer->texture << endl;
        // Hitung rasio aspek
        float aspectRatio = static_cast<float>(videoPlayer->width) / static_cast<float>(videoPlayer->height);

        // Hitung dimensi tampilan
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        float displayWidth = contentSize.x;
        float displayHeight = displayWidth / aspectRatio;

        if (displayHeight > contentSize.y) {
            displayHeight = contentSize.y;
            displayWidth = displayHeight * aspectRatio;
        }

        // Pusatkan video di ruang yang tersedia
        float posX = (contentSize.x - displayWidth) * 0.5f;
        float posY = (contentSize.y - displayHeight) * 0.5f;

        ImGui::SetCursorPos(ImVec2(posX, posY));

        // Render frame video
        ImGui::Image(reinterpret_cast<ImTextureID>(reinterpret_cast<void*>(static_cast<intptr_t>(videoPlayer->glTextureID))),
             ImVec2(displayWidth, displayHeight));
    }
}

void MainWindow::handleEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);

        if (event.type == SDL_QUIT)
            isRunning = false;
        else if (event.type == SDL_KEYDOWN) {
            if (event.key.keysym.sym == SDLK_ESCAPE)
                isRunning = false;
            else if (event.key.keysym.sym == SDLK_f) {
                fullscreen = !fullscreen;
                SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            } 
            else if (event.key.keysym.sym == SDLK_t) {
                darkTheme = !darkTheme;
                setTheme(darkTheme);
            }
        }
        else if (event.type == SDL_WINDOWEVENT) {
            if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                windowWidth = event.window.data1;
                windowHeight = event.window.data2;
            }
        }
    }
}

// Method Update Like Unity too
void MainWindow::update() {
    static float volume = 1.0f;
    static bool isBackgroundChanged = false;
    static bool isBackgroundActived = false;
    CurrentBackground currentBg = static_cast<CurrentBackground>(currentBgInt);
    
    const char* backgroundOptions[] = {
        "Shiroko",
        "Shun (Small)"
    };

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT)
            isRunning = false;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Dockspace
    #ifdef ImGuiConfigFlags_DockingEnable
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    #else
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar;
    #endif
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    // ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    
    ImGui::Begin("DockSpace", nullptr, window_flags);
    ImGui::PopStyleVar(2);
    
    // DockSpace
    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    #ifdef ImGuiConfigFlags_DockingEnable
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    #else
        ImGui::Text("Docking is not enabled in this build of Dear ImGui.");
    #endif
    
    // Render background SETELAH DockSpace tetapi SEBELUM semua window UI lainnya
    // Ini memastikan background ada di belakang semua window UI
    if (isBackgroundActived)
    {
        if (isBackgroundChanged)
        {
            HandleUpdateBackground(currentBg);
            isBackgroundChanged = false;
        }

        ImGuiIO& io = ImGui::GetIO();
        // Menggunakan ImGui::GetBackgroundDrawList untuk menggambar di lapisan paling belakang
        if (backgroundTexture.TextureID != 0) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::SetNextWindowBgAlpha(volume); // <--- Bikin window background jadi transparan
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::Begin("Background", nullptr,
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoInputs |
                // ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus);
            
            ImGui::Image((ImTextureID)(intptr_t)backgroundTexture.TextureID, io.DisplaySize);
            ImGui::End();
            ImGui::PopStyleVar(2);
        }
    } 
    else if (isBackgroundChanged)
    {
        HandleUpdateBackground(currentBg);
        isBackgroundChanged = false;
    }    
    
    // Menu bar
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Project", "Ctrl+N")) {}
            if (ImGui::MenuItem("Open Project", "Ctrl+O")) {}
            if (ImGui::MenuItem("Save", "Ctrl+S")) {}
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) isRunning = false;
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) {}
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Cut", "Ctrl+X")) {}
            if (ImGui::MenuItem("Copy", "Ctrl+C")) {}
            if (ImGui::MenuItem("Paste", "Ctrl+V")) {}
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Toggle Dark/Light Theme", "Ctrl+T")) {
                darkTheme = !darkTheme;
                setTheme(darkTheme);
            }
            if (ImGui::MenuItem("Toggle Fullscreen", "F")) {
                fullscreen = !fullscreen;
                SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Secondary Window", nullptr, &showSecondary)) {}
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Documentation")) {}
            if (ImGui::MenuItem("About")) {}
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Background")){
            ImGui::Checkbox("Use Background", &isBackgroundActived);
            if (ImGui::Combo(" ", &currentBgInt, backgroundOptions, Background_Count)) {
                currentBg = static_cast<CurrentBackground>(currentBgInt);
                cout << currentBg << endl;
                isBackgroundChanged = true;
                isBackgroundActived = true;
            }
            
            // Tambahan pengaturan background opacity
            ImGui::SliderFloat("Opacity", &volume, 0.1f, 1.0f);
            ImGui::EndMenu();
        }

        // Status bar di menu kanan
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 200);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        
        ImGui::EndMenuBar();
    }
    
    // Left: Hierarchy with tabs
    ImGui::Begin("Explorer", nullptr, ImGuiWindowFlags_NoCollapse);
    
    if (ImGui::BeginTabBar("ExplorerTabs")) {
        if (ImGui::BeginTabItem("Hierarchy")) {
            ImGui::BeginChild("HierarchyTree", ImVec2(0, 0), true);
            if (ImGui::TreeNode("Scene")) {
                if (ImGui::TreeNode("Main Camera")) {
                    ImGui::Text("Properties");
                    ImGui::TreePop();
                }
                if (ImGui::TreeNode("Player")) {
                    ImGui::Text("Sprite");
                    ImGui::Text("Collider");
                    ImGui::TreePop();
                }
                if (ImGui::TreeNode("Enemy")) {
                    ImGui::Text("AI Controller");
                    ImGui::TreePop();
                }
                ImGui::TreePop();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        
        if (ImGui::BeginTabItem("Assets")) {
            ImGui::BeginChild("AssetsList", ImVec2(0, 0), true);
            if (ImGui::TreeNode("Textures")) {
                ImGui::Text("player.png");
                ImGui::Text("enemy.png");
                ImGui::Text("background.jpg");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Audio")) {
                ImGui::Text("music.mp3");
                ImGui::Text("effect.wav");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Scripts")) {
                ImGui::Text("Player.cs");
                ImGui::Text("Enemy.cs");
                ImGui::TreePop();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        
        ImGui::EndTabBar();
    }
    
    ImGui::End();

    // Right: Inspector
    ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse);
    
    ImGui::Text("Selected Object");
    ImGui::Separator();
    
    ImGui::InputText("Name", objectName, IM_ARRAYSIZE(objectName));
    
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", position, 0.1f);
        ImGui::DragFloat3("Rotation", rotation, 0.1f);
        ImGui::DragFloat3("Scale", scale, 0.1f);
    }
    
    if (ImGui::CollapsingHeader("Material")) {
        static float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        ImGui::ColorEdit4("Color", color);
        
        const char* items[] = { "Standard", "Transparent", "Emission" };
        static int item_current = 0;
        ImGui::Combo("Shader", &item_current, items, IM_ARRAYSIZE(items));
        
        static float metallic = 0.0f;
        static float smoothness = 0.5f;
        ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f);
        ImGui::SliderFloat("Smoothness", &smoothness, 0.0f, 1.0f);
    }
    
    if (ImGui::CollapsingHeader("Physics")) {
        static bool useGravity = true;
        static bool isKinematic = false;
        static float mass = 1.0f;
        static float drag = 0.0f;
        
        ImGui::Checkbox("Use Gravity", &useGravity);
        ImGui::Checkbox("Is Kinematic", &isKinematic);
        ImGui::InputFloat("Mass", &mass, 0.1f);
        ImGui::InputFloat("Drag", &drag, 0.01f);
    }
    
    if (ImGui::Button("Add Component", ImVec2(-1, 0))) {
        ImGui::OpenPopup("AddComponentPopup");
    }
    
    if (ImGui::BeginPopup("AddComponentPopup")) {
        ImGui::Text("Components");
        if (ImGui::Selectable("Mesh Renderer")) {}
        if (ImGui::Selectable("Audio Source")) {}
        if (ImGui::Selectable("Collider")) {}
        if (ImGui::Selectable("Particle System")) {}
        if (ImGui::Selectable("Light")) {}
        if (ImGui::Selectable("Script")) {}
        ImGui::EndPopup();
    }
    
    ImGui::End();

    // Center: Main Tabs (Viewport, Video Player, etc)
    ImGui::Begin("Main View", nullptr, ImGuiWindowFlags_NoCollapse);
    
    if (ImGui::BeginTabBar("MainTabs")) {
        if (ImGui::BeginTabItem("Viewport")) {
            ImVec2 contentSize = ImGui::GetContentRegionAvail();
            
            // Add toolbar for viewport
            ImGui::BeginGroup();
            if (ImGui::Button("Play")) {}
            ImGui::SameLine();
            if (ImGui::Button("Pause")) {}
            ImGui::SameLine();
            if (ImGui::Button("Stop")) {}
            ImGui::SameLine();
            ImGui::Separator();
            ImGui::SameLine();
            const char* viewModes[] = { "3D", "2D", "UI" };
            static int viewMode = 0;
            ImGui::SetNextItemWidth(100);
            ImGui::Combo("View", &viewMode, viewModes, IM_ARRAYSIZE(viewModes));
            ImGui::EndGroup();
            
            ImGui::Separator();
            
            // Render game viewport
            static ImVec4 viewportBgColor = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImGui::GetCursorScreenPos(),
                ImVec2(ImGui::GetCursorScreenPos().x + contentSize.x, 
                       ImGui::GetCursorScreenPos().y + contentSize.y - 30),
                ImGui::ColorConvertFloat4ToU32(viewportBgColor)
            );
            
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + contentSize.y - 30);
            ImGui::Text("Game Viewport (Scene would render here)");
            
            ImGui::EndTabItem();
        }
        
        if (ImGui::BeginTabItem("Video Player")) {
            renderVideoPlayer();
            ImGui::EndTabItem();
        }
        
        if (ImGui::BeginTabItem("Animation")) {
            ImGui::Text("Animation editor will be displayed here");
            ImGui::EndTabItem();
        }
        
        if (ImGui::BeginTabItem("Particle Editor")) {
            ImGui::Text("Particle system editor will be displayed here");
            ImGui::EndTabItem();
        }
        
        ImGui::EndTabBar();
    }
    
    ImGui::End();

    // Bottom: Console & Output
    ImGui::Begin("Console", nullptr, ImGuiWindowFlags_NoCollapse);
    
    static int selectedTab = 0;
    ImGui::BeginTabBar("ConsoleTabs");
    
    if (ImGui::BeginTabItem("Output")) {
        selectedTab = 0;
        static char consoleBuffer[4096] = "Game initialized successfully.\nAll systems operational.\nReady to start...\n";
        ImGui::InputTextMultiline("##console", consoleBuffer, IM_ARRAYSIZE(consoleBuffer), 
                                 ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndTabItem();
    }
    
    if (ImGui::BeginTabItem("Errors")) {
        selectedTab = 1;
        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "No errors found.");
        ImGui::EndTabItem();
    }
    
    if (ImGui::BeginTabItem("Build")) {
        selectedTab = 2;
        ImGui::Text("Build output will be displayed here");
        ImGui::EndTabItem();
    }
    
    ImGui::EndTabBar();
    
    ImGui::End();

    // Show secondary window if needed
    if (showSecondary) {
        ShowSecondaryWindow(&showSecondary);
    }

    // videoPlayer->updateAudio();
    
    ImGui::End(); // DockSpace
}

void MainWindow::render() {
    // Clear screen
    // SDL_SetRenderDrawColor(renderer, 24, 25, 34, 255);
    // SDL_RenderClear(renderer);
    // glViewport(0, 0, windowWidth, windowHeight);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // set_mainbackground();
    // Render ImGui
    ImGui::Render();
    // ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window);

    // set_mainbackground();

    // Present renderer
    // SDL_RenderPresent(renderer);
}

void MainWindow::clean() {
    // ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    glDeleteTextures(1, &videoPlayer->glTextureID);

    ImGui::DestroyContext();

    if (glContext)
    SDL_GL_DeleteContext(glContext);

    // SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    if (audioDeviceID) {
        SDL_CloseAudioDevice(audioDeviceID);
        audioDeviceID = 0;
    }
    if (swrContext) {
        swr_free(&swrContext);
    }
    if (audioCodecContext) {
        avcodec_free_context(&audioCodecContext);
    }

    SDL_Quit();
}
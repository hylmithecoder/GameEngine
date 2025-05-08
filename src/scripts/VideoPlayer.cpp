#include "../header/VideoPlayer.hpp"
#include <iostream>
using namespace std;
// #include <swresample.h>

VideoPlayer::VideoPlayer() :
    audioDeviceID(0),
    swrContext(nullptr)
{}

VideoPlayer::~VideoPlayer() {
    cleanup();
}

bool VideoPlayer::openAudio() {
    // 1. Temukan codec audio
    const AVCodec* audioCodec = avcodec_find_decoder(formatContext->streams[audioStream]->codecpar->codec_id);
    if (!audioCodec) {
        std::cerr << "Could not find audio codec\n";
        return false;
    }

    // 2. Alokasikan codec context dan set parameters
    audioCodecContext = avcodec_alloc_context3(audioCodec);
    if (!audioCodecContext) {
        std::cerr << "Could not allocate audio codec context\n";
        return false;
    }
    
    if (avcodec_parameters_to_context(audioCodecContext, formatContext->streams[audioStream]->codecpar) < 0) {
        std::cerr << "Could not copy codec parameters to context\n";
        return false;
    }

    if (avcodec_open2(audioCodecContext, audioCodec, NULL) < 0) {
        std::cerr << "Could not open audio codec\n";
        return false;
    }

    // 3. Setup SwrContext untuk resampling audio
    swrContext = swr_alloc();
    if (!swrContext) {
        std::cerr << "Could not allocate SwrContext\n";
        return false;
    }

    // 4. Set input options (channel layout, sample rate, format)
    av_opt_set_int(swrContext, "in_channel_layout", audioCodecContext->ch_layout.u.mask, 0);
    av_opt_set_int(swrContext, "in_sample_rate", audioCodecContext->sample_rate, 0);
    av_opt_set_sample_fmt(swrContext, "in_sample_fmt", audioCodecContext->sample_fmt, 0);

    // 5. Set output options (channel layout, sample rate, format)
    av_opt_set_int(swrContext, "out_channel_layout", audioCodecContext->ch_layout.u.mask, 0);
    av_opt_set_int(swrContext, "out_sample_rate", audioCodecContext->sample_rate, 0);
    av_opt_set_sample_fmt(swrContext, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

    // 6. Inisialisasi SwrContext
    if (swr_init(swrContext) < 0) {
        std::cerr << "Failed to initialize resampling context: " << swrContext << endl;
        return false;
    }
    cout << "Resampling context initialized: " << swrContext << endl;

    // 7. Siapkan SDL Audio Device
    SDL_AudioSpec wantSpec, haveSpec;
    SDL_zero(wantSpec);
    
    wantSpec.freq = audioCodecContext->sample_rate;
    wantSpec.format = AUDIO_S16SYS;
    wantSpec.channels = audioCodecContext->ch_layout.nb_channels;
    wantSpec.samples = 1024;  // Atur buffer ukuran sesuai kebutuhan
    
    // Setup callback function untuk SDL Audio
    wantSpec.callback = [](void* userdata, Uint8* stream, int len) {
        VideoPlayer* player = static_cast<VideoPlayer*>(userdata);
        SDL_memset(stream, 0, len); // Clear the stream buffer
        if (player->audioDeviceID != 0) {
            SDL_DequeueAudio(player->audioDeviceID, stream, len);
        }
    };
    wantSpec.userdata = this;

    // Buka perangkat audio
    audioDeviceID = SDL_OpenAudioDevice(NULL, 0, &wantSpec, &haveSpec, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (audioDeviceID == 0) {
        std::cerr << "Failed to open audio device: " << SDL_GetError() << std::endl;
        return false;
    }

    // Pastikan bahwa spesifikasi yang diterima cocok dengan yang diminta
    if (wantSpec.freq != haveSpec.freq || wantSpec.channels != haveSpec.channels || wantSpec.format != haveSpec.format) {
        std::cerr << "Warning: Audio device parameters don't match requested parameters\n";
    }

    // Mulai pemutaran audio
    SDL_PauseAudioDevice(audioDeviceID, 0);  // Mengaktifkan audio device untuk pemutaran

    return true;
}

SDL_Texture* VideoPlayer::convertFrameToTexture(AVFrame* frame, SDL_Renderer* renderer, SwsContext* swsCtx) {
    int width = frame->width;
    int height = frame->height;
    cout << frame << renderer << swsCtx << endl;
    // Alokasikan buffer RGB
    AVFrame* rgbFrame = av_frame_alloc();
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, width, height, 1);
    uint8_t* buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));
    av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, buffer,
                         AV_PIX_FMT_RGB24, width, height, 1);

    // Convert dari YUV ke RGB
    sws_scale(
        swsCtx,
        frame->data, frame->linesize,
        0, height,
        rgbFrame->data, rgbFrame->linesize
    );

    // Buat SDL_Texture dari data RGB
    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STREAMING,
        width, height
    );

    if (texture) {
        SDL_UpdateTexture(texture, nullptr, rgbFrame->data[0], rgbFrame->linesize[0]);
    } else {
        std::cerr << "Failed to create SDL Texture: " << SDL_GetError() << std::endl;
    }

    // Cleanup
    av_free(buffer);
    av_frame_free(&rgbFrame);

    return texture;
}


void VideoPlayer::cleanup() {
    if (buffer) av_free(buffer);
    if (frameRGB) av_frame_free(&frameRGB);
    if (frame) av_frame_free(&frame);
    if (packet) av_packet_free(&packet);
    if (codecContext) avcodec_free_context(&codecContext);
    if (formatContext) avformat_close_input(&formatContext);
    if (swsContext) sws_freeContext(swsContext);
    if (texture) SDL_DestroyTexture(texture);

    formatContext = nullptr;
    codecContext = nullptr;
    swsContext = nullptr;
    frame = nullptr;
    frameRGB = nullptr;
    packet = nullptr;
    texture = nullptr;
    buffer = nullptr;
    videoStream = -1;
    isPlaying = false;
}
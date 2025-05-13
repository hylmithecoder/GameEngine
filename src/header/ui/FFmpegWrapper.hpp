#pragma once

// Disable warnings from FFmpeg headers
#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable: 4244) // possible loss of data
    #pragma warning(disable: 4996) // deprecated functions
#endif

extern "C" {
    // Sesuaikan path sesuai dengan instalasi FFmpeg Anda
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
    #include <libavutil/imgutils.h>
    #include <libswresample/swresample.h>
    #include <libavutil/channel_layout.h>   // untuk av_get_default_channel_layout
    #include <libavutil/samplefmt.h> 
    #include <libavutil/opt.h>
    #include <libavutil/time.h>
}

#ifdef _MSC_VER
    #pragma warning(pop)
#endif

// Wrapper class untuk memudahkan penggunaan FFmpeg
class FFmpegWrapper {
public:
    static bool Init() {
        // Register all codecs, demuxers and protocols (deprecated in newer versions)
        #if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58, 9, 100)
            av_register_all();
        #endif
        
        return true;
    }
    
    static void Cleanup() {
        // FFmpeg cleanup jika diperlukan
    }
};
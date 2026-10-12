#include "../../../include/core_engine/VideoDecoder.hpp"
#include <algorithm>
#include <chrono>
#include <thread>

#ifdef ILMEEE_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#elif defined(__ANDROID__)
#include "../../../include/core_engine/AndroidMedia.hpp"
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#endif

namespace ilmeee {

namespace {
constexpr size_t kMaxQueuedFrames = 8;
// Keep decoding past a full frame queue while the sound track has less
// than this buffered, and stop at kAudioHigh regardless.
constexpr size_t kAudioLow = kAudioRate / 4;
constexpr size_t kAudioHigh = kAudioRate * 4;
} // namespace

#ifdef ILMEEE_HAS_FFMPEG

struct VideoDecoder::Impl {
  AVFormatContext *fmt = nullptr;
  AVCodecContext *vdec = nullptr;
  AVCodecContext *adec = nullptr;
  SwsContext *sws = nullptr;
  SwrContext *swr = nullptr;
  int vstream = -1, astream = -1;
  int outW = 0, outH = 0;

  ~Impl() {
    sws_freeContext(sws);
    swr_free(&swr);
    avcodec_free_context(&vdec);
    avcodec_free_context(&adec);
    avformat_close_input(&fmt);
  }
};

bool VideoDecoder::Available() { return true; }

bool VideoDecoder::Open(const std::string &path, bool loop,
                        std::string *error) {
  Close();
  auto impl = std::make_unique<Impl>();
  auto fail = [&](const char *msg) {
    if (error)
      *error = msg;
    return false;
  };
  if (avformat_open_input(&impl->fmt, path.c_str(), nullptr, nullptr) < 0)
    return fail("cannot open file");
  if (avformat_find_stream_info(impl->fmt, nullptr) < 0)
    return fail("cannot read stream info");

  const AVCodec *vcodec = nullptr;
  impl->vstream =
      av_find_best_stream(impl->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &vcodec, 0);
  if (impl->vstream < 0 || !vcodec)
    return fail("no video stream");
  impl->vdec = avcodec_alloc_context3(vcodec);
  avcodec_parameters_to_context(impl->vdec,
                                impl->fmt->streams[impl->vstream]->codecpar);
  impl->vdec->thread_count = 0; // let FFmpeg pick
  if (avcodec_open2(impl->vdec, vcodec, nullptr) < 0)
    return fail("cannot open video codec");
  if (impl->vdec->width <= 0 || impl->vdec->height <= 0)
    return fail("video has no size");
  const float fit =
      std::min({1.0f, (float)kMaxVideoWidth / impl->vdec->width,
                (float)kMaxVideoHeight / impl->vdec->height});
  impl->outW = std::max(2, (int)(impl->vdec->width * fit) & ~1);
  impl->outH = std::max(2, (int)(impl->vdec->height * fit) & ~1);

  // Sound track is optional; a broken one just plays silent.
  const AVCodec *acodec = nullptr;
  impl->astream = av_find_best_stream(impl->fmt, AVMEDIA_TYPE_AUDIO, -1,
                                      impl->vstream, &acodec, 0);
  if (impl->astream >= 0 && acodec) {
    impl->adec = avcodec_alloc_context3(acodec);
    avcodec_parameters_to_context(impl->adec,
                                  impl->fmt->streams[impl->astream]->codecpar);
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    if (avcodec_open2(impl->adec, acodec, nullptr) < 0 ||
        swr_alloc_set_opts2(&impl->swr, &stereo, AV_SAMPLE_FMT_FLT, kAudioRate,
                            &impl->adec->ch_layout, impl->adec->sample_fmt,
                            impl->adec->sample_rate, 0, nullptr) < 0 ||
        swr_init(impl->swr) < 0) {
      avcodec_free_context(&impl->adec);
      swr_free(&impl->swr);
      impl->astream = -1;
    }
  } else {
    impl->astream = -1;
  }

  impl_ = std::move(impl);
  audio_ = impl_->astream >= 0 ? std::make_shared<PcmRing>() : nullptr;
  stop_ = false;
  eof_ = false;
  thread_ = std::thread([this, loop] { Run(loop); });
  return true;
}

void VideoDecoder::Run(bool loop) {
  Impl &d = *impl_;
  AVPacket *pkt = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();
  std::vector<float> pcm;
  const AVRational vtb = d.fmt->streams[d.vstream]->time_base;
  double offset = 0.0;    // added to pts after each loop
  double lastVideo = 0.0; // last video pts (raw) of the current pass
  double frameDur = 1.0 / 30.0;
  uint64_t audioFrames = 0; // pushed in total, across loops

  auto emitVideo = [&]() {
    while (avcodec_receive_frame(d.vdec, frame) == 0) {
      int64_t ts = frame->best_effort_timestamp;
      const double raw = ts == AV_NOPTS_VALUE ? lastVideo + frameDur
                                              : ts * av_q2d(vtb);
      if (raw > lastVideo)
        frameDur = std::clamp(raw - lastVideo, 1.0 / 240.0, 0.5);
      lastVideo = raw;
      d.sws = sws_getCachedContext(
          d.sws, frame->width, frame->height, (AVPixelFormat)frame->format,
          d.outW, d.outH, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr,
          nullptr);
      if (!d.sws)
        continue;
      VideoFrame out;
      out.pts = offset + raw;
      out.width = d.outW;
      out.height = d.outH;
      out.rgba.resize((size_t)d.outW * d.outH * 4);
      uint8_t *dst[4] = {out.rgba.data(), nullptr, nullptr, nullptr};
      int dstStride[4] = {d.outW * 4, 0, 0, 0};
      sws_scale(d.sws, frame->data, frame->linesize, 0, frame->height, dst,
                dstStride);
      std::lock_guard<std::mutex> lock(mutex_);
      frames_.push_back(std::move(out));
      // Only reached while the sound track is starving: drop the oldest.
      while (frames_.size() > kMaxQueuedFrames * 2)
        frames_.pop_front();
    }
  };
  auto emitAudio = [&]() {
    while (avcodec_receive_frame(d.adec, frame) == 0) {
      const int maxOut = swr_get_out_samples(d.swr, frame->nb_samples) + 256;
      pcm.resize((size_t)maxOut * kAudioChannels);
      uint8_t *outPtr = (uint8_t *)pcm.data();
      const int n =
          swr_convert(d.swr, &outPtr, maxOut,
                      (const uint8_t **)frame->extended_data, frame->nb_samples);
      if (n > 0) {
        audio_->Push(pcm.data(), (size_t)n);
        audioFrames += (uint64_t)n;
      }
    }
  };

  while (!stop_) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      // Polls too: draining the sound track doesn't notify us.
      const auto canDecode = [&] {
        if (stop_)
          return true;
        const size_t queuedAudio = audio_ ? audio_->Frames() : kAudioLow;
        const bool videoFull = frames_.size() >= kMaxQueuedFrames;
        const bool audioFull = queuedAudio >= kAudioLow;
        return !(videoFull && audioFull) &&
               !(queuedAudio >= kAudioHigh && !frames_.empty());
      };
      while (!canDecode())
        cv_.wait_for(lock, std::chrono::milliseconds(10));
    }
    if (stop_)
      break;

    if (av_read_frame(d.fmt, pkt) < 0) {
      // End of file: flush what the codecs still hold.
      avcodec_send_packet(d.vdec, nullptr);
      emitVideo();
      if (d.adec) {
        avcodec_send_packet(d.adec, nullptr);
        emitAudio();
      }
      if (!loop) {
        eof_ = true;
        if (audio_)
          audio_->SetFinished(true);
        break;
      }
      // Next pass continues the timeline where the sound (or the picture,
      // without sound) ended, so the clock never jumps back.
      offset = audio_ ? (double)audioFrames / kAudioRate
                      : offset + lastVideo + frameDur;
      lastVideo = 0.0;
      av_seek_frame(d.fmt, -1, 0, AVSEEK_FLAG_BACKWARD);
      avcodec_flush_buffers(d.vdec);
      if (d.adec)
        avcodec_flush_buffers(d.adec);
      continue;
    }
    if (pkt->stream_index == d.vstream) {
      if (avcodec_send_packet(d.vdec, pkt) >= 0)
        emitVideo();
    } else if (pkt->stream_index == d.astream && d.adec) {
      if (avcodec_send_packet(d.adec, pkt) >= 0)
        emitAudio();
    }
    av_packet_unref(pkt);
  }
  av_frame_free(&frame);
  av_packet_free(&pkt);
}

#elif defined(__ANDROID__) // MediaCodec

struct VideoDecoder::Impl {
  android_media::Extractor ex;
  AMediaCodec *vcodec = nullptr;
  AMediaCodec *acodec = nullptr;
  // The decoder renders into this reader's surface; frames come back as
  // YUV_420_888 images with explicit strides, whatever layout the
  // hardware uses internally.
  AImageReader *reader = nullptr;
  int vtrack = -1, atrack = -1;
  android_media::PcmConverter pcm;

  ~Impl() {
    for (AMediaCodec *c : {vcodec, acodec})
      if (c) {
        AMediaCodec_stop(c);
        AMediaCodec_delete(c);
      }
    if (reader)
      AImageReader_delete(reader);
  }
};

bool VideoDecoder::Available() { return true; }

bool VideoDecoder::Open(const std::string &path, bool loop,
                        std::string *error) {
  Close();
  auto impl = std::make_unique<Impl>();
  auto fail = [&](const std::string &msg) {
    if (error)
      *error = msg;
    return false;
  };
  if (!impl->ex.Open(path, error))
    return false;

  AMediaFormat *vfmt = nullptr;
  std::string vmime;
  impl->vtrack = impl->ex.FindTrack("video/", &vfmt, &vmime);
  if (impl->vtrack < 0)
    return fail("no video stream");
  int32_t w = 0, h = 0;
  AMediaFormat_getInt32(vfmt, AMEDIAFORMAT_KEY_WIDTH, &w);
  AMediaFormat_getInt32(vfmt, AMEDIAFORMAT_KEY_HEIGHT, &h);
  ANativeWindow *window = nullptr;
  if (w <= 0 || h <= 0 ||
      AImageReader_new(w, h, AIMAGE_FORMAT_YUV_420_888, 4, &impl->reader) !=
          AMEDIA_OK ||
      AImageReader_getWindow(impl->reader, &window) != AMEDIA_OK) {
    AMediaFormat_delete(vfmt);
    return fail("cannot create an image reader for " + std::to_string(w) +
                "x" + std::to_string(h));
  }
  impl->vcodec = AMediaCodec_createDecoderByType(vmime.c_str());
  const bool vok =
      impl->vcodec &&
      AMediaCodec_configure(impl->vcodec, vfmt, window, nullptr, 0) ==
          AMEDIA_OK &&
      AMediaCodec_start(impl->vcodec) == AMEDIA_OK;
  AMediaFormat_delete(vfmt);
  if (!vok)
    return fail("this device has no decoder for " + vmime);
  AMediaExtractor_selectTrack(impl->ex.get(), (size_t)impl->vtrack);

  // Sound track is optional; a broken one just plays silent.
  AMediaFormat *afmt = nullptr;
  std::string amime;
  impl->atrack = impl->ex.FindTrack("audio/", &afmt, &amime);
  if (impl->atrack >= 0) {
    impl->acodec = AMediaCodec_createDecoderByType(amime.c_str());
    if (impl->acodec &&
        AMediaCodec_configure(impl->acodec, afmt, nullptr, nullptr, 0) ==
            AMEDIA_OK &&
        AMediaCodec_start(impl->acodec) == AMEDIA_OK) {
      int rate, channels;
      bool isFloat;
      android_media::PcmFormatOf(afmt, rate, channels, isFloat);
      impl->pcm.SetFormat(rate, channels, isFloat);
      AMediaExtractor_selectTrack(impl->ex.get(), (size_t)impl->atrack);
    } else {
      if (impl->acodec)
        AMediaCodec_delete(impl->acodec);
      impl->acodec = nullptr;
      impl->atrack = -1;
    }
    AMediaFormat_delete(afmt);
  }

  impl_ = std::move(impl);
  audio_ = impl_->acodec ? std::make_shared<PcmRing>() : nullptr;
  stop_ = false;
  eof_ = false;
  thread_ = std::thread([this, loop] { Run(loop); });
  return true;
}

namespace {

// YUV_420_888 image → RGBA, cropped and scaled (nearest) to fit
// kMaxVideoWidth x kMaxVideoHeight. BT.601 limited range.
bool ImageToFrame(AImage *img, VideoFrame &out) {
  AImageCropRect r{};
  int32_t iw = 0, ih = 0;
  AImage_getWidth(img, &iw);
  AImage_getHeight(img, &ih);
  if (AImage_getCropRect(img, &r) != AMEDIA_OK || r.right <= r.left ||
      r.bottom <= r.top)
    r = {0, 0, iw, ih};
  const int cw = r.right - r.left, ch = r.bottom - r.top;
  if (cw <= 0 || ch <= 0)
    return false;
  uint8_t *p[3];
  int len[3], rowStride[3], pixStride[3] = {1, 1, 1};
  for (int i = 0; i < 3; ++i) {
    if (AImage_getPlaneData(img, i, &p[i], &len[i]) != AMEDIA_OK ||
        AImage_getPlaneRowStride(img, i, &rowStride[i]) != AMEDIA_OK)
      return false;
    if (i > 0)
      AImage_getPlanePixelStride(img, i, &pixStride[i]);
  }
  const float fit = std::min(
      {1.0f, (float)kMaxVideoWidth / cw, (float)kMaxVideoHeight / ch});
  out.width = std::max(2, (int)(cw * fit) & ~1);
  out.height = std::max(2, (int)(ch * fit) & ~1);
  out.rgba.resize((size_t)out.width * out.height * 4);
  uint8_t *dst = out.rgba.data();
  auto clamp8 = [](int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); };
  for (int y = 0; y < out.height; ++y) {
    const int sy = r.top + (int)((int64_t)y * ch / out.height);
    const uint8_t *yRow = p[0] + (size_t)sy * rowStride[0];
    const uint8_t *uRow = p[1] + (size_t)(sy / 2) * rowStride[1];
    const uint8_t *vRow = p[2] + (size_t)(sy / 2) * rowStride[2];
    for (int x = 0; x < out.width; ++x) {
      const int sx = r.left + (int)((int64_t)x * cw / out.width);
      const int c = (int)yRow[sx] - 16;
      const int d = (int)uRow[(sx / 2) * pixStride[1]] - 128;
      const int e = (int)vRow[(sx / 2) * pixStride[2]] - 128;
      *dst++ = clamp8((298 * c + 409 * e + 128) >> 8);
      *dst++ = clamp8((298 * c - 100 * d - 208 * e + 128) >> 8);
      *dst++ = clamp8((298 * c + 516 * d + 128) >> 8);
      *dst++ = 255;
    }
  }
  return true;
}

} // namespace

void VideoDecoder::Run(bool loop) {
  Impl &d = *impl_;
  AMediaExtractor *ex = d.ex.get();
  std::vector<float> pcm;
  double offset = 0.0;    // added to pts after each loop
  double lastVideo = 0.0; // last video pts (raw) of the current pass
  double frameDur = 1.0 / 30.0;
  uint64_t audioFrames = 0;
  bool inputEOS = false, videoEOS = false, audioEOS = d.acodec == nullptr;

  auto pushFrames = [&](bool waitForOne) {
    // A rendered buffer reaches the reader asynchronously.
    for (int tries = 0; tries < (waitForOne ? 40 : 1); ++tries) {
      bool got = false;
      AImage *img = nullptr;
      while (AImageReader_acquireNextImage(d.reader, &img) == AMEDIA_OK) {
        got = true;
        int64_t ns = 0;
        AImage_getTimestamp(img, &ns);
        VideoFrame out;
        const bool ok = ImageToFrame(img, out);
        AImage_delete(img);
        if (!ok)
          continue;
        const double raw = ns / 1e9;
        if (raw > lastVideo)
          frameDur = std::clamp(raw - lastVideo, 1.0 / 240.0, 0.5);
        lastVideo = raw;
        out.pts = offset + raw;
        std::lock_guard<std::mutex> lock(mutex_);
        frames_.push_back(std::move(out));
        while (frames_.size() > kMaxQueuedFrames * 2)
          frames_.pop_front();
      }
      if (got || stop_)
        return;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };

  auto queueEOS = [&](AMediaCodec *c) {
    while (c && !stop_) {
      const ssize_t in = AMediaCodec_dequeueInputBuffer(c, 2000);
      if (in >= 0) {
        AMediaCodec_queueInputBuffer(c, (size_t)in, 0, 0, 0,
                                     AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        return;
      }
    }
  };

  while (!stop_) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      const auto canDecode = [&] {
        if (stop_)
          return true;
        const size_t queuedAudio = audio_ ? audio_->Frames() : kAudioLow;
        const bool videoFull = frames_.size() >= kMaxQueuedFrames;
        const bool audioFull = queuedAudio >= kAudioLow;
        return !(videoFull && audioFull) &&
               !(queuedAudio >= kAudioHigh && !frames_.empty());
      };
      while (!canDecode())
        cv_.wait_for(lock, std::chrono::milliseconds(10));
    }
    if (stop_)
      break;

    // ---- feed one compressed sample ----
    bool fed = false;
    if (!inputEOS) {
      const ssize_t track = AMediaExtractor_getSampleTrackIndex(ex);
      if (track < 0) {
        queueEOS(d.vcodec);
        queueEOS(d.acodec);
        inputEOS = true;
      } else {
        AMediaCodec *c = track == d.vtrack   ? d.vcodec
                         : track == d.atrack ? d.acodec
                                             : nullptr;
        if (!c) {
          AMediaExtractor_advance(ex);
        } else {
          const ssize_t in = AMediaCodec_dequeueInputBuffer(c, 2000);
          if (in >= 0) {
            size_t cap = 0;
            uint8_t *buf = AMediaCodec_getInputBuffer(c, (size_t)in, &cap);
            const ssize_t n = AMediaExtractor_readSampleData(ex, buf, cap);
            AMediaCodec_queueInputBuffer(
                c, (size_t)in, 0, n > 0 ? (size_t)n : 0,
                (uint64_t)std::max<int64_t>(0, AMediaExtractor_getSampleTime(ex)),
                0);
            AMediaExtractor_advance(ex);
            fed = true;
          }
        }
      }
    }

    // ---- drain decoded output ----
    AMediaCodecBufferInfo info;
    for (;;) {
      const ssize_t o =
          AMediaCodec_dequeueOutputBuffer(d.vcodec, &info, fed ? 0 : 2000);
      if (o >= 0) {
        const bool render = info.size > 0;
        AMediaCodec_releaseOutputBuffer(d.vcodec, (size_t)o, render);
        if (render)
          pushFrames(true);
        if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)
          videoEOS = true;
      } else if (o != AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED &&
                 o != AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
        break;
      }
    }
    while (d.acodec) {
      const ssize_t o = AMediaCodec_dequeueOutputBuffer(d.acodec, &info, 0);
      if (o >= 0) {
        size_t size = 0;
        uint8_t *buf = AMediaCodec_getOutputBuffer(d.acodec, (size_t)o, &size);
        pcm.clear();
        if (buf && info.size > 0)
          d.pcm.Convert(buf + info.offset, (size_t)info.size, pcm);
        AMediaCodec_releaseOutputBuffer(d.acodec, (size_t)o, false);
        if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
          d.pcm.Flush(pcm);
          audioEOS = true;
        }
        if (!pcm.empty()) {
          audio_->Push(pcm.data(), pcm.size() / kAudioChannels);
          audioFrames += pcm.size() / kAudioChannels;
        }
      } else if (o == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        AMediaFormat *f = AMediaCodec_getOutputFormat(d.acodec);
        int rate, channels;
        bool isFloat;
        android_media::PcmFormatOf(f, rate, channels, isFloat);
        AMediaFormat_delete(f);
        d.pcm.SetFormat(rate, channels, isFloat);
      } else if (o != AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
        break;
      }
    }

    if (videoEOS && audioEOS) {
      if (!loop) {
        eof_ = true;
        if (audio_)
          audio_->SetFinished(true);
        break;
      }
      // Next pass continues the timeline where the sound (or picture) ended.
      offset = audio_ ? (double)audioFrames / kAudioRate
                      : offset + lastVideo + frameDur;
      lastVideo = 0.0;
      AMediaExtractor_seekTo(ex, 0, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
      AMediaCodec_flush(d.vcodec);
      if (d.acodec)
        AMediaCodec_flush(d.acodec);
      inputEOS = false;
      videoEOS = false;
      audioEOS = d.acodec == nullptr;
    }
  }
}

#else // neither FFmpeg nor Android

struct VideoDecoder::Impl {};

bool VideoDecoder::Available() { return false; }

bool VideoDecoder::Open(const std::string &, bool, std::string *error) {
  if (error)
    *error = "video playback needs FFmpeg, which this build lacks";
  return false;
}

void VideoDecoder::Run(bool) {}

#endif

VideoDecoder::VideoDecoder() = default;
VideoDecoder::~VideoDecoder() { Close(); }

void VideoDecoder::Close() {
  stop_ = true;
  cv_.notify_all();
  if (thread_.joinable())
    thread_.join();
  std::lock_guard<std::mutex> lock(mutex_);
  frames_.clear();
  impl_.reset();
  audio_.reset();
}

bool VideoDecoder::TakeFrame(double clock, VideoFrame &out, bool first) {
  bool got = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (first && !frames_.empty()) {
      out = std::move(frames_.front());
      frames_.pop_front();
      got = true;
    }
    while (!frames_.empty() && frames_.front().pts <= clock + 0.005) {
      out = std::move(frames_.front());
      frames_.pop_front();
      got = true;
    }
  }
  if (got)
    cv_.notify_all();
  return got;
}

bool VideoDecoder::Finished() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return eof_ && frames_.empty();
}

} // namespace ilmeee

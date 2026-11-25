#include "../../../include/core_engine/MediaHttp.hpp"

#ifdef ILMEEE_HAS_FFMPEG

#include "../../../include/core_engine/MediaComponents.hpp"
#include <algorithm>
#include <cstring>
#include <curl/curl.h>
#include <memory>
#include <mutex>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/mem.h>
}

namespace ilmeee {

namespace {

constexpr int kIoBufferSize = 64 * 1024;
// Forward seeks shorter than this read through the open transfer instead of
// reconnecting with a Range request.
constexpr int64_t kSkipThrough = 256 * 1024;

// One HTTP resource read sequentially through the curl multi interface:
// Read() pumps the transfer until enough bytes have arrived, Seek() restarts
// it at an offset (Range). Servers that ignore Range answer 200 and the
// leading bytes are discarded, so seeking still works, only slower.
class CurlStream {
public:
  CurlStream(std::string url, AVIOInterruptCB interrupt)
      : url_(std::move(url)), interrupt_(interrupt) {}

  ~CurlStream() {
    if (easy_) {
      if (attached_)
        curl_multi_remove_handle(multi_, easy_);
      curl_easy_cleanup(easy_);
    }
    if (multi_)
      curl_multi_cleanup(multi_);
  }

  bool Open() {
    multi_ = curl_multi_init();
    easy_ = curl_easy_init();
    if (!multi_ || !easy_ || !Start(0))
      return false;
    Fill(1); // wait for the response so size_ / errors are known
    return !failed_;
  }

  bool SizeKnown() const { return size_ >= 0; }

  int Read(uint8_t *out, int len) {
    if (Available() == 0)
      Fill(1);
    const size_t n = std::min(Available(), (size_t)len);
    if (n == 0)
      return failed_ ? AVERROR(EIO) : AVERROR_EOF;
    std::memcpy(out, buf_.data() + head_, n);
    Consume(n);
    return (int)n;
  }

  int64_t Seek(int64_t off, int whence) {
    if (whence & AVSEEK_SIZE)
      return size_ >= 0 ? size_ : AVERROR(ENOSYS);
    whence &= ~AVSEEK_FORCE;
    int64_t target;
    switch (whence) {
    case SEEK_SET: target = off; break;
    case SEEK_CUR: target = pos_ + off; break;
    case SEEK_END:
      if (size_ < 0)
        return AVERROR(ENOSYS);
      target = size_ + off;
      break;
    default: return AVERROR(EINVAL);
    }
    if (target < 0)
      return AVERROR(EINVAL);
    if (target == pos_)
      return pos_;
    if (target > pos_ && target - pos_ <= kSkipThrough && !failed_) {
      while (pos_ < target) {
        if (Available() == 0)
          Fill(1);
        const size_t n = std::min(Available(), (size_t)(target - pos_));
        if (n == 0)
          break;
        Consume(n);
      }
      if (pos_ == target)
        return pos_;
    }
    if (!Start(target))
      return AVERROR(EIO);
    return pos_;
  }

private:
  size_t Available() const { return buf_.size() - head_; }

  void Consume(size_t n) {
    head_ += n;
    pos_ += (int64_t)n;
    if (head_ == buf_.size()) {
      buf_.clear();
      head_ = 0;
    } else if (head_ >= (size_t)kIoBufferSize * 4) {
      buf_.erase(buf_.begin(), buf_.begin() + (std::ptrdiff_t)head_);
      head_ = 0;
    }
  }

  bool Interrupted() const {
    return interrupt_.callback && interrupt_.callback(interrupt_.opaque);
  }

  bool Start(int64_t offset) {
    if (attached_) {
      curl_multi_remove_handle(multi_, easy_);
      attached_ = false;
    }
    // reset keeps the connection and DNS caches, so a seek doesn't pay for
    // another DoH lookup.
    curl_easy_reset(easy_);
    curl_easy_setopt(easy_, CURLOPT_URL, url_.c_str());
    curl_easy_setopt(easy_, CURLOPT_DOH_URL, kMediaDohUrl);
    curl_easy_setopt(easy_, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(easy_, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(easy_, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy_, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(easy_, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(easy_, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy_, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(easy_, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(easy_, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(easy_, CURLOPT_USERAGENT, "IlmeeeEngine");
    curl_easy_setopt(easy_, CURLOPT_WRITEFUNCTION, &CurlStream::OnWrite);
    curl_easy_setopt(easy_, CURLOPT_WRITEDATA, this);
    if (offset > 0)
      curl_easy_setopt(easy_, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)offset);
    buf_.clear();
    head_ = 0;
    pos_ = offset;
    startOffset_ = offset;
    skip_ = 0;
    gotBody_ = false;
    failed_ = false;
    running_ = curl_multi_add_handle(multi_, easy_) == CURLM_OK;
    attached_ = running_;
    return running_;
  }

  void Fill(size_t want) {
    while (running_ && Available() < want) {
      if (Interrupted()) {
        failed_ = true;
        running_ = false;
        break;
      }
      int still = 0;
      if (curl_multi_perform(multi_, &still) != CURLM_OK) {
        failed_ = true;
        running_ = false;
        break;
      }
      if (Available() >= want)
        break;
      if (!still) {
        int left = 0;
        while (CURLMsg *m = curl_multi_info_read(multi_, &left))
          if (m->msg == CURLMSG_DONE && m->data.result != CURLE_OK)
            failed_ = true;
        running_ = false;
        break;
      }
      curl_multi_poll(multi_, nullptr, 0, 200, nullptr);
    }
  }

  void OnFirstBody() {
    gotBody_ = true;
    long code = 0;
    curl_off_t length = -1;
    curl_easy_getinfo(easy_, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_getinfo(easy_, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length);
    const bool partial = code == 206;
    if (startOffset_ > 0 && !partial)
      skip_ = startOffset_; // Range ignored: whole body from byte 0
    if (length >= 0)
      size_ = (partial ? startOffset_ : 0) + length;
  }

  static size_t OnWrite(char *data, size_t size, size_t count, void *user) {
    auto *self = static_cast<CurlStream *>(user);
    const size_t len = size * count;
    if (!self->gotBody_)
      self->OnFirstBody();
    size_t from = 0;
    if (self->skip_ > 0) {
      from = (size_t)std::min<int64_t>(self->skip_, (int64_t)len);
      self->skip_ -= (int64_t)from;
    }
    self->buf_.insert(self->buf_.end(), data + from, data + len);
    return len;
  }

  std::string url_;
  AVIOInterruptCB interrupt_;
  CURLM *multi_ = nullptr;
  CURL *easy_ = nullptr;
  bool attached_ = false, running_ = false, failed_ = false, gotBody_ = false;
  std::vector<uint8_t> buf_;
  size_t head_ = 0;
  int64_t pos_ = 0, size_ = -1, startOffset_ = 0, skip_ = 0;
};

int ReadPacket(void *opaque, uint8_t *buf, int size) {
  return static_cast<CurlStream *>(opaque)->Read(buf, size);
}

int64_t SeekPacket(void *opaque, int64_t offset, int whence) {
  return static_cast<CurlStream *>(opaque)->Seek(offset, whence);
}

bool IsCurlIo(const AVIOContext *pb) {
  return pb && pb->read_packet == &ReadPacket;
}

void FreeCurlIo(AVIOContext **pb) {
  if (!*pb)
    return;
  delete static_cast<CurlStream *>((*pb)->opaque);
  av_freep(&(*pb)->buffer);
  avio_context_free(pb);
}

AVIOContext *MakeCurlIo(const char *url, const AVIOInterruptCB &interrupt) {
  static std::once_flag curlInit;
  std::call_once(curlInit, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
  auto stream = std::make_unique<CurlStream>(url, interrupt);
  if (!stream->Open())
    return nullptr;
  auto *buffer = static_cast<uint8_t *>(av_malloc(kIoBufferSize));
  if (!buffer)
    return nullptr;
  AVIOContext *pb = avio_alloc_context(buffer, kIoBufferSize, 0, stream.get(),
                                       &ReadPacket, nullptr, &SeekPacket);
  if (!pb) {
    av_free(buffer);
    return nullptr;
  }
  pb->seekable = stream->SizeKnown() ? AVIO_SEEKABLE_NORMAL : 0;
  stream.release();
  return pb;
}

// FFmpeg's defaults, captured from a fresh context, for non-http opens.
using IoOpenFn = int (*)(AVFormatContext *, AVIOContext **, const char *, int,
                         AVDictionary **);
using IoCloseFn = int (*)(AVFormatContext *, AVIOContext *);
IoOpenFn gDefaultOpen = nullptr;
IoCloseFn gDefaultClose = nullptr;

int IoOpen(AVFormatContext *s, AVIOContext **pb, const char *url, int flags,
           AVDictionary **options) {
  if (url && IsMediaUrl(url) && !(flags & AVIO_FLAG_WRITE)) {
    *pb = MakeCurlIo(url, s->interrupt_callback);
    return *pb ? 0 : AVERROR(EIO);
  }
  return gDefaultOpen(s, pb, url, flags, options);
}

int IoClose2(AVFormatContext *s, AVIOContext *pb) {
  if (IsCurlIo(pb)) {
    FreeCurlIo(&pb);
    return 0;
  }
  return gDefaultClose(s, pb);
}

} // namespace

int OpenMediaInput(AVFormatContext **fmt, const std::string &path,
                   const AVIOInterruptCB *interrupt) {
  if (!IsMediaUrl(path)) {
    if (interrupt) {
      *fmt = avformat_alloc_context();
      if (!*fmt)
        return AVERROR(ENOMEM);
      (*fmt)->interrupt_callback = *interrupt;
    }
    return avformat_open_input(fmt, path.c_str(), nullptr, nullptr);
  }

  AVFormatContext *ctx = avformat_alloc_context();
  if (!ctx)
    return AVERROR(ENOMEM);
  static std::once_flag captured;
  std::call_once(captured, [ctx] {
    gDefaultOpen = ctx->io_open;
    gDefaultClose = ctx->io_close2;
  });
  if (interrupt)
    ctx->interrupt_callback = *interrupt;
  AVIOContext *pb = MakeCurlIo(path.c_str(), ctx->interrupt_callback);
  if (!pb) {
    avformat_free_context(ctx);
    return AVERROR(EIO);
  }
  ctx->pb = pb;
  ctx->io_open = &IoOpen;
  ctx->io_close2 = &IoClose2;

  // HLS "persistent HTTP" reuses FFmpeg's own http URLContext for the next
  // request and asserts if the IO isn't one; our curl IO already keeps the
  // connection alive, so turn it off.
  AVDictionary *opts = nullptr;
  av_dict_set(&opts, "http_persistent", "0", 0);
  const int ret = avformat_open_input(&ctx, path.c_str(), nullptr, &opts);
  av_dict_free(&opts);
  if (ret < 0) {
    // ctx is already freed; custom IO is never closed by FFmpeg.
    FreeCurlIo(&pb);
    *fmt = nullptr;
    return ret;
  }
  *fmt = ctx;
  return ret;
}

void CloseMediaInput(AVFormatContext **fmt) {
  AVIOContext *pb = (*fmt && IsCurlIo((*fmt)->pb)) ? (*fmt)->pb : nullptr;
  avformat_close_input(fmt);
  FreeCurlIo(&pb);
}

} // namespace ilmeee

#endif // ILMEEE_HAS_FFMPEG

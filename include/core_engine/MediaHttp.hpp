#pragma once
// Network input for the audio/video players. FFmpeg's own http(s) protocol
// resolves hosts through the system resolver; instead, every http(s) media
// URL is read through libcurl with DNS-over-HTTPS to kMediaDohUrl. That
// covers the main URL and anything the demuxer opens on its own (HLS
// playlists and segments) via AVFormatContext::io_open. Local files go to
// FFmpeg untouched.

#include <string>

struct AVFormatContext;
struct AVIOInterruptCB;

namespace ilmeee {

// Cloudflare 1.1.1.1. DoH rather than --dns-servers style resolving: the
// latter needs a c-ares build of libcurl, which most distros don't ship.
inline constexpr const char *kMediaDohUrl = "https://1.1.1.1/dns-query";

#ifdef ILMEEE_HAS_FFMPEG
// Drop-in for avformat_open_input(fmt, path, nullptr, nullptr). `interrupt`
// (optional) aborts blocking network reads, e.g. when a player closes.
int OpenMediaInput(AVFormatContext **fmt, const std::string &path,
                   const AVIOInterruptCB *interrupt = nullptr);
// Drop-in for avformat_close_input; also frees the curl-backed IO.
void CloseMediaInput(AVFormatContext **fmt);
#endif

} // namespace ilmeee

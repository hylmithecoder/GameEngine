#include "../../../include/core_engine/MediaSystem.hpp"
#include "../../../include/core_engine/Debugger.hpp"
#include "../../../include/core_engine/SceneRenderer.hpp"
#include <algorithm>
#include <cmath>

namespace ilmeee {

std::string MediaSystem::ResolvePath(const std::string &clip) const {
  if (clip.empty())
    return {};
  const std::filesystem::path p(clip);
  return p.is_absolute() || root_.empty() ? clip : (root_ / p).string();
}

void MediaSystem::SetError(const std::string &clip,
                           const std::string &message) {
  auto &slot = errors_[clip];
  if (slot != message && !message.empty())
    Debug::Log("Media '" + clip + "': " + message, Debug::LogLevel::WARNING);
  slot = message;
}

std::string MediaSystem::ErrorFor(const std::string &clip) const {
  auto it = errors_.find(clip);
  return it == errors_.end() ? std::string() : it->second;
}

bool MediaSystem::Preview(const std::string &clip, std::string *error) {
  StopPreview();
  if (state_ != State::Stopped)
    return false;
  std::string err;
  auto data = audio_.LoadClip(ResolvePath(clip), &err);
  if (!data) {
    SetError(clip, err);
    if (error)
      *error = err;
    return false;
  }
  SetError(clip, "");
  preview_ = audio_.PlayClip(data, false);
  return preview_ != 0;
}

void MediaSystem::StopPreview() {
  if (preview_)
    audio_.StopVoice(preview_);
  preview_ = 0;
}

bool MediaSystem::IsPreviewing() const {
  return preview_ != 0 && audio_.IsVoiceActive(preview_);
}

void MediaSystem::ResetVideo(SceneRenderer &renderer, VideoSlot &slot) {
  if (slot.voice)
    audio_.StopVoice(slot.voice);
  slot.voice = 0;
  slot.decoder.reset();
  if (slot.texture)
    renderer.DestroyDynamicTexture(slot.texture);
  slot.texture = 0;
  slot.texW = slot.texH = 0;
  slot.clock = 0.0;
  slot.shown = false;
  slot.lastFramePts = 0.0;
  slot.warnedSlow = false;
  slot.failed = false;
  slot.clip.clear();
}

void MediaSystem::Start(SceneRenderer &renderer) {
  StopPreview();
  audio_.SetPaused(false);
  const size_t n = renderer.GetMesh3DCount();
  // Videos restart from the beginning in play mode.
  for (VideoSlot &slot : videos_)
    ResetVideo(renderer, slot);
  for (VideoSlot &slot : videos_)
    slot.playing = true;

  sources_.assign(n, 0);
  for (size_t i = 0; i < n; ++i) {
    const auto request = audioRequests_.find(i);
    if (request == audioRequests_.end() ? renderer.meshes3d[i].audio.playOnStart
                                        : request->second)
      sources_[i] = StartAudio(renderer, i);
  }
  audioRequests_.clear();
  UpdateAudioGains(renderer);
}

VoiceId MediaSystem::StartAudio(SceneRenderer &renderer, size_t mesh) {
  const AudioSourceComponent &a = renderer.meshes3d[mesh].audio;
  if (!a.enabled || a.clip.empty())
    return 0;
  std::string err;
  auto clip = audio_.LoadClip(ResolvePath(a.clip), &err);
  SetError(a.clip, clip ? "" : err);
  return clip ? audio_.PlayClip(clip, a.loop) : 0;
}

bool MediaSystem::PlayAudio(SceneRenderer &renderer, size_t mesh) {
  if (mesh >= renderer.GetMesh3DCount())
    return false;
  if (state_ == State::Stopped) {
    audioRequests_[mesh] = true;
    return true;
  }
  StopAudio(mesh);
  if (sources_.size() <= mesh)
    sources_.resize(mesh + 1, 0);
  sources_[mesh] = StartAudio(renderer, mesh);
  UpdateAudioGains(renderer);
  return sources_[mesh] != 0;
}

void MediaSystem::StopAudio(size_t mesh) {
  if (state_ == State::Stopped) {
    audioRequests_[mesh] = false;
    return;
  }
  if (mesh < sources_.size() && sources_[mesh]) {
    audio_.StopVoice(sources_[mesh]);
    sources_[mesh] = 0;
  }
}

bool MediaSystem::IsAudioPlaying(const SceneRenderer &renderer,
                                 size_t mesh) const {
  if (mesh >= renderer.GetMesh3DCount())
    return false;
  if (state_ == State::Stopped) {
    const AudioSourceComponent &a = renderer.meshes3d[mesh].audio;
    const auto request = audioRequests_.find(mesh);
    return a.enabled && !a.clip.empty() &&
           (request == audioRequests_.end() ? a.playOnStart : request->second);
  }
  return mesh < sources_.size() && sources_[mesh] &&
         audio_.IsVoiceActive(sources_[mesh]);
}

MediaSystem::VideoSlot &MediaSystem::VideoAt(size_t mesh) {
  while (videos_.size() <= mesh) {
    videos_.emplace_back();
    videos_.back().playing = state_ != State::Stopped;
  }
  return videos_[mesh];
}

bool MediaSystem::PlayVideo(SceneRenderer &renderer, size_t mesh) {
  if (mesh >= renderer.GetMesh3DCount())
    return false;
  VideoSlot &slot = VideoAt(mesh);
  slot.request = 1;
  // Before playback begins Start() rewinds every video anyway.
  if (state_ != State::Stopped) {
    ResetVideo(renderer, slot);
    slot.playing = true;
  }
  const VideoPlayerComponent &v = renderer.meshes3d[mesh].video;
  return v.enabled && !v.clip.empty();
}

void MediaSystem::StopVideo(size_t mesh) { VideoAt(mesh).request = -1; }

bool MediaSystem::IsVideoPlaying(const SceneRenderer &renderer,
                                 size_t mesh) const {
  if (mesh >= renderer.GetMesh3DCount())
    return false;
  const VideoPlayerComponent &v = renderer.meshes3d[mesh].video;
  const int request = mesh < videos_.size() ? videos_[mesh].request : 0;
  if (!v.enabled || v.clip.empty() || request < 0 ||
      (request == 0 && !v.playOnStart))
    return false;
  if (state_ == State::Stopped || mesh >= videos_.size())
    return true; // starts with playback
  const VideoSlot &slot = videos_[mesh];
  if (slot.failed)
    return false;
  // No decoder yet: Play() was just called, it opens on the next update.
  return !slot.decoder || slot.loop || !slot.decoder->Finished();
}

void MediaSystem::StopPlayback(SceneRenderer &renderer) {
  audio_.StopAll();
  preview_ = 0;
  sources_.clear();
  audioRequests_.clear();
  audio_.SetPaused(false);
  for (VideoSlot &slot : videos_) {
    ResetVideo(renderer, slot);
    slot.playing = false;
    slot.request = 0;
  }
}

void MediaSystem::UpdateAudioGains(SceneRenderer &renderer) {
  glm::vec3 lpos(0.0f), lright(1.0f, 0.0f, 0.0f), lfwd(0.0f, 0.0f, -1.0f);
  const bool hasListener = renderer.GetPlayerCameraPose(lpos, lright, lfwd);
  for (size_t i = 0; i < sources_.size() && i < renderer.GetMesh3DCount();
       ++i) {
    if (!sources_[i])
      continue;
    const AudioSourceComponent &a = renderer.meshes3d[i].audio;
    const float vol = std::max(0.0f, a.volume);
    if (!a.spatial || !hasListener) {
      audio_.SetVoiceGains(sources_[i], vol, vol, false);
      continue;
    }
    const glm::vec3 to = renderer.GetMesh3DWorldPosition(i) - lpos;
    const float dist = glm::length(to);
    const float minD = std::max(0.0f, a.minDistance);
    const float maxD = std::max(minD + 1e-3f, a.maxDistance);
    const float att =
        dist <= minD ? 1.0f
                     : std::clamp(1.0f - (dist - minD) / (maxD - minD), 0.0f,
                                  1.0f);
    // Direction in the listener's frame: side = -1 left .. +1 right,
    // front = +1 ahead .. -1 behind.
    const glm::vec3 dir = dist > 1e-4f ? to / dist : lfwd;
    const float side = std::clamp(glm::dot(dir, lright), -1.0f, 1.0f);
    const float behind = std::clamp(-glm::dot(dir, lfwd), 0.0f, 1.0f);
    // Constant-power pan over the full range: a source right beside the
    // listener is heard in that ear only (both at 1.0 straight ahead).
    const float theta = (side + 1.0f) * 0.25f * 3.14159265f;
    const float g = vol * att * (1.0f - 0.25f * behind) * 1.41421356f;
    // Interaural time difference (~0.66 ms max) on the far ear, and head
    // shadow: the far ear loses highs (20 kHz → ~3 kHz cutoff at 90°).
    // Sounds from behind are a little duller in both ears.
    const float far = std::fabs(side);
    const float itd = far * 0.00066f * kAudioRate;
    const auto onePole = [](float cutoffHz) {
      return cutoffHz >= 18000.0f
                 ? 1.0f
                 : 1.0f - std::exp(-2.0f * 3.14159265f * cutoffHz /
                                   (float)kAudioRate);
    };
    const float rear = 1.0f - 0.55f * behind;
    const float nearCut = 20000.0f * rear;
    const float farCut = 20000.0f * std::pow(0.15f, far) * rear;
    VoiceSpatial s;
    s.mono = true;
    s.gainL = g * std::cos(theta);
    s.gainR = g * std::sin(theta);
    s.delayL = side > 0.0f ? itd : 0.0f;
    s.delayR = side < 0.0f ? itd : 0.0f;
    s.lowpassL = onePole(side > 0.0f ? farCut : nearCut);
    s.lowpassR = onePole(side < 0.0f ? farCut : nearCut);
    audio_.SetVoiceSpatial(sources_[i], s);
  }
}

void MediaSystem::UpdateVideos(SceneRenderer &renderer, bool advance,
                               float dt) {
  const size_t n = renderer.GetMesh3DCount();
  if (videos_.size() > n) {
    for (size_t i = n; i < videos_.size(); ++i)
      ResetVideo(renderer, videos_[i]);
    videos_.resize(n);
  }
  while (videos_.size() < n) {
    videos_.emplace_back();
    videos_.back().playing = state_ != State::Stopped;
  }

  for (size_t i = 0; i < n; ++i) {
    auto &mesh = renderer.meshes3d[i];
    VideoSlot &slot = videos_[i];
    const VideoPlayerComponent &v = mesh.video;
    const bool wanted =
        v.enabled && !v.clip.empty() &&
        (state_ == State::Stopped || slot.request > 0 ||
         (slot.request == 0 && v.playOnStart));
    if (!wanted) {
      if (!slot.clip.empty() || slot.texture)
        ResetVideo(renderer, slot);
      mesh.videoTexture = VK_NULL_HANDLE;
      continue;
    }
    if (slot.clip != v.clip || slot.loop != v.loop) {
      const bool playing = slot.playing;
      ResetVideo(renderer, slot);
      slot.playing = playing;
      slot.clip = v.clip;
      slot.loop = v.loop;
    }
    if (slot.failed) {
      mesh.videoTexture = VK_NULL_HANDLE;
      continue;
    }
    if (!slot.decoder) {
      slot.decoder = std::make_unique<VideoDecoder>();
      std::string err;
      // Poster mode never needs to wrap around.
      if (!slot.decoder->Open(ResolvePath(slot.clip),
                              slot.playing && slot.loop, &err)) {
        SetError(slot.clip, err);
        slot.decoder.reset();
        slot.failed = true;
        mesh.videoTexture = VK_NULL_HANDLE;
        continue;
      }
      SetError(slot.clip, "");
      if (slot.playing && slot.decoder->Audio())
        slot.voice = audio_.PlayStream(slot.decoder->Audio());
    }

    // Clock: the sound track while it plays (keeps lips in sync), the
    // frame time otherwise.
    if (slot.playing && advance) {
      if (slot.voice && audio_.IsVoiceActive(slot.voice))
        slot.clock = audio_.VoicePlayedSeconds(slot.voice);
      else
        slot.clock += dt;
    }
    if (slot.voice)
      audio_.SetVoiceGains(slot.voice, std::max(0.0f, v.volume),
                           std::max(0.0f, v.volume), false);

    VideoFrame frame;
    if (slot.decoder->TakeFrame(slot.playing ? slot.clock : 0.0, frame,
                                !slot.shown)) {
      if (!slot.texture || frame.width != slot.texW ||
          frame.height != slot.texH) {
        if (slot.texture)
          renderer.DestroyDynamicTexture(slot.texture);
        slot.texture = renderer.CreateDynamicTexture(frame.width, frame.height);
        slot.texW = frame.width;
        slot.texH = frame.height;
      }
      if (slot.texture)
        renderer.UpdateDynamicTexture(slot.texture, frame.rgba.data());
      slot.shown = true;
      slot.lastFramePts = frame.pts;
      slot.decoder->Recycle(std::move(frame.rgba));
    }
    // The picture falling well behind the sound means this device cannot
    // decode the file in real time (e.g. 4K AV1 on a phone): say so once.
    if (slot.playing && !slot.warnedSlow && slot.clock > 3.0 &&
        slot.clock - slot.lastFramePts > 1.0 && !slot.decoder->Finished()) {
      slot.warnedSlow = true;
      SetError(slot.clip, "decodes slower than real time on this device "
                          "(picture " +
                              std::to_string(slot.clock - slot.lastFramePts)
                                  .substr(0, 4) +
                              " s behind); use a smaller H.264 video");
    }
    mesh.videoTexture = slot.texture
                            ? renderer.GetDynamicTextureDescriptor(slot.texture)
                            : VK_NULL_HANDLE;
  }
}

void MediaSystem::Update(SceneRenderer &renderer, State state, float dt) {
  if (state != state_) {
    const State prev = state_;
    state_ = state;
    if (state == State::Stopped)
      StopPlayback(renderer);
    else if (prev == State::Stopped)
      Start(renderer);
    audio_.SetPaused(state == State::Paused);
  }
  if (state_ != State::Stopped)
    UpdateAudioGains(renderer);
  UpdateVideos(renderer, state_ == State::Playing, std::max(0.0f, dt));
  audio_.Update();
  if (preview_ && !audio_.IsVoiceActive(preview_))
    preview_ = 0;
}

void MediaSystem::Shutdown(SceneRenderer &renderer) {
  audio_.StopAll();
  preview_ = 0;
  sources_.clear();
  for (VideoSlot &slot : videos_)
    ResetVideo(renderer, slot);
  videos_.clear();
  for (auto &m : renderer.meshes3d)
    m.videoTexture = VK_NULL_HANDLE;
  audio_.ClearClipCache();
  state_ = State::Stopped;
}

} // namespace ilmeee

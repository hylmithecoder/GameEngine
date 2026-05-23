#include "../../../../../include/core_engine/core_editor/panels/VideoPlayerPanel.hpp"
#include "../../../../../include/vulkan/vulkanhandler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <imgui.h>

namespace Ilmeee {

VideoPlayerPanel::VideoPlayerPanel(VulkanHandler *h) : handler(h) {}

std::string VideoPlayerPanel::FormatTime(double seconds) {
  if (seconds < 0.0 || !std::isfinite(seconds))
    seconds = 0.0;
  int total = static_cast<int>(seconds);
  int h = total / 3600;
  int m = (total % 3600) / 60;
  int s = total % 60;
  char buf[16];
  if (h > 0) {
    std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
  } else {
    std::snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
  }
  return std::string(buf);
}

bool VideoPlayerPanel::Open(const std::string &path) {
  if (!handler)
    return false;
  if (path.empty())
    return false;
  std::strncpy(filePathBuf, path.c_str(), sizeof(filePathBuf) - 1);
  filePathBuf[sizeof(filePathBuf) - 1] = '\0';

  if (handler->OpenFileVideo(path.c_str())) {
    handler->openAudio();
    handler->isPlaying = true;
    paused = false;
    return true;
  }
  return false;
}

void VideoPlayerPanel::Play() {
  if (!handler)
    return;
  paused = false;
  handler->isPlaying = true;
}

void VideoPlayerPanel::Pause() {
  if (!handler)
    return;
  paused = true;
}

void VideoPlayerPanel::TogglePause() { paused = !paused; }

void VideoPlayerPanel::Stop() {
  if (!handler)
    return;
  paused = true;
  handler->SeekTo(0.0);
}

void VideoPlayerPanel::SeekTo(double seconds) {
  if (!handler)
    return;
  handler->SeekTo(seconds);
}

void VideoPlayerPanel::DrawVideoFrame() {
  if (!handler)
    return;

  // Don't submit a draw call with an unallocated descriptor — the
  // Mesa anv driver crashes inside vkCmdBindDescriptorSets if the
  // handle is non-null but invalid, and ImGui has no way to validate.
  // VulkanHandler now initializes videoDescriptorSet to VK_NULL_HANDLE
  // at declaration; createVideoTexture() fills it in lazily.
  VkDescriptorSet ds = handler->getVideoDescriptorSet();
  if (ds == VK_NULL_HANDLE) {
    ImGui::TextDisabled("Video frame not ready");
    return;
  }
  ImTextureID tex = (ImTextureID)ds;

  const float aspect = handler->height > 0
                           ? static_cast<float>(handler->width) /
                                 static_cast<float>(handler->height)
                           : 16.0f / 9.0f;

  ImVec2 avail = ImGui::GetContentRegionAvail();
  // Reserve some vertical room for the controls below.
  const float controlsHeight = 70.0f;
  avail.y = std::max(60.0f, avail.y - controlsHeight);

  float w = avail.x;
  float h = w / aspect;
  if (h > avail.y) {
    h = avail.y;
    w = h * aspect;
  }

  // Center horizontally so the frame doesn't stick to the left edge.
  float xOffset = (avail.x - w) * 0.5f;
  if (xOffset > 0.0f) {
    ImGui::Dummy(ImVec2(xOffset, 0));
    ImGui::SameLine(0.0f, 0.0f);
  }

  ImGui::Image(tex, ImVec2(w, h));
}

void VideoPlayerPanel::DrawSeekBar() {
  if (!handler)
    return;

  // Hard sanity-clamp: ImGui's slider asserts on NaN/inf or values
  // outside FLT_MAX/2, and FFmpeg can hand us oddities for streams
  // without a known duration (e.g. live sources reporting -1).
  auto sanitize = [](double v) -> double {
    if (!std::isfinite(v) || v < 0.0)
      return 0.0;
    if (v > 1e7) // ~115 days — anything beyond is not a real media file
      return 1e7;
    return v;
  };

  const double dur = sanitize(handler->duration);
  const double cur = sanitize(handler->currentTime);

  // While dragging, show drag value (don't fight the user with playback
  // updates). When released, commit the seek.
  double displayValue = seekDragging ? sanitize(seekDragValue) : cur;

  ImGui::Text("%s", FormatTime(displayValue).c_str());
  ImGui::SameLine();

  ImGui::SetNextItemWidth(-60.0f);
  float seekFloat = static_cast<float>(displayValue);
  const float maxFloat = std::max(static_cast<float>(dur), 0.001f);

  bool changed = ImGui::SliderFloat("##seek", &seekFloat, 0.0f, maxFloat, "",
                                    ImGuiSliderFlags_NoInput);

  if (changed) {
    seekDragging = true;
    seekDragValue = seekFloat;
  }
  if (seekDragging && ImGui::IsItemDeactivatedAfterEdit()) {
    SeekTo(seekDragValue);
    seekDragging = false;
  }

  ImGui::SameLine();
  ImGui::Text("%s", FormatTime(dur).c_str());
}

void VideoPlayerPanel::DrawTransportBar() {
  if (!handler)
    return;

  const bool hasMedia = handler->isPlaying || handler->duration > 0.0;

  if (ImGui::Button(paused ? "Play" : "Pause", ImVec2(72, 0))) {
    if (hasMedia)
      TogglePause();
  }
  ImGui::SameLine();
  if (ImGui::Button("Stop", ImVec2(60, 0))) {
    if (hasMedia)
      Stop();
  }
  ImGui::SameLine();
  if (ImGui::Button("-10s", ImVec2(50, 0))) {
    if (hasMedia)
      SeekTo(handler->currentTime - 10.0);
  }
  ImGui::SameLine();
  if (ImGui::Button("+10s", ImVec2(50, 0))) {
    if (hasMedia)
      SeekTo(handler->currentTime + 10.0);
  }

  ImGui::SameLine();
  ImGui::Checkbox("Loop", &loop);
}

void VideoPlayerPanel::DrawInfoStrip() {
  if (!handler)
    return;
  if (handler->width == 0 || handler->height == 0) {
    ImGui::TextDisabled("No media loaded.");
    return;
  }
  ImGui::TextDisabled("%ux%u  @ %.2f fps", handler->width, handler->height,
                      handler->fps);
  if (handler->audioCodecContext) {
    ImGui::SameLine();
    ImGui::TextDisabled("  |  audio %d Hz, %d ch",
                        handler->audioCodecContext->sample_rate,
                        handler->audioCodecContext->ch_layout.nb_channels);
  }
}

void VideoPlayerPanel::OnImGuiRender() {
  // File row — load a new clip.
  ImGui::SetNextItemWidth(-110.0f);
  ImGui::InputText("##path", filePathBuf, IM_ARRAYSIZE(filePathBuf));
  ImGui::SameLine();
  if (ImGui::Button("Open", ImVec2(100, 0))) {
    if (filePathBuf[0] != '\0')
      Open(filePathBuf);
  }

  ImGui::Separator();

  DrawVideoFrame();

  // While not paused and we have media, drive the underlying decoder.
  if (handler && handler->isPlaying && !paused) {
    handler->updateBothVideoAndAudio();
  }

  ImGui::Separator();
  DrawTransportBar();
  DrawSeekBar();
  DrawInfoStrip();
}

} // namespace Ilmeee

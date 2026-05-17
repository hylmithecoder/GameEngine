#pragma once

#include "Panel.hpp"
#include <string>

class VulkanHandler;

namespace Ilmeee {

// Editor panel that wraps the engine's VulkanHandler-based video
// pipeline behind a transport UI (open, play/pause, stop, seek bar,
// volume, loop). The panel owns no decoding state — it delegates
// everything to the VulkanHandler passed at construction time.
//
// Decoupled from MainWindow so the same panel can be reused in a
// future runtime cutscene player or in the VN engine's BG-video slot.
class VideoPlayerPanel : public Panel {
public:
  explicit VideoPlayerPanel(VulkanHandler *handler);

  void OnImGuiRender() override;
  const char *GetName() const override { return "Video Player"; }
  DockHint GetDockHint() const override { return DockHint::Bottom; }

  // Programmatic control (e.g. cutscene trigger from a script).
  bool Open(const std::string &path);
  void Play();
  void Pause();
  void TogglePause();
  void Stop();
  void SeekTo(double seconds);
  bool IsPaused() const { return paused; }

private:
  void DrawTransportBar();
  void DrawSeekBar();
  void DrawVideoFrame();
  void DrawInfoStrip();

  static std::string FormatTime(double seconds);

  VulkanHandler *handler = nullptr;
  bool paused = false;
  bool loop = true;
  bool seekDragging = false;
  double seekDragValue = 0.0;
  char filePathBuf[512] = {0};
};

} // namespace Ilmeee

#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>

namespace Ilmeee {

// Minimal contract the editor needs from any scene renderer (2D today,
// 3D later). The editor talks to this — not to a concrete class —
// so adding a 3D renderer later doesn't ripple through panel code.
//
// SceneRenderer2D and a future SceneRenderer3D should both inherit
// from IViewportRenderer. Adopting the interface is opt-in: existing
// callers that talk to SceneRenderer2D directly keep working.
class IViewportRenderer {
public:
  virtual ~IViewportRenderer() = default;

  // Render the scene into the renderer's offscreen target. Called once
  // per editor frame, before ImGui composition.
  virtual void Render() = 0;

  // The viewport panel calls this whenever the panel's content region
  // changes size. Implementations should recreate render targets as
  // needed but should be cheap when called with the same size twice.
  virtual void Resize(uint32_t width, uint32_t height) = 0;

  // Return a Vulkan descriptor set that ImGui can sample to display
  // the current offscreen frame inside a panel. Stable across frames
  // unless Resize() was called.
  virtual VkDescriptorSet GetImGuiTexture() const = 0;

  // For UI labelling (panel title, log prefixes). Stable string.
  virtual const char *GetName() const { return "Viewport"; }

  // True if the renderer needs an explicit 3D perspective camera
  // (used by the editor to decide which gizmo set + camera controls
  // to show). 2D renderers return false.
  virtual bool Is3D() const { return false; }
};

} // namespace Ilmeee

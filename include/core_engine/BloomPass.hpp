#pragma once
// Bloom post-process for SceneRenderer's offscreen targets.
//
// After the scene render pass, Record():
//   1. prefilter: target (RGBA8, SHADER_READ_ONLY) -> level 0 (half size,
//      RGBA16F), keeping only pixels above the threshold (soft knee);
//   2. downsample level i-1 -> level i (13-tap);
//   3. upsample level i -> level i-1, added (tent filter, weight = radius);
//   4. composite: level 0 added onto the target (intensity * tint).
// The target stays SHADER_READ_ONLY afterwards, so whatever displays it
// (ImGui image, readback) does not change.
//
// One level chain per target view, created on first use and freed with
// Release(view) before that view is destroyed (resize, shutdown).

#include "BloomSettings.hpp"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace ilmeee {

class BloomPass {
public:
  using ShaderLoader = std::function<VkShaderModule(const std::string &)>;
  using MemoryTypeFinder =
      std::function<uint32_t(uint32_t typeBits, VkMemoryPropertyFlags)>;

  // False (bloom stays off, Record does nothing) when shaders or Vulkan
  // objects are missing; `error` says why.
  bool Init(VkDevice device, VkFormat targetFormat, const ShaderLoader &load,
            const MemoryTypeFinder &memoryType, std::string *error = nullptr);
  void Destroy();
  bool Ready() const { return ready_; }

  // Records the passes for `targetView` (w x h, format given to Init) into
  // `cmd`, outside any render pass. No-op when not ready or disabled.
  void Record(VkCommandBuffer cmd, VkImageView targetView, int w, int h,
              const BloomSettings &settings);
  // Frees the chain of `targetView`. Call (device idle) before destroying it.
  void Release(VkImageView targetView);

private:
  struct Level {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    int w = 0, h = 0;
  };
  struct Chain {
    int w = 0, h = 0;
    std::vector<Level> levels;
    VkDescriptorSet sourceSet = VK_NULL_HANDLE;
    VkFramebuffer composite = VK_NULL_HANDLE;
  };
  bool CreateChain(Chain &chain, VkImageView target, int w, int h);
  void DestroyChain(Chain &chain);
  VkDescriptorSet AllocateSet(VkImageView view);
  VkRenderPass MakePass(VkFormat format, bool load);
  VkPipeline MakePipeline(VkRenderPass pass, VkShaderModule frag,
                          bool additive);
  void Pass(VkCommandBuffer cmd, VkRenderPass pass, VkFramebuffer fb, int w,
            int h, VkPipeline pipeline, VkDescriptorSet set,
            const float (&push)[12]);

  VkDevice device_ = VK_NULL_HANDLE;
  VkFormat targetFormat_ = VK_FORMAT_R8G8B8A8_UNORM;
  MemoryTypeFinder memoryType_;
  bool ready_ = false;
  VkSampler sampler_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
  VkDescriptorPool pool_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  VkRenderPass clearPass_ = VK_NULL_HANDLE;  // RGBA16F, contents replaced
  VkRenderPass loadPass_ = VK_NULL_HANDLE;   // RGBA16F, added onto
  VkRenderPass targetPass_ = VK_NULL_HANDLE; // target format, added onto
  VkShaderModule vert_ = VK_NULL_HANDLE, down_ = VK_NULL_HANDLE,
                 up_ = VK_NULL_HANDLE;
  VkPipeline downPipeline_ = VK_NULL_HANDLE;
  VkPipeline upPipeline_ = VK_NULL_HANDLE;
  VkPipeline compositePipeline_ = VK_NULL_HANDLE;
  std::unordered_map<VkImageView, Chain> chains_;
};

} // namespace ilmeee

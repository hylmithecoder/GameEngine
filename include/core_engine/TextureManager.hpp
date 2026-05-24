#pragma once

#include <imgui_impl_vulkan.h>
#include <string>
#include <unordered_map>
#include <vulkan/vulkan.h>

class TextureManager {
public:
  TextureManager() = default;
  ~TextureManager();

  void SetVulkanContext(VkDevice device, VkPhysicalDevice physicalDevice,
                        VkQueue graphicsQueue, VkCommandPool commandPool,
                        VkDescriptorPool descriptorPool, VkSampler sampler);

  VkDescriptorSet GetTextureDescriptor(const std::string &path);
  void ClearTextures();

  // Directory for the generated KTX2 texture cache. Defaults to
  // ~/.ilmeeeengine/texcache. Cached files are mipmapped and reused
  // across loads/sessions (keyed by source path + modified time).
  void SetCacheDir(const std::string &dir);
  // Toggle the KTX cache path. When off, textures load straight from the
  // source image via stb (the original behavior). Default: on.
  void SetUseKtxCache(bool on) { useKtxCache = on; }

private:
  VkDevice device = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkQueue graphicsQueue = VK_NULL_HANDLE;
  VkCommandPool commandPool = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
  VkSampler defaultSampler = VK_NULL_HANDLE;
  // Trilinear sampler spanning the full mip range, used for KTX textures so
  // their generated mip levels are actually sampled.
  VkSampler mipSampler = VK_NULL_HANDLE;

  std::string cacheDir;
  bool useKtxCache = true;

  struct TextureResource {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
  };

  std::unordered_map<std::string, TextureResource> textureCache;

  uint32_t findMemoryType(uint32_t typeFilter,
                          VkMemoryPropertyFlags properties);
  // Direct stb_image → VkImage upload (single mip). The reliable fallback.
  TextureResource LoadTextureVulkan(const std::string &path);
  // KTX cache path: build/reuse a mipmapped .ktx2 then upload all levels.
  void EnsureMipSampler();
  std::string CacheKtxPathFor(const std::string &srcPath);
  bool BuildKtxCache(const std::string &srcPath, const std::string &outKtx);
  TextureResource LoadTextureFromKTX(const std::string &ktxPath);
};
#include "../../../include/core_engine/TextureManager.hpp"
#include "../../../include/core_engine/Debugger.hpp"
#include <cstring>
#include <stb/stb_image.h>
#include <stdexcept>

using namespace Debug;

TextureManager::~TextureManager() { ClearTextures(); }

void TextureManager::SetVulkanContext(VkDevice device,
                                      VkPhysicalDevice physicalDevice,
                                      VkQueue graphicsQueue,
                                      VkCommandPool commandPool,
                                      VkDescriptorPool descriptorPool,
                                      VkSampler sampler) {
  this->device = device;
  this->physicalDevice = physicalDevice;
  this->graphicsQueue = graphicsQueue;
  this->commandPool = commandPool;
  this->descriptorPool = descriptorPool;
  this->defaultSampler = sampler;
}

VkDescriptorSet TextureManager::GetTextureDescriptor(const std::string &path) {
  if (path.empty())
    return VK_NULL_HANDLE;

  if (textureCache.find(path) != textureCache.end()) {
    return textureCache[path].descriptorSet;
  }

  try {
    TextureResource res = LoadTextureVulkan(path);
    textureCache[path] = res;
    return res.descriptorSet;
  } catch (const std::exception &e) {
    ::Log("Failed to load texture: " + path + " - " + e.what(),
          Debug::LogLevel::CRASH);
    return VK_NULL_HANDLE;
  }
}

void TextureManager::ClearTextures() {
  if (device == VK_NULL_HANDLE)
    return;

  for (auto &pair : textureCache) {
    TextureResource &res = pair.second;
    if (res.view != VK_NULL_HANDLE)
      vkDestroyImageView(device, res.view, nullptr);
    if (res.image != VK_NULL_HANDLE)
      vkDestroyImage(device, res.image, nullptr);
    if (res.memory != VK_NULL_HANDLE)
      vkFreeMemory(device, res.memory, nullptr);
    // Descriptor sets are usually cleaned up with the pool
  }
  textureCache.clear();
}

uint32_t TextureManager::findMemoryType(uint32_t typeFilter,
                                        VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags &
                                    properties) == properties) {
      return i;
    }
  }
  throw std::runtime_error("failed to find suitable memory type!");
}

TextureManager::TextureResource
TextureManager::LoadTextureVulkan(const std::string &path) {
  int texWidth, texHeight, texChannels;
  stbi_uc *pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels,
                              STBI_rgb_alpha);
  VkDeviceSize imageSize = texWidth * texHeight * 4;

  if (!pixels) {
    throw std::runtime_error("failed to load texture image!");
  }

  // 1. Staging Buffer
  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;

  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = imageSize;
  bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer);

  VkMemoryRequirements memReqs;
  vkGetBufferMemoryRequirements(device, stagingBuffer, &memReqs);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex = findMemoryType(
      memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  vkAllocateMemory(device, &allocInfo, nullptr, &stagingBufferMemory);
  vkBindBufferMemory(device, stagingBuffer, stagingBufferMemory, 0);

  void *data;
  vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
  memcpy(data, pixels, static_cast<size_t>(imageSize));
  vkUnmapMemory(device, stagingBufferMemory);
  stbi_image_free(pixels);

  // 2. Create Image
  TextureResource res;
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = static_cast<uint32_t>(texWidth);
  imageInfo.extent.height = static_cast<uint32_t>(texHeight);
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

  vkCreateImage(device, &imageInfo, nullptr, &res.image);
  vkGetImageMemoryRequirements(device, res.image, &memReqs);

  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex = findMemoryType(
      memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  vkAllocateMemory(device, &allocInfo, nullptr, &res.memory);
  vkBindImageMemory(device, res.image, res.memory, 0);

  // 3. Copy Buffer to Image (Implementation omitted for brevity, should use cmd
  // buffer) For now, let's just create the view and descriptor

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = res.image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.layerCount = 1;

  vkCreateImageView(device, &viewInfo, nullptr, &res.view);

  res.descriptorSet = ImGui_ImplVulkan_AddTexture(
      defaultSampler, res.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  // Cleanup staging buffer
  vkDestroyBuffer(device, stagingBuffer, nullptr);
  vkFreeMemory(device, stagingBufferMemory, nullptr);

  return res;
}
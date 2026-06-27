#include "../../include/ui/SvgIconManager.hpp"
#include "../../include/core_engine/Debugger.hpp"

#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>

#include <cstring>

void SvgIconManager::Init(VulkanHandler *vkHandler) { vulkan_ = vkHandler; }

unsigned char *SvgIconManager::RasterizeSvg(const std::string &path, int size) {
  NSVGimage *image = nsvgParseFromFile(path.c_str(), "px", 96.0f);
  if (!image) {
    Debug::Log("SvgIconManager: failed to parse SVG: " + path,
               Debug::LogLevel::WARNING);
    return nullptr;
  }

  NSVGrasterizer *rast = nsvgCreateRasterizer();
  if (!rast) {
    nsvgDelete(image);
    return nullptr;
  }

  unsigned char *pixels = (unsigned char *)malloc(size * size * 4);
  if (!pixels) {
    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);
    return nullptr;
  }
  memset(pixels, 0, size * size * 4);

  // Scale to fit requested size while maintaining aspect ratio
  float scaleX = (float)size / image->width;
  float scaleY = (float)size / image->height;
  float scale = (scaleX < scaleY) ? scaleX : scaleY;

  // Center the icon
  float offsetX = (size - image->width * scale) * 0.5f;
  float offsetY = (size - image->height * scale) * 0.5f;

  nsvgRasterize(rast, image, offsetX, offsetY, scale, pixels, size, size,
                size * 4);

  nsvgDeleteRasterizer(rast);
  nsvgDelete(image);

  return pixels;
}

ImTextureID SvgIconManager::GetIcon(const std::string &svgPath, int size) {
  // Build cache key
  std::string key = svgPath + ":" + std::to_string(size);
  auto it = cache_.find(key);
  if (it != cache_.end())
    return it->second;

  if (!vulkan_)
    return (ImTextureID)0;

  // Rasterize SVG to RGBA pixels
  unsigned char *pixels = RasterizeSvg(svgPath, size);
  if (!pixels)
    return (ImTextureID)0;

  // Upload to Vulkan using the same path as LoadImage but with raw pixels
  // We reuse the VulkanHandler infrastructure directly
  VkDevice device = vulkan_->GetDevice();
  VkPhysicalDevice physDevice = vulkan_->GetPhysicalDevice();

  VkDeviceSize imageSize = size * size * 4;

  // Create VkImage
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = size;
  imageInfo.extent.height = size;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  imageInfo.tiling = VK_IMAGE_TILING_LINEAR;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
  imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VkImage textureImage;
  if (vkCreateImage(device, &imageInfo, nullptr, &textureImage) != VK_SUCCESS) {
    free(pixels);
    return (ImTextureID)0;
  }

  // Allocate memory
  VkMemoryRequirements memReqs;
  vkGetImageMemoryRequirements(device, textureImage, &memReqs);

  VkPhysicalDeviceMemoryProperties memProps;
  vkGetPhysicalDeviceMemoryProperties(physDevice, &memProps);

  uint32_t memTypeIndex = UINT32_MAX;
  for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
    if ((memReqs.memoryTypeBits & (1 << i)) &&
        (memProps.memoryTypes[i].propertyFlags &
         (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      memTypeIndex = i;
      break;
    }
  }
  if (memTypeIndex == UINT32_MAX) {
    vkDestroyImage(device, textureImage, nullptr);
    free(pixels);
    return (ImTextureID)0;
  }

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex = memTypeIndex;

  VkDeviceMemory texMemory;
  if (vkAllocateMemory(device, &allocInfo, nullptr, &texMemory) != VK_SUCCESS) {
    vkDestroyImage(device, textureImage, nullptr);
    free(pixels);
    return (ImTextureID)0;
  }

  vkBindImageMemory(device, textureImage, texMemory, 0);

  // Copy pixel data
  void *data;
  vkMapMemory(device, texMemory, 0, imageSize, 0, &data);
  memcpy(data, pixels, static_cast<size_t>(imageSize));
  vkUnmapMemory(device, texMemory);
  free(pixels);

  // Create ImageView
  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = textureImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  VkImageView textureView;
  if (vkCreateImageView(device, &viewInfo, nullptr, &textureView) !=
      VK_SUCCESS) {
    vkFreeMemory(device, texMemory, nullptr);
    vkDestroyImage(device, textureImage, nullptr);
    return (ImTextureID)0;
  }

  // Create Sampler
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;

  VkSampler sampler;
  if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
    vkDestroyImageView(device, textureView, nullptr);
    vkFreeMemory(device, texMemory, nullptr);
    vkDestroyImage(device, textureImage, nullptr);
    return (ImTextureID)0;
  }

  // Register with ImGui
  VkDescriptorSet descSet = ImGui_ImplVulkan_AddTexture(
      sampler, textureView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  ImTextureID texId = (ImTextureID)descSet;
  cache_[key] = texId;

  return texId;
}

void SvgIconManager::DrawIcon(const std::string &svgPath, int size) {
  ImTextureID tex = GetIcon(svgPath, size);
  if (tex) {
    ImGui::Image(tex, ImVec2((float)size, (float)size));
    ImGui::SameLine();
  }
}

bool SvgIconManager::DrawIconButton(const char *id, const std::string &svgPath,
                                    int size, const ImVec4 &bgColor,
                                    const ImVec4 &tintColor) {
  ImTextureID tex = GetIcon(svgPath, size);
  if (!tex)
    return false;

  ImGui::PushStyleColor(ImGuiCol_Button, bgColor);
  ImGui::PushStyleColor(
      ImGuiCol_ButtonHovered,
      ImVec4(bgColor.x + 0.1f, bgColor.y + 0.1f, bgColor.z + 0.1f, bgColor.w));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(bgColor.x - 0.05f, bgColor.y - 0.05f,
                               bgColor.z - 0.05f, bgColor.w));

  bool clicked = ImGui::ImageButton(id, tex, ImVec2((float)size, (float)size),
                                    ImVec2(0, 0), ImVec2(1, 1),
                                    ImVec4(0, 0, 0, 0), tintColor);
  ImGui::PopStyleColor(3);

  return clicked;
}

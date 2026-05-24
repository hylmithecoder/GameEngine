#include "../../../include/core_engine/TextureManager.hpp"
#include "../../../include/core_engine/Debugger.hpp"
#include "../../../include/core_engine/UserDataDir.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <ktx.h>
#include <stb/stb_image.h>
#include <stdexcept>
#include <vector>

using namespace Debug;

TextureManager::~TextureManager() {
  ClearTextures();
  if (device != VK_NULL_HANDLE && mipSampler != VK_NULL_HANDLE) {
    vkDestroySampler(device, mipSampler, nullptr);
    mipSampler = VK_NULL_HANDLE;
  }
}

void TextureManager::SetCacheDir(const std::string &dir) { cacheDir = dir; }

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

  auto it = textureCache.find(path);
  if (it != textureCache.end())
    return it->second.descriptorSet;

  std::string lower = path;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  auto endsWith = [&](const char *suf) {
    size_t n = std::strlen(suf);
    return lower.size() >= n && lower.compare(lower.size() - n, n, suf) == 0;
  };
  const bool isKtx = endsWith(".ktx") || endsWith(".ktx2");

  // Strategy: .ktx files load directly; other images go through the
  // mipmapped KTX cache when enabled, falling back to a plain stb upload if
  // any step of the KTX path fails — so texturing can never regress.
  try {
    TextureResource res{};
    if (isKtx) {
      res = LoadTextureFromKTX(path);
    } else if (useKtxCache) {
      std::string ktxPath = CacheKtxPathFor(path);
      if (ktxPath.empty())
        throw std::runtime_error("no cache path available");
      std::error_code ec;
      if (!std::filesystem::exists(ktxPath, ec)) {
        if (!BuildKtxCache(path, ktxPath))
          throw std::runtime_error("KTX cache build failed");
      }
      res = LoadTextureFromKTX(ktxPath);
    } else {
      res = LoadTextureVulkan(path);
    }
    textureCache[path] = res;
    return res.descriptorSet;
  } catch (const std::exception &e) {
    if (!isKtx) {
      ::Log("KTX path failed for " + path + " (" + e.what() +
                "); falling back to stb",
            Debug::LogLevel::WARNING);
      try {
        TextureResource res = LoadTextureVulkan(path);
        textureCache[path] = res;
        return res.descriptorSet;
      } catch (const std::exception &e2) {
        ::Log("Failed to load texture: " + path + " - " + e2.what(),
              Debug::LogLevel::CRASH);
        return VK_NULL_HANDLE;
      }
    }
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

  // 3. Staging→image upload via a one-shot graphics-queue command
  // buffer. Two barriers: UNDEFINED→TRANSFER_DST around the copy, then
  // TRANSFER_DST→SHADER_READ_ONLY before sampling. Synchronous (queue
  // wait idle) since texture loads are cold-path / load-time.
  {
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = commandPool;
    cmdAllocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageMemoryBarrier toDst{};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = res.image;
    toDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toDst.subresourceRange.baseMipLevel = 0;
    toDst.subresourceRange.levelCount = 1;
    toDst.subresourceRange.baseArrayLayer = 0;
    toDst.subresourceRange.layerCount = 1;
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toDst);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<uint32_t>(texWidth),
                          static_cast<uint32_t>(texHeight), 1};
    vkCmdCopyBufferToImage(cmd, stagingBuffer, res.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier toShader = toDst;
    toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &toShader);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
  }

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

// ---------------------------------------------------------------------------
// KTX2 cache path: image -> mipmapped .ktx2 (cached) -> Vulkan image.
// ---------------------------------------------------------------------------

void TextureManager::EnsureMipSampler() {
  if (mipSampler != VK_NULL_HANDLE || device == VK_NULL_HANDLE)
    return;
  VkSamplerCreateInfo s{};
  s.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  s.magFilter = VK_FILTER_LINEAR;
  s.minFilter = VK_FILTER_LINEAR;
  s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  s.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  s.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  s.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  s.minLod = 0.0f;
  s.maxLod = VK_LOD_CLAMP_NONE; // sample the whole mip chain
  s.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
  if (vkCreateSampler(device, &s, nullptr, &mipSampler) != VK_SUCCESS)
    throw std::runtime_error("failed to create mip sampler");
}

std::string TextureManager::CacheKtxPathFor(const std::string &srcPath) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path abs = fs::absolute(srcPath, ec);
  std::string absStr = ec ? srcPath : abs.string();

  uint64_t mtime = 0;
  auto t = fs::last_write_time(srcPath, ec);
  if (ec)
    return ""; // source missing → let caller fall back to stb
  mtime = (uint64_t)t.time_since_epoch().count();

  std::string dir = cacheDir;
  if (dir.empty())
    dir = (fs::path(ilmeee::UserDataRoot()) / "texcache").string();
  fs::create_directories(dir, ec);

  size_t h = std::hash<std::string>{}(absStr);
  char name[80];
  std::snprintf(name, sizeof(name), "%016zx_%016llx.ktx2", h,
                (unsigned long long)mtime);
  return (fs::path(dir) / name).string();
}

bool TextureManager::BuildKtxCache(const std::string &srcPath,
                                   const std::string &outKtx) {
  int w = 0, h = 0, ch = 0;
  stbi_uc *pixels = stbi_load(srcPath.c_str(), &w, &h, &ch, STBI_rgb_alpha);
  if (!pixels || w <= 0 || h <= 0)
    return false;
  std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> guard(pixels,
                                                             stbi_image_free);

  uint32_t levels = 1;
  for (int m = (w > h ? w : h); m > 1; m >>= 1)
    ++levels;

  ktxTextureCreateInfo ci{};
  ci.vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
  ci.baseWidth = (uint32_t)w;
  ci.baseHeight = (uint32_t)h;
  ci.baseDepth = 1;
  ci.numDimensions = 2;
  ci.numLevels = levels;
  ci.numLayers = 1;
  ci.numFaces = 1;
  ci.isArray = KTX_FALSE;
  ci.generateMipmaps = KTX_FALSE; // we supply the mip data ourselves

  ktxTexture2 *kt = nullptr;
  if (ktxTexture2_Create(&ci, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &kt) !=
      KTX_SUCCESS)
    return false;
  ktxTexture *kbase = reinterpret_cast<ktxTexture *>(kt);

  std::vector<unsigned char> cur(pixels, pixels + (size_t)w * h * 4);
  if (ktxTexture_SetImageFromMemory(kbase, 0, 0, 0, cur.data(), cur.size()) !=
      KTX_SUCCESS) {
    ktxTexture_Destroy(kbase);
    return false;
  }

  // Box-filter downsample to fill each smaller mip level.
  int pw = w, ph = h;
  for (uint32_t lvl = 1; lvl < levels; ++lvl) {
    int nw = pw > 1 ? pw / 2 : 1;
    int nh = ph > 1 ? ph / 2 : 1;
    std::vector<unsigned char> next((size_t)nw * nh * 4);
    for (int y = 0; y < nh; ++y) {
      for (int x = 0; x < nw; ++x) {
        int x0 = x * 2, y0 = y * 2;
        int x1 = (x0 + 1 < pw) ? x0 + 1 : x0;
        int y1 = (y0 + 1 < ph) ? y0 + 1 : y0;
        for (int c = 0; c < 4; ++c) {
          int sum = cur[((size_t)y0 * pw + x0) * 4 + c] +
                    cur[((size_t)y0 * pw + x1) * 4 + c] +
                    cur[((size_t)y1 * pw + x0) * 4 + c] +
                    cur[((size_t)y1 * pw + x1) * 4 + c];
          next[((size_t)y * nw + x) * 4 + c] = (unsigned char)(sum / 4);
        }
      }
    }
    if (ktxTexture_SetImageFromMemory(kbase, lvl, 0, 0, next.data(),
                                      next.size()) != KTX_SUCCESS) {
      ktxTexture_Destroy(kbase);
      return false;
    }
    cur.swap(next);
    pw = nw;
    ph = nh;
  }

  KTX_error_code wr = ktxTexture_WriteToNamedFile(kbase, outKtx.c_str());
  ktxTexture_Destroy(kbase);
  if (wr != KTX_SUCCESS)
    return false;
  ::Log("KTX cache built (" + std::to_string(levels) + " mips): " + outKtx,
        Debug::LogLevel::SUCCESS);
  return true;
}

TextureManager::TextureResource
TextureManager::LoadTextureFromKTX(const std::string &ktxPath) {
  EnsureMipSampler();

  ktxTexture *kt = nullptr;
  KTX_error_code r = ktxTexture_CreateFromNamedFile(
      ktxPath.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &kt);
  if (r != KTX_SUCCESS || !kt)
    throw std::runtime_error("ktxTexture_CreateFromNamedFile failed");

  const uint32_t width = kt->baseWidth;
  const uint32_t height = kt->baseHeight;
  const uint32_t mipLevels = kt->numLevels;
  ktx_uint8_t *ktxData = ktxTexture_GetData(kt);
  ktx_size_t ktxSize = ktxTexture_GetDataSize(kt);
  const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

  // Staging buffer holds all mip levels packed exactly as KTX laid them out.
  VkBuffer staging = VK_NULL_HANDLE;
  VkDeviceMemory stagingMem = VK_NULL_HANDLE;
  VkBufferCreateInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bi.size = ktxSize;
  bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  vkCreateBuffer(device, &bi, nullptr, &staging);
  VkMemoryRequirements mr;
  vkGetBufferMemoryRequirements(device, staging, &mr);
  VkMemoryAllocateInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  ai.allocationSize = mr.size;
  ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  vkAllocateMemory(device, &ai, nullptr, &stagingMem);
  vkBindBufferMemory(device, staging, stagingMem, 0);
  void *map = nullptr;
  vkMapMemory(device, stagingMem, 0, ktxSize, 0, &map);
  std::memcpy(map, ktxData, ktxSize);
  vkUnmapMemory(device, stagingMem);

  std::vector<VkBufferImageCopy> regions;
  for (uint32_t i = 0; i < mipLevels; ++i) {
    ktx_size_t off = 0;
    if (ktxTexture_GetImageOffset(kt, i, 0, 0, &off) != KTX_SUCCESS)
      continue;
    VkBufferImageCopy region{};
    region.bufferOffset = off;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = i;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {std::max(1u, width >> i), std::max(1u, height >> i),
                          1};
    regions.push_back(region);
  }
  ktxTexture_Destroy(kt); // data already copied into the staging buffer

  TextureResource res;
  VkImageCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.extent = {width, height, 1};
  ici.mipLevels = mipLevels;
  ici.arrayLayers = 1;
  ici.format = format;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  vkCreateImage(device, &ici, nullptr, &res.image);
  vkGetImageMemoryRequirements(device, res.image, &mr);
  ai.allocationSize = mr.size;
  ai.memoryTypeIndex =
      findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  vkAllocateMemory(device, &ai, nullptr, &res.memory);
  vkBindImageMemory(device, res.image, res.memory, 0);

  VkCommandBufferAllocateInfo cba{};
  cba.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cba.commandPool = commandPool;
  cba.commandBufferCount = 1;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  vkAllocateCommandBuffers(device, &cba, &cmd);
  VkCommandBufferBeginInfo cbi{};
  cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &cbi);

  VkImageSubresourceRange range{};
  range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  range.baseMipLevel = 0;
  range.levelCount = mipLevels; // barriers cover every mip level
  range.baseArrayLayer = 0;
  range.layerCount = 1;

  VkImageMemoryBarrier toDst{};
  toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toDst.image = res.image;
  toDst.subresourceRange = range;
  toDst.srcAccessMask = 0;
  toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                       1, &toDst);

  vkCmdCopyBufferToImage(cmd, staging, res.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         (uint32_t)regions.size(), regions.data());

  VkImageMemoryBarrier toShader = toDst;
  toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &toShader);
  vkEndCommandBuffer(cmd);

  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;
  vkQueueSubmit(graphicsQueue, 1, &submit, VK_NULL_HANDLE);
  vkQueueWaitIdle(graphicsQueue);
  vkFreeCommandBuffers(device, commandPool, 1, &cmd);

  VkImageViewCreateInfo vci{};
  vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  vci.image = res.image;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = format;
  vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  vci.subresourceRange.baseMipLevel = 0;
  vci.subresourceRange.levelCount = mipLevels;
  vci.subresourceRange.baseArrayLayer = 0;
  vci.subresourceRange.layerCount = 1;
  vkCreateImageView(device, &vci, nullptr, &res.view);

  res.descriptorSet = ImGui_ImplVulkan_AddTexture(
      mipSampler, res.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  vkDestroyBuffer(device, staging, nullptr);
  vkFreeMemory(device, stagingMem, nullptr);
  return res;
}
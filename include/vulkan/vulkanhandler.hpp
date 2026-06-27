#pragma once
#include "../core_engine/Debugger.hpp"
#ifdef __linux__
#include <xcb/xcb.h>
#endif
#include <vulkan/vulkan.h>
#ifdef __linux__
#include <vulkan/vulkan_xcb.h>
#endif
#include "../audio/FFmpegWrapper.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_vulkan.h>
#include <VkInitializer.hpp>
#include <fstream>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <iostream>
#include <stdlib.h>
#include <string>
#include <vector>
// #include <VkDevice.hpp>
#include <VkTools.hpp>
#include <array>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

using namespace std;
using namespace ImGui;
using namespace Debug;

constexpr auto MAX_CONCURRENT_FRAMES = 2;

class VulkanHandler {
public:
  VkDescriptorSet LoadImage(const char *filename);

  // Public accessors for Vulkan device handles (used by SvgIconManager)
  VkDevice GetDevice() const { return currentDevice; }
  VkPhysicalDevice GetPhysicalDevice() const { return currentPhysicalDevice; }

  string currentFile;

  array<VkFence, MAX_CONCURRENT_FRAMES> waitFences;
  vector<VkSemaphore> presentCompleteSemaphores;
  vector<VkSemaphore> renderCompleteSemaphores;
  vector<VkImage> images;

  void
  setCurrentDeviceAndPhysic(VkDevice device, VkPhysicalDevice physicalDevice,
                            VkQueue graphicsQueue, uint32_t currentQueueFamily,
                            VkCommandPool commandPool,
                            VkPhysicalDeviceMemoryProperties memoryProperties);
  // FFmpeg
  bool OpenFileVideo(const char *filename);
  bool openAudio();
  void cleanupVideoTexture();
  VkDescriptorSet getVideoDescriptorSet() const { return videoDescriptorSet; }
  bool createVideoTexture();
  void updateVideoTexture();
  bool updateVideoFrame();
  bool isPlaying = false;
  void updateAudio();
  void updateBothVideoAndAudio();
  void updateBothVideoAndAudio24fps();

  // Seek to a specific absolute position in seconds. Clamped to
  // [0, duration]. Flushes decoder + audio queue so playback resumes
  // at the new position. Returns false if no video is loaded or the
  // demuxer rejected the seek.
  bool SeekTo(double seconds);
  void setAndUpdateWidthAndHeight(int width, int height) {
    this->width = width;
    this->height = height;
  };

  bool isOnlyRenderImage = false, isOnlyAudio = false;
  uint32_t width = 0, height = 0;
  AVFormatContext *formatContext = nullptr;
  int videoStream = -1;
  // All three were previously uninitialized — duration read as garbage
  // crashed ImGui's slider assert (NaN/inf out of FLT_MAX/2 range).
  double currentTime = 0.0;
  double duration = 0.0;
  double fps = 0.0;
  AVCodecContext *audioCodecContext = nullptr;

  VkPhysicalDeviceMemoryProperties deviceMemoryProperties{};
  uint32_t getMemoryTypeIndex(uint32_t typeBits,
                              VkMemoryPropertyFlags properties) {
    // Iterate over all memory types available for the device used in this
    // example
    for (uint32_t i = 0; i < deviceMemoryProperties.memoryTypeCount; i++) {
      if ((typeBits & 1) == 1) {
        if ((deviceMemoryProperties.memoryTypes[i].propertyFlags &
             properties) == properties) {
          return i;
        }
      }
      typeBits >>= 1;
    }

    throw "Could not find a suitable memory type!";
  }

  string folderShadersVulkan() { return "assets/shaders/vulkan/"; }

  VkClearColorValue defaultClearColor = {{0.025f, 0.025f, 0.025f, 1.0f}};
  VkClearColorValue blueClearColor = {{0.0f, 0.0f, 0.2f, 1.0f}};
  VkPipelineShaderStageCreateInfo loadShader(const char *fileName,
                                             VkShaderStageFlagBits stage);
  uint32_t currentFrame = 0;
  uint32_t currentImageIndex = 0;
  vector<VkFramebuffer> currentFrameBuffers;
  VkRenderPass currentRenderPass;

protected:
  VkDevice currentDevice;
  VkPhysicalDevice currentPhysicalDevice;
  VkQueue currentGraphicsQueue;
  VkCommandPool currentOffscreenCommandPool;
  uint32_t currentQueueFamily;
  VkDescriptorPool currentDescriptorPool;
  vector<VkShaderModule> shaderModules;
  VkPipeline currentPipeline;
  VkPhysicalDeviceMemoryProperties currentMemoryProperties{};

private:
  // Video texture variables for Vulkan. Initialized to VK_NULL_HANDLE
  // explicitly — getVideoDescriptorSet() is called from the UI every
  // frame and previously returned garbage when no video was loaded,
  // causing the Mesa anv driver to crash at descriptor bind time.
  VkImage videoImage = VK_NULL_HANDLE;
  VkDeviceMemory videoImageMemory = VK_NULL_HANDLE;
  VkImageView videoImageView = VK_NULL_HANDLE;
  VkSampler videoSampler = VK_NULL_HANDLE;
  VkDescriptorSet videoDescriptorSet = VK_NULL_HANDLE;

  // Buffer for staging texture data
  VkBuffer stagingBuffer = VK_NULL_HANDLE;
  VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;

  // FFmpeg variable
  bool isPlayingAudio = false;
  AVCodecContext *codecContext = nullptr;
  SwsContext *swsContext = nullptr;
  AVFrame *frame = nullptr;
  AVFrame *frameRGB = nullptr;
  AVFrame *hw_frame = nullptr;
  AVPacket *packet = nullptr;
  AVFrame *lastGoodFrameRGB;
  SDL_Texture *videoTexture = nullptr;
  SDL_AudioSpec audioSpec;
  // int audioStream = -1;
  SwrContext *swrContext = nullptr;
  SDL_AudioDeviceID audioDeviceID = 0;
  uint8_t *buffer;
  bool hasValidFrame = false;
  int audioStreamIndex = -1;
  SDL_AudioStream *audioStream = nullptr;

  AVChannelLayout audioChannelLayout;

  void cleanUpVideoHandler();

  VkMemoryAllocateInfo
  createMemoryAllocateInfo(VkMemoryRequirements &currentMemRequirements,
                           VkPhysicalDevice &currentPhysicalDevice) {
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = currentMemRequirements.size;

    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(currentPhysicalDevice, &memProperties);
    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
      if ((currentMemRequirements.memoryTypeBits & (1 << i)) &&
          (memProperties.memoryTypes[i].propertyFlags &
           (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
              (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        memTypeIndex = i;
        break;
      }
    }
    cout << "Mem properties: " << memProperties.memoryTypeCount << endl;
    if (memTypeIndex == UINT32_MAX)
      throw runtime_error("Failed to find suitable memory type!");

    allocInfo.memoryTypeIndex = memTypeIndex;

    return allocInfo;
  }
  uint32_t findMemoryType(uint32_t typeFilter,
                          VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(currentPhysicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
      if ((typeFilter & (1 << i)) &&
          (memProperties.memoryTypes[i].propertyFlags & properties) ==
              properties) {
        return i;
      }
    }

    throw std::runtime_error("Failed to find suitable memory type!");
  };
  void endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    // Submit to graphics queue (assuming you have graphicsQueue)
    vkQueueSubmit(currentGraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(currentGraphicsQueue);

    vkFreeCommandBuffers(currentDevice, currentOffscreenCommandPool, 1,
                         &commandBuffer);
  };

  VkCommandBuffer beginSingleTimeCommands() {
    // Validate device and command pool
    if (currentDevice == VK_NULL_HANDLE ||
        currentOffscreenCommandPool == VK_NULL_HANDLE) {
      throw std::runtime_error("Device or command pool not initialized! error "
                               "di class vulkan Handler");
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = currentOffscreenCommandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    VkResult result =
        vkAllocateCommandBuffers(currentDevice, &allocInfo, &commandBuffer);
    if (result != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate command buffer!");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
      throw std::runtime_error("Failed to begin command buffer!");
    }

    // Debug print after successful allocation
    Log("Command Buffer allocated: " + std::to_string((uint64_t)commandBuffer),
        LogLevel::INFO);

    return commandBuffer;
  }

  bool processVideoPacket(AVPacket *pkt);
  void processAudioPacket(AVPacket *pkt);
};

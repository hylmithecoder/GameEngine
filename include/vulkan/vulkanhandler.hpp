#pragma once
#include "../core_engine/Debugger.hpp"
#include <vulkan/vulkan.h>
#ifdef __linux__
#include <xcb/xcb.h>
#include <vulkan/vulkan_xcb.h>
#endif
#include <SDL_audio.h>
#include <SDL_vulkan.h>
#include <SDL.h>
#include <string>
#include <iostream>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <stdlib.h>
#include <imgui_impl_sdl3.h>
#include <fstream>
#include "../audio/FFmpegWrapper.hpp"
#include <vector>
#include <VkInitializer.hpp>
// #include <VkDevice.hpp>
#include <VkTools.hpp>
#include <vector>
#include <array>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

using namespace std;
using namespace ImGui;
using namespace Debug;

constexpr auto MAX_CONCURRENT_FRAMES = 2;

class VulkanHandler {
    public: 
        VkDescriptorSet LoadImage(const char* filename);

        string currentFile;

        array<VkFence, MAX_CONCURRENT_FRAMES> waitFences;
        vector<VkSemaphore> presentCompleteSemaphores;
        vector<VkSemaphore> renderCompleteSemaphores;
        vector<VkImage> images;

        void setCurrentDeviceAndPhysic(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue graphicsQueue, uint32_t currentQueueFamily, VkCommandPool commandPool, VkPhysicalDeviceMemoryProperties memoryProperties);
        // FFmpeg
        bool OpenFileVideo(const char* filename);
        void cleanupVideoTexture();
        VkDescriptorSet getVideoDescriptorSet() const { return videoDescriptorSet; }
        bool createVideoTexture();
        void updateVideoTexture();
        bool updateVideoFrame();
        bool isPlaying = false;
        void updateAudio();
        void updateBothVideoAndAudio();
        void updateBothVideoAndAudio24fps();
        void setAndUpdateWidthAndHeight(int width, int height){
            this->width = width;
            this->height = height;
        };

        bool isOnlyRenderImage, isOnlyAudio;
        uint32_t width, height;
        AVFormatContext* formatContext = nullptr;
        int videoStream = -1;
        double currentTime, duration, fps = 0.0;
        AVCodecContext* audioCodecContext = nullptr;

        VkPhysicalDeviceMemoryProperties deviceMemoryProperties{};
        uint32_t getMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties)
        {
            // Iterate over all memory types available for the device used in this example
            for (uint32_t i = 0; i < deviceMemoryProperties.memoryTypeCount; i++)
            {
                if ((typeBits & 1) == 1)
                {
                    if ((deviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
                    {
                        return i;
                    }
                }
                typeBits >>= 1;
            }

            throw "Could not find a suitable memory type!";
        }

        string folderShadersVulkan(){
            return "assets/shaders/vulkan/";
        }

        VkClearColorValue defaultClearColor = { { 0.025f, 0.025f, 0.025f, 1.0f } };
        VkClearColorValue blueClearColor = { { 0.0f, 0.0f, 0.2f, 1.0f } };
        VkPipelineShaderStageCreateInfo loadShader(const char* fileName, VkShaderStageFlagBits stage);
        uint32_t currentFrame = 0;
        uint32_t currentImageIndex = 0;
        vector<VkFramebuffer> currentFrameBuffers;

    protected :
        VkDevice currentDevice;
        VkPhysicalDevice currentPhysicalDevice;
        VkQueue currentGraphicsQueue;
        VkCommandPool currentOffscreenCommandPool;
        uint32_t currentQueueFamily;
        VkDescriptorPool currentDescriptorPool;
        vector<VkShaderModule> shaderModules;
        VkRenderPass currentRenderPass;
        VkPipeline currentPipeline;
        VkPhysicalDeviceMemoryProperties currentMemoryProperties{};

    private :
        // Video texture variables for Vulkan
        VkImage videoImage;
        VkDeviceMemory videoImageMemory;
        VkImageView videoImageView;
        VkSampler videoSampler;
        VkDescriptorSet videoDescriptorSet;
        
        // Buffer for staging texture data
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;

        // FFmpeg variable
        bool isPlayingAudio = false;
        AVCodecContext* codecContext = nullptr;
        SwsContext* swsContext = nullptr;
        AVFrame* frame = nullptr;
        AVFrame* frameRGB = nullptr;
        AVFrame* hw_frame = nullptr;
        AVPacket* packet = nullptr;
        AVFrame* lastGoodFrameRGB;
        SDL_Texture* videoTexture = nullptr;    
        SDL_AudioSpec audioSpec;
        // int audioStream = -1;
        SwrContext* swrContext = nullptr;
        SDL_AudioDeviceID audioDeviceID = 0;
        uint8_t* buffer; 
        bool hasValidFrame = false;
        int audioStreamIndex = -1;
        SDL_AudioStream* audioStream = nullptr;

        AVChannelLayout audioChannelLayout;

        void cleanUpVideoHandler();
        bool openAudio();

        VkMemoryAllocateInfo createMemoryAllocateInfo(VkMemoryRequirements& currentMemRequirements,
            VkPhysicalDevice& currentPhysicalDevice
        ) {
            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = currentMemRequirements.size;

            VkPhysicalDeviceMemoryProperties memProperties;
            vkGetPhysicalDeviceMemoryProperties(currentPhysicalDevice, &memProperties);
            uint32_t memTypeIndex = UINT32_MAX;
            for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
                if ((currentMemRequirements.memoryTypeBits & (1 << i)) &&
                    (memProperties.memoryTypes[i].propertyFlags &
                    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                {
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
        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties){
            VkPhysicalDeviceMemoryProperties memProperties;
            vkGetPhysicalDeviceMemoryProperties(currentPhysicalDevice, &memProperties);

            for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
                if ((typeFilter & (1 << i)) && 
                    (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                    return i;
                }
            }

            throw std::runtime_error("Failed to find suitable memory type!");
        };
        void endSingleTimeCommands(VkCommandBuffer commandBuffer){
            vkEndCommandBuffer(commandBuffer);

            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            // Submit to graphics queue (assuming you have graphicsQueue)
            vkQueueSubmit(currentGraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
            vkQueueWaitIdle(currentGraphicsQueue);

            vkFreeCommandBuffers(currentDevice, currentOffscreenCommandPool, 1, &commandBuffer);
        };

        VkCommandBuffer beginSingleTimeCommands() {
            // Validate device and command pool
            if (currentDevice == VK_NULL_HANDLE || currentOffscreenCommandPool == VK_NULL_HANDLE) {
                throw std::runtime_error("Device or command pool not initialized! error di class vulkan Handler");
            }

            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandPool = currentOffscreenCommandPool;
            allocInfo.commandBufferCount = 1;

            VkCommandBuffer commandBuffer;
            VkResult result = vkAllocateCommandBuffers(currentDevice, &allocInfo, &commandBuffer);
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
            Log("Command Buffer allocated: " + std::to_string((uint64_t)commandBuffer), LogLevel::INFO);

            return commandBuffer;
        }

        bool processVideoPacket(AVPacket* pkt);
        void processAudioPacket(AVPacket* pkt);
};

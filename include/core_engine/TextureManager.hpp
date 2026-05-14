#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <unordered_map>
#include <imgui_impl_vulkan.h>

class TextureManager {
public:
    TextureManager() = default;
    ~TextureManager();

    void SetVulkanContext(VkDevice device, VkPhysicalDevice physicalDevice, 
                          VkQueue graphicsQueue, VkCommandPool commandPool, 
                          VkDescriptorPool descriptorPool, VkSampler sampler);

    VkDescriptorSet GetTextureDescriptor(const std::string& path);
    void ClearTextures();

private:
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkSampler defaultSampler = VK_NULL_HANDLE;

    struct TextureResource {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };

    std::unordered_map<std::string, TextureResource> textureCache;
    
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    TextureResource LoadTextureVulkan(const std::string& path);
};
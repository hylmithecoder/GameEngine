#pragma once

#include <SDL.h>
#include <vulkan/vulkan.h>

class TestVulkan {
public:
    TestVulkan(SDL_Window* window);
    ~TestVulkan();

    void draw();

private:
    // SDL Window reference
    SDL_Window* sdlWindow = nullptr;

    // Vulkan objects
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

    // Queue family indices
    int graphicsQueueFamily = -1;
    int presentQueueFamily = -1;

    // Initialization functions
    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createCommandPool();
    void createDescriptorPool();
    void setupImGui();

    // Helper functions
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
};
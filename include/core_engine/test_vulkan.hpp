#pragma once

// #define VK_USE_PLATFORM_WIN32_KHR
#include <SDL.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <optional>
#include <string>

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    
    bool isComplete() {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

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

    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;
    VkRenderPass renderPass;
    std::vector<VkFramebuffer> swapchainFramebuffers;

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
    void createSwapChain();
    void createRenderPass();

    // Helper functions
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);

     // Add these new members
    const std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
    
    // Add these new helper functions
    bool checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions();
    bool isDeviceSuitable(VkPhysicalDevice device);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    
    // Debug messenger
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    void setupDebugMessenger();

    int ratePhysicalDevice(VkPhysicalDevice device);
    void printDeviceProperties(VkPhysicalDevice device);
    bool isDiscreteGPU(VkPhysicalDevice device);
};
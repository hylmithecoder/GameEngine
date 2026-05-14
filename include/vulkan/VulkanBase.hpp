#pragma once

#include "../core_engine/Debugger.hpp"
#include "VkInitializer.hpp"
#include "VkTools.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace vkhandler {

/**
 * @brief Core Vulkan handles and context information.
 * Decoupled from the window for future-proofing and multi-window support.
 */
struct VulkanContext {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue graphicsQueue = VK_NULL_HANDLE;
  VkQueue presentQueue = VK_NULL_HANDLE;
  VkCommandPool commandPool = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
  uint32_t graphicsQueueFamily = 0;
  uint32_t presentQueueFamily = 0;
};

/**
 * @brief Base class for a Vulkan-powered application or window.
 * Handles the boilerplate of initialization, swapchain management, and ImGui
 * setup. Android future-proofed by using SDL3 and abstraction of surface
 * creation.
 */
class VulkanBase {
public:
  VulkanBase();
  virtual ~VulkanBase();

  // Core Lifecycle
  bool Init(const std::string &title, int width, int height);
  void Run();
  void Cleanup();

  // Getters
  SDL_Window *GetWindow() const { return window; }
  const VulkanContext &GetContext() const { return ctx; }

protected:
  // Hooks for implementation classes
  virtual void OnInit() {}
  virtual void OnUpdate(float deltaTime) {}
  virtual void OnRender(VkCommandBuffer cmd) {}
  virtual void OnCleanup() {}
  virtual void OnResize(int width, int height) {}

  // Utility methods
  VkCommandBuffer BeginSingleTimeCommands();
  void EndSingleTimeCommands(VkCommandBuffer commandBuffer);
  uint32_t FindMemoryType(uint32_t typeFilter,
                          VkMemoryPropertyFlags properties);

  // Pipeline helper
  struct PipelineConfig {
    std::string vertShader;
    std::string fragShader;
    VkVertexInputBindingDescription bindingDescription;
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace frontFace = VK_FRONT_FACE_CLOCKWISE;
  };

  VkPipeline CreateGraphicsPipeline(const PipelineConfig &config,
                                    VkPipelineLayout layout);

  // Common Vulkan members
  SDL_Window *window = nullptr;
  VulkanContext ctx;

  // Swapchain resources
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkSwapchainKHR swapChain = VK_NULL_HANDLE;
  std::vector<VkImage> swapChainImages;
  std::vector<VkImageView> swapChainImageViews;
  VkFormat swapChainImageFormat;
  VkExtent2D swapChainExtent;

  // Rendering resources
  VkRenderPass renderPass = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> framebuffers;

  // Synchronization
  std::vector<VkSemaphore> imageAvailableSemaphores;
  std::vector<VkSemaphore> renderFinishedSemaphores;
  std::vector<VkFence> inFlightFences;
  uint32_t currentFrame = 0;
  const int MAX_FRAMES_IN_FLIGHT = 2;

  bool isRunning = false;
  int windowWidth, windowHeight;

private:
  // Initialization steps
  bool CreateInstance();
  bool SelectPhysicalDevice();
  bool CreateLogicalDevice();
  bool CreateSwapChain();
  bool CreateImageViews();
  bool CreateRenderPass();
  bool CreateFramebuffers();
  bool CreateCommandPool();
  bool CreateDescriptorPool();
  bool CreateSyncPrimitives();
  bool InitImGui();

  // Swapchain maintenance
  void RecreateSwapChain();
  void CleanupSwapChain();

  // Frame execution
  void DrawFrame();
};

} // namespace vkhandler

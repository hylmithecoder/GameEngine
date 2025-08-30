#pragma once
#include <Debugger.hpp>
#include <vulkan/vulkan.h>
#include <SDL_vulkan.h>
#include <SDL.h>
#include <string>
#include <iostream>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <stdlib.h>
#include <imgui_impl_sdl3.h>
#include <vector>
#include <camera.hpp>
#include <map>
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include <stdexcept>
#include <fstream>
using namespace std;
using namespace Debug;
// using namespace glm;

class Viewport3D {
    public:
        SDL_Window* mainWindow = nullptr;
        // Camera
        Camera* camera = new Camera(glm::vec3(0.0f, 0.0f, 3.0f));
        // Main Component vulkan
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        ImGuiIO currentIo;
        VkAllocationCallbacks*   g_Allocator = nullptr;
        VkInstance               g_Instance = VK_NULL_HANDLE;
        VkPhysicalDevice         g_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice                 g_Device = VK_NULL_HANDLE;
        uint32_t                 g_QueueFamily = (uint32_t)-1;
        VkQueue                  g_Queue = VK_NULL_HANDLE;
        VkPipelineCache          g_PipelineCache = VK_NULL_HANDLE;
        VkDescriptorPool         g_DescriptorPool = VK_NULL_HANDLE;

        ImGui_ImplVulkanH_Window g_MainWindowData;
        uint32_t                 g_MinImageCount = 2;
        bool                     g_SwapChainRebuild = false;
        ImGui_ImplVulkanH_Window* wd = nullptr;
        ImGui_ImplVulkan_InitInfo init_info = {};

        VkPipelineLayout pipelineLayout;
        VkPipeline graphicsPipeline;
        VkRenderPass renderPass;

        void createRenderPass();
        void createGraphicsPipeline(const std::string& vertShaderPath, const std::string& fragShaderPath);
        VkShaderModule createShaderModule(const std::vector<char>& code);
        static std::vector<char> readFile(const std::string& filename);
        void CreateOffscreenResources(int width, int height);
        void CreateOffscreenPipeline();
        void RenderOffscreen(uint32_t width, uint32_t height);
        void DrawImguiViewport();

        // Offscreen render target
        VkImage offscreenImage;
        VkDeviceMemory offscreenImageMemory;
        VkImageView offscreenImageView;
        VkSampler offscreenSampler;
        VkFramebuffer offscreenFramebuffer;
        VkRenderPass offscreenRenderPass;

        // Descriptor untuk ImGui::Image()
        VkDescriptorSet offscreenDescriptorSet;
        VkDescriptorSetLayout descriptorSetLayout;
        VkDescriptorPool imguiDescriptorPool;

        // Command buffer khusus offscreen 
        VkCommandBuffer offscreenCmdBuffer = VK_NULL_HANDLE;// Offscreen command pool + semaphore
        VkCommandPool offscreenCommandPool = VK_NULL_HANDLE;
        VkSemaphore offscreenSignalSemaphore = VK_NULL_HANDLE;

        int graphicsQueueFamily = -1;
        int presentQueueFamily = -1;

        void SetupVulkan(ImVector<const char*> instance_extensions, SDL_Window* currentWindow);
        void initVulkan(ImVector<const char*> instance_extensions, SDL_Window* window){
            SetupVulkan(instance_extensions, window);
            create_vk_surface();
            SetupVulkanWindow(&g_MainWindowData, surface, 1280, 720);
            SetupImgui();
            pickPhysicalDevice();
        };

        void showCurrentCameraPosition(){
            ImGui::Text("Camera Position: (%.2f, %.2f, %.2f)", camera->Position.x, camera->Position.y, camera->Position.z);
        }

        string convertUintVariabletoString(auto var){
            return to_string(reinterpret_cast<uintptr_t>(var));
        }
        
        bool IsExtensionAvailable(const ImVector<VkExtensionProperties>& properties, const char* extension);
        void create_vk_surface();
        void SetupVulkanWindow(ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int width, int height);
        void SetupImgui();
        void CleanupVulkan();
        void FrameRender(ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data);
        void FramePresent(ImGui_ImplVulkanH_Window* wd);
        void CreateOffscreenCommandResources();
        void renderViewport();
        VkDescriptorSet LoadTextureForImGui(const char* filename);
        VkDescriptorSet LoadTextureSimple(const char* filename);
        void DrawImage();
        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
        void CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
        void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
        void EndSingleTimeCommands(VkCommandBuffer commandBuffer);
        VkCommandBuffer BeginSingleTimeCommands();
        static void check_vk_result(VkResult err)
        {
            if (err == VK_SUCCESS)
                // cout << "[vulkan] Success: VkResult = " << err << endl;
                return;
            Logger::Log("VkResult = " + to_string(err), Debug::LogLevel::CRASH);
            if (err < 0)
                abort();
        }
        void Update();

        // Info Vulkan
        int ratePhysicalDevice(VkPhysicalDevice device);
        void pickPhysicalDevice();

    private:
        int viewportWidth, viewportHeight;
};
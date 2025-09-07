// #pragma once
// #include <Debugger.hpp>
// #include <vulkan/vulkan.h>
// #include <SDL_vulkan.h>
// #include <SDL.h>
// #include <string>
// #include <iostream>
// #include <imgui.h>
// #include <imgui_impl_vulkan.h>
// #include <stdlib.h>
// #include <imgui_impl_sdl3.h>
#include <chrono>
#include <vector>
#include <camera.hpp>
#include <map>
#include <vulkanhandler.hpp>
// #define STB_IMAGE_IMPLEMENTATION
// #include <stb/stb_image.h>
// #include <stdexcept>
#include <fstream>
// using namespace std;
// using namespace Debug;
// using namespace glm;

class Viewport3D {
    public:
        // Di class Viewport3D
        VulkanHandler imageHandler;
        struct UniformBufferObject {
            glm::mat4 model;
            glm::mat4 view;
            glm::mat4 proj;
        };

        struct Vertex {
            glm::vec3 pos;     // posisi 3D
            glm::vec3 color;   // warna
            glm::vec3 normal;  // normal vector

            static VkVertexInputBindingDescription getBindingDescription() {
                VkVertexInputBindingDescription bindingDescription{};
                bindingDescription.binding = 0;
                bindingDescription.stride = sizeof(Vertex);
                bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
                return bindingDescription;
            }

            static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() {
                std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
                
                // Position
                attributeDescriptions[0].binding = 0;
                attributeDescriptions[0].location = 0;
                attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
                attributeDescriptions[0].offset = offsetof(Vertex, pos);
                
                // Color
                attributeDescriptions[1].binding = 0;
                attributeDescriptions[1].location = 1;
                attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
                attributeDescriptions[1].offset = offsetof(Vertex, color);
                
                // Normal
                attributeDescriptions[2].binding = 0;
                attributeDescriptions[2].location = 2;
                attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
                attributeDescriptions[2].offset = offsetof(Vertex, normal);

                return attributeDescriptions;
            }
        };

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
        bool isRunning = false;
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
        void DrawViewport3D();

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
            helperInitImage();
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

        void Events();
        
        bool IsExtensionAvailable(const ImVector<VkExtensionProperties>& properties, const char* extension);
        void create_vk_surface();
        void SetupVulkanWindow(ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int width, int height);
        void SetupImgui();
        void CleanupVulkan();
        void FrameRender(ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data);
        void FramePresent(ImGui_ImplVulkanH_Window* wd);
        void CreateOffscreenCommandResources();
        void renderViewport();
        VkDescriptorSet LoadImage(const char* filename);
        void DrawImage();
        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        VkBuffer CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
        // void CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
        // void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
        VkCommandBuffer BeginSingleTimeCommands() {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandPool = offscreenCommandPool;
            allocInfo.commandBufferCount = 1;

            VkCommandBuffer commandBuffer;
            vkAllocateCommandBuffers(g_Device, &allocInfo, &commandBuffer);

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            vkBeginCommandBuffer(commandBuffer, &beginInfo);

            return commandBuffer;
        }

        void EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
            vkEndCommandBuffer(commandBuffer);

            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            vkQueueSubmit(g_Queue, 1, &submitInfo, VK_NULL_HANDLE);
            vkQueueWaitIdle(g_Queue);

            vkFreeCommandBuffers(g_Device, offscreenCommandPool, 1, &commandBuffer);
        }

        static void check_vk_result(VkResult err) {
            if (err == VK_SUCCESS) return;

            Logger::Log("[vulkan] Error: VkResult = " + std::to_string(err), LogLevel::CRASH);
            
            switch (err) {
                case VK_ERROR_DEVICE_LOST:
                    Logger::Log("Device lost - attempting recovery...", LogLevel::WARNING);
                    // Don't throw here, let the caller handle recovery
                    break;
                case VK_ERROR_OUT_OF_DATE_KHR:
                case VK_SUBOPTIMAL_KHR:
                    // These are recoverable - just signal for rebuild
                    Logger::Log("Swap chain out of date or suboptimal - rebuilding...", LogLevel::WARNING);
                    // g_SwapChainRebuild = true;
                    break;
                default:
                    if (err < 0) {
                        throw std::runtime_error("Unhandled Vulkan error");
                    }
                    break;
            }
        }

        void Update();

        // Info Vulkan
        int ratePhysicalDevice(VkPhysicalDevice device);
        void pickPhysicalDevice();

        // Add these to the class public members:
        // Uniform buffer objects
        VkBuffer uniformBuffer;
        VkDeviceMemory uniformBufferMemory;
        VkDescriptorSetLayout uniformDescriptorSetLayout;
        VkDescriptorSet uniformDescriptorSet;

        // Add these method declarations
        void createUniformBuffers();
        void createUniformDescriptorSetLayout();
        void createUniformDescriptorSets();
        void updateUniformBuffer();
        void createVertexBuffer();

    private:
        int viewportWidth, viewportHeight;
        void helperInitImage(){
            imageHandler.setCurrentDeviceAndPhysic(g_Device, g_PhysicalDevice);
        }

        VkBuffer vertexBuffer;
        VkDeviceMemory vertexBufferMemory;
};
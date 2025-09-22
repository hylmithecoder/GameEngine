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
        VulkanHandler imageHandler;
        struct UniformBufferObject {
            glm::mat4 model;
            glm::mat4 view;
            glm::mat4 proj;
        };

        struct Vertex {
            glm::vec3 pos;     // Position
            glm::vec3 color;   // warna
            glm::vec3 normal;  // vector

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
            // helperInitImage();
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
                    break;
                case VK_ERROR_OUT_OF_DATE_KHR:
                case VK_SUBOPTIMAL_KHR:
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

        void Update(ImGuiIO& io);

        // Info Vulkan
        int ratePhysicalDevice(VkPhysicalDevice device);
        void pickPhysicalDevice();

        // Uniform buffer objects
        VkBuffer uniformBuffer;
        VkDeviceMemory uniformBufferMemory;
        vector<VkDeviceMemory> uniformBuffersMemory;
        VkDescriptorSetLayout uniformDescriptorSetLayout;
        VkDescriptorSet uniformDescriptorSet;
        vector<VkDescriptorSet> uniformDescriptorSets;

        // Add these method declarations
        void createUniformBuffers();
        void createUniformDescriptorSetLayout();
        void createUniformDescriptorSets();
        void updateUniformBuffer();
        void createVertexBuffer();
        void renderVideoFrame();
        void helperInitImage(){
            imageHandler.setCurrentDeviceAndPhysic(g_Device, g_PhysicalDevice, g_Queue, g_QueueFamily, offscreenCommandPool);
        }

    private:
        int viewportWidth, viewportHeight;

        VkBuffer vertexBuffer;
        VkDeviceMemory vertexBufferMemory;
        uint32_t currentFrame = 0;
        uint32_t vertexCount = 0;

        ImGuiIO getImGuiIO(ImGuiIO& io){
            // cout << io << endl;
            return io;
        }

        float fontSize;

        void videoPlayerUI();
        VkRenderPassBeginInfo createRenderPassInfo(VkRenderPass& currentRenderPass, 
            VkFramebuffer& currentFrameBuffer, 
            uint32_t& width, 
            uint32_t& height, 
            VkClearValue& clearValue)
        {
            VkRenderPassBeginInfo rpInfo{};
            rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpInfo.renderPass = currentRenderPass;
            rpInfo.framebuffer = currentFrameBuffer;
            rpInfo.renderArea.offset = {0, 0};
            rpInfo.renderArea.extent = { width, height };
            rpInfo.clearValueCount = 1;
            rpInfo.pClearValues = &clearValue;

            return rpInfo;
        };

        VkFramebufferCreateInfo createFrameBuffer(VkRenderPass& currentRenderPass, 
            VkImageView* currentImageView,
            int& width, 
            int& height
        )
        {
            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = offscreenRenderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = currentImageView;
            fbInfo.width = viewportWidth;
            fbInfo.height = viewportHeight;
            fbInfo.layers = 1;

            return fbInfo;
        }

        VkAttachmentDescription createColorAttachment(){
            VkAttachmentDescription colorAttachment{};
            colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
            colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            return colorAttachment;
        }

        VkGraphicsPipelineCreateInfo createPipeLineInfo(VkPipelineShaderStageCreateInfo* currentShaderStage,
            VkPipelineVertexInputStateCreateInfo& currentVertexInput,
            VkPipelineInputAssemblyStateCreateInfo& currentInputAssembly,
            VkPipelineViewportStateCreateInfo& currentViewportState,
            VkPipelineRasterizationStateCreateInfo& currentRasterizerInfo,
            VkPipelineMultisampleStateCreateInfo& multisampling,
            VkPipelineColorBlendStateCreateInfo& colorBlending
        ){
            VkGraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.stageCount = 2;
            pipelineInfo.pStages = currentShaderStage;
            pipelineInfo.pVertexInputState = &currentVertexInput;
            pipelineInfo.pInputAssemblyState = &currentInputAssembly;
            pipelineInfo.pViewportState = &currentViewportState;
            pipelineInfo.pRasterizationState = &currentRasterizerInfo;
            pipelineInfo.pMultisampleState = &multisampling;
            pipelineInfo.pColorBlendState = &colorBlending;
            pipelineInfo.layout = pipelineLayout;
            pipelineInfo.renderPass = renderPass;
            pipelineInfo.subpass = 0;

            return pipelineInfo;
        }
};
#pragma once
#include <chrono>
#include <vector>
#include <camera.hpp>
#include <map>
#include <vulkanhandler.hpp>
#include <fstream>
#include <texture.hpp>
using namespace Debug;
using namespace vkhandler;

// constexpr auto MAX_CONCURRENT_FRAMES = 2;

class Viewport3D {
    public:
        VulkanHandler imageHandler;
        TextureBase textureHandler;

        struct ShaderData {
            glm::mat4 projectionMatrix;
            glm::mat4 modelMatrix;
            glm::mat4 viewMatrix;
	    };

        struct UniformBufferObject {
            VkDeviceMemory memory{ VK_NULL_HANDLE };
            VkBuffer buffer{ VK_NULL_HANDLE };
            // The descriptor set stores the resources bound to the binding points in a shader
            // It connects the binding points of the different shaders with the buffers and images used for those bindings
            VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
            // We keep a pointer to the mapped buffer, so we can easily update it's contents via a memcpy
            uint8_t* mapped{ nullptr };
        };
	    array<UniformBufferObject, MAX_CONCURRENT_FRAMES> uniformBuffers;

        
        struct Vertex {
            float position[3];
            float color[3];
        };

        struct {
            VkDeviceMemory memory{ VK_NULL_HANDLE }; // Handle to the device memory for this buffer
            VkBuffer buffer{ VK_NULL_HANDLE };		 // Handle to the Vulkan buffer object that the memory is bound to
        } vertices;

        // Index buffer
        struct {
            VkDeviceMemory memory{ VK_NULL_HANDLE };
            VkBuffer buffer{ VK_NULL_HANDLE };
            uint32_t count{ 0 };
        } indices;

        SDL_Window* mainWindow = nullptr;
        // Camera
        Camera camera;
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
        VkSwapchainKHR swapChain {VK_NULL_HANDLE};

        // Viewport
        void createSwapChain(uint32_t& width, uint32_t& height, bool vsync, bool fullscreen);
        void createRenderPass();
        void createGraphicsPipeline(const string& vertShaderPath, const string& fragShaderPath);
        VkShaderModule createShaderModule(const vector<char>& code);
        static vector<char> readFile(const string& filename);
        void CreateOffscreenResources(int width, int height);
        void CreateOffscreenPipeline();
        void RenderOffscreen(uint32_t width, uint32_t height);
        void CreateFrameBuffer(uint32_t width, uint32_t height);
        void CreateDepthStencil(uint32_t width, uint32_t height);
        void DrawViewport3D();
        void HandleCameraMovement();
        void recreateOffscreenResources(uint32_t width, uint32_t height);
        VkFormat depthFormat {VK_FORMAT_UNDEFINED};
        vector<VkImage> images;
        vector<VkImageView> imageViews;
        uint32_t viewportWidth, viewportHeight;
        glm::vec4 position;
        glm::vec3 rotation = glm::vec3(0.0f, 0.0f, 0.0f);

        vector<glm::mat4> glm4Deserealize(const glm::mat4& targetMat4);
        vector<VkSemaphore> presentCompleteSemaphores;
        vector<VkSemaphore> renderCompleteSemaphores;
        vector<VkFramebuffer> framebuffers;
        // VulkanSwapChain swapChain;

        // Offscreen render target
        VkImage offscreenImage;
        VkDeviceMemory offscreenImageMemory;
        VkImageView offscreenImageView;
        VkSampler offscreenSampler;
        VkFramebuffer offscreenFramebuffer;
        VkRenderPass offscreenRenderPass;
        VkFormat colorFormat{};
        VkColorSpaceKHR colorSpace{};
        uint32_t imageCount{ 0 };

        struct {
            VkImageView imageView;
            VkImage image;
            VkDeviceMemory memory;
        } depthStencil {};

        VkDescriptorSet offscreenDescriptorSet;
        VkDescriptorSetLayout descriptorSetLayout;

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
            ImGui::Text("Camera Position: (%.2f, %.2f, %.2f)", camera.matrices.view[3][0], camera.matrices.view[3][1], camera.matrices.view[3][2]);
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
        uint32_t FindMemoryType(VkMemoryRequirements memRequirements, VkPhysicalDeviceMemoryProperties memProperties, VkMemoryPropertyFlags properties);
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

        VkCommandBufferAllocateInfo commandBufferAllocateInfo(
			VkCommandPool commandPool, 
			VkCommandBufferLevel level, 
			uint32_t bufferCount)
		{
			VkCommandBufferAllocateInfo commandBufferAllocateInfo {};
			commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			commandBufferAllocateInfo.commandPool = commandPool;
			commandBufferAllocateInfo.level = level;
			commandBufferAllocateInfo.commandBufferCount = bufferCount;
			return commandBufferAllocateInfo;
		}

        void EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
            vkEndCommandBuffer(commandBuffer);

            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            VkFenceCreateInfo fenceCI{};
            fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceCI.flags = 0;
            VkFence fence;
            check_vk_result(vkCreateFence(g_Device, &fenceCI, nullptr, &fence));

            vkQueueSubmit(g_Queue, 1, &submitInfo, VK_NULL_HANDLE);
		    // check_vk_result(vkWaitForFences(g_Device, 1, &fence, VK_TRUE, 100000000000));
		    // vkDestroyFence(g_Device, fence, nullptr);
            
            vkQueueWaitIdle(g_Queue);

            vkFreeCommandBuffers(g_Device, offscreenCommandPool, 1, &commandBuffer);
        }

        static void check_vk_result(VkResult err) {
            // Logger::Log("[vulkan] Info: VkResult = " + to_string(err), LogLevel::INFO);

            if (err == VK_SUCCESS){ 
                return;
            }

            // Logger::Log("[vulkan] Error: VkResult = " + to_string(err), LogLevel::CRASH);
            
            switch (err) {
                case VK_ERROR_DEVICE_LOST:
                    Logger::Log("Device lost - attempting recovery...", LogLevel::WARNING);
                    break;
                case VK_ERROR_OUT_OF_DATE_KHR:
                case VK_SUBOPTIMAL_KHR:
                    // Logger::Log("Swap chain out of date or suboptimal - rebuilding...", LogLevel::WARNING);
                    // g_SwapChainRebuild = true;
                    break;
                default:
                    if (err < 0) {
                        throw runtime_error("Unhandled Vulkan error");
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
    	VkCommandPool commandPool{ VK_NULL_HANDLE };

        // Add these method declarations
        void createUniformBuffers();
        void createUniformDescriptorSetLayout();
        void createUniformDescriptorSets();
        void updateUniformBuffer();
        void createVertexBuffer();
        void renderVideoFrame();
        void createCommandBuffers();
        void helperInitImage(){
            cout << "Device: " << g_Device << "\n"
            "Physical Device" << g_PhysicalDevice << "\n"
            "Queue: " << g_Queue << endl; 
            imageHandler.setCurrentDeviceAndPhysic(g_Device, g_PhysicalDevice, g_Queue, g_QueueFamily, offscreenCommandPool, memoryProperties);
            textureHandler.setCurrentDeviceAndPhysic(g_Device, g_PhysicalDevice, g_Queue, g_QueueFamily, offscreenCommandPool, memoryProperties);
        }

        array<VkCommandBuffer, MAX_CONCURRENT_FRAMES> commandBuffers{};
        array<VkFence, MAX_CONCURRENT_FRAMES> waitFences{};
        void createSynchronizationPrimitives();

    private:

        VkPhysicalDeviceMemoryProperties memoryProperties{};

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
            u_int32_t& width, 
            u_int32_t& height
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
            VkPipelineColorBlendStateCreateInfo& colorBlending,
            VkPipelineDepthStencilStateCreateInfo& currentDepthStencilInfo,
            VkPipelineDynamicStateCreateInfo& currentDynameState,
            int& sizeShaderStage
        ){
            VkGraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.stageCount = sizeShaderStage;
            pipelineInfo.basePipelineIndex = -1;
			pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
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
            pipelineInfo.pDepthStencilState = &currentDepthStencilInfo;
            pipelineInfo.pNext = nullptr;
            pipelineInfo.pDynamicState = &currentDynameState;
            return pipelineInfo;
        }

        VkPipelineShaderStageCreateInfo createShaderStageInfo(VkShaderModule shaderModule){
            VkPipelineShaderStageCreateInfo stageInfo{};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
            stageInfo.module = shaderModule;
            stageInfo.pName = "main";

            return stageInfo;
        }

        void HandleRenderViewport(uint32_t width, uint32_t height);
};
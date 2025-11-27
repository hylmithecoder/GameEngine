#include <VkBuffer.hpp>
#include <VkDevice.hpp>
#include <filesystem>
#include <stdexcept>
#include <memory>
#include <typeinfo>

class TextureBase : public VulkanHandler {
    public :
        vkhandler::VulkanDevice vulkanDevice;
        struct Vertex {
            float pos[3];
            float uv[2];
            float normal[3];
        };

        // vector<ktx>
        string convertToKtx(const string& imageNameWillConvert);
        void loadTexture(const string& ktxTexturePath);

        virtual void setCurrentDeviceAndPhysic(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue graphicsQueue, VkCommandPool commandPool, VkPhysicalDeviceMemoryProperties memoryProperties, VkRenderPass renderPass, VkPhysicalDeviceProperties deviceProps, VkPhysicalDeviceFeatures deviceFeatures, VkDescriptorPool descriptorPool) {
            Log("Override to this vulkandevice class");
            
            try {
                currentDevice = device;
                currentPhysicalDevice = physicalDevice;
                currentOffscreenCommandPool = commandPool;
                currentGraphicsQueue = graphicsQueue;
                currentMemoryProperties = memoryProperties;
                currentDescriptorPool = descriptorPool;
                // currentRenderPass = renderPass;

                LogPointer("Get Device: ", currentDevice);
                LogPointer("Get Physical Device: ", currentPhysicalDevice);
                LogPointer("Get Queue: ", currentGraphicsQueue);
                LogPointer("Get Descriptor pool ", currentDescriptorPool);
                vulkanDevice.logicalDevice = device;
                vulkanDevice.physicalDevice = physicalDevice;
                vulkanDevice.memoryProperties = currentMemoryProperties;
                vulkanDevice.commandPool = commandPool;
                vulkanDevice.properties = deviceProps;
                vulkanDevice.features = deviceFeatures;

                if (vulkanDevice.features.samplerAnisotropy) {
                    vulkanDevice.enabledFeatures.samplerAnisotropy = VK_TRUE;
                    Log("Device supports samplerAnisotropy", LogLevel::SUCCESS);
                } else {
                    Log("Device does not support samplerAnisotropy", LogLevel::WARNING);
                }
                // setAllInitVulkanToVulkanDevice();
            } catch (const exception& e){
                cerr << e.what() << endl;
            }
        }

        void generateQuad();
        void setupDescriptors();
        void preparePipelines();
        void prepareUniformBuffers();
        void updateUniformBuffers();

        // Global for render current texture
        void buildCommandBuffer();
        void setupRenderPassTexture();

        struct UniformData {
            glm::mat4 projection;
            glm::mat4 modelView;
            glm::vec4 viewPos;
            // This is used to change the bias for the level-of-detail (mips) in the fragment shader
            float lodBias = 0.0f;
        } uniformData;
        
        array<vkhandler::Buffer, MAX_CONCURRENT_FRAMES> uniformBuffers;
        array<VkCommandBuffer, MAX_CONCURRENT_FRAMES> drawCmdBuffers;
        array<VkDescriptorSet, MAX_CONCURRENT_FRAMES> descriptorSets{};
        vkhandler::Buffer vertexBuffer;
        vkhandler::Buffer indexBuffer;
        uint32_t indexCount{ 0 };
        VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };

    private :
        struct Texture {
            VkSampler sampler{ VK_NULL_HANDLE };
            VkImage image{ VK_NULL_HANDLE };
            VkImageLayout imageLayout;
            VkDeviceMemory deviceMemory{ VK_NULL_HANDLE };
            VkImageView view{ VK_NULL_HANDLE };
            uint32_t width{ 0 };
            uint32_t height{ 0 };
            uint32_t mipLevels{ 0 };
        } texture;

        VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };

        VkPipeline pipeline{ VK_NULL_HANDLE };
        VkPipelineCache pipelineCache{ VK_NULL_HANDLE };
        VkFormat depthFormat {VK_FORMAT_UNDEFINED};

        // helper
        // VkPipelineShaderStageCreateInfo loadShader(const char* fileName, VkShaderStageFlagBits stage);
};

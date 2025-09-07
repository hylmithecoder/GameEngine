#include <vulkanhandler.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include <stdexcept>

VkDescriptorSet VulkanHandler::LoadImage(const char* filename){
     // Load PNG pakai stb_image
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(filename, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels)
        throw runtime_error("Failed to load texture image!");

    VkDeviceSize imageSize = texWidth * texHeight * 4;
    
    Debug::Logger::Log("Texture size: "+to_string(imageSize));
    // Buat image yang langsung bisa diakses CPU (HOST_VISIBLE)
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = texWidth;
    imageInfo.extent.height = texHeight;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_LINEAR; // langsung bisa diakses CPU
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkImage textureImage;
    if (vkCreateImage(currentDevice, &imageInfo, nullptr, &textureImage) != VK_SUCCESS)
        throw runtime_error("Failed to create image!");
        
    Debug::Logger::Log("Texture image: "+to_string(reinterpret_cast<uintptr_t>(textureImage)));

    // Alokasi memory
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(currentDevice, textureImage, &memRequirements);
    cout << "Mem requirements size: " << memRequirements.size << endl;
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;

    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(currentPhysicalDevice, &memProperties);
    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((memRequirements.memoryTypeBits & (1 << i)) &&
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

    VkDeviceMemory textureMemory;
    if (vkAllocateMemory(currentDevice, &allocInfo, nullptr, &textureMemory) != VK_SUCCESS)
        throw runtime_error("Failed to allocate image memory!");
    cout << "Allocinfo: " << allocInfo.allocationSize << endl;

    vkBindImageMemory(currentDevice, textureImage, textureMemory, 0);

    // Copy pixel data langsung ke image
    VkImageSubresource subresource{};
    subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(currentDevice, textureImage, &subresource, &layout);

    void* data;
    vkMapMemory(currentDevice, textureMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(currentDevice, textureMemory);

    stbi_image_free(pixels);
    cout << "Texture memory: " << textureMemory << endl;
    cout << "subresource: " << subresource.aspectMask << endl;
    cout << "Sucresource layout: " << layout.offset << endl;
    // Buat ImageView
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = textureImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView textureImageView;
    if (vkCreateImageView(currentDevice, &viewInfo, nullptr, &textureImageView) != VK_SUCCESS)
        throw runtime_error("Failed to create texture image view!");

    cout << "View Info: "<< textureImageView << endl;
    // Buat Sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    VkSampler textureSampler;
    if (vkCreateSampler(currentDevice, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS)
        throw runtime_error("Failed to create texture sampler!");
    // cout << "TextureSampler loaded: " << textureSampler << endl;
    // cout << "TextureImageView loaded: " << textureImageView << endl;
    Debug::Logger::Log("TextureSampler loaded: " + to_string(reinterpret_cast<uintptr_t>(textureSampler)));
    Debug::Logger::Log("TextureImageView loaded: " + to_string(reinterpret_cast<uintptr_t>(textureImageView)));
    // Tambahin ke ImGui
    VkDescriptorSet imguiDescSet = ImGui_ImplVulkan_AddTexture(
        textureSampler,
        textureImageView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    return imguiDescSet;
}

void VulkanHandler::setCurrentDeviceAndPhysic(VkDevice device, VkPhysicalDevice physicalDevice){
    Logger::Log("Setting current device and physical device", LogLevel::SUCCESS);
    currentDevice = device;
    currentPhysicalDevice = physicalDevice;
}
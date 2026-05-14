#include <vulkan/vulkan.h>
#include "../../../include/core_engine/Debugger.hpp"
#include <ktx.h>
#include <ktxvulkan.h>
#include <stb/stb_image.h>
#include <texture.hpp>
#define STB_IMAGE_IMPLEMENTATION

void TextureBase::generateQuad() {
  // Setup vertices for a single uv-mapped quad made from two triangles
  vector<Vertex> vertices = {
      {{1.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
      {{-1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
      {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
      {{1.0f, -1.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};

  // Setup indices
  vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};
  indexCount = static_cast<uint32_t>(indices.size());
  Log("Index count: " + to_string(static_cast<int>(indexCount)));

  // Create buffers and upload data to the GPU
  struct StagingBuffers {
    Buffer vertices;
    Buffer indices;
  } stagingBuffers;

  // Host visible source buffers (staging)
  VK_CHECK_RESULT(vulkanDevice.createBuffer(
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      &stagingBuffers.vertices, vertices.size() * sizeof(Vertex),
      vertices.data()));
  VK_CHECK_RESULT(vulkanDevice.createBuffer(
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      &stagingBuffers.indices, indices.size() * sizeof(uint32_t),
      indices.data()));

  // Device local destination buffers
  VK_CHECK_RESULT(vulkanDevice.createBuffer(
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &vertexBuffer,
      vertices.size() * sizeof(Vertex)));
  VK_CHECK_RESULT(vulkanDevice.createBuffer(
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &indexBuffer,
      indices.size() * sizeof(uint32_t)));

  // Copy from host do device
  vulkanDevice.copyBuffer(&stagingBuffers.vertices, &vertexBuffer,
                          currentGraphicsQueue);
  vulkanDevice.copyBuffer(&stagingBuffers.indices, &indexBuffer,
                          currentGraphicsQueue);
  Log("Success to create vertex buffer texture !", LogLevel::SUCCESS);
  // Clean up
  stagingBuffers.vertices.destroy();
  stagingBuffers.indices.destroy();
}

void TextureBase::setupDescriptors() {
  // Pool
  vector<VkDescriptorPoolSize> poolSizes = {
      initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                       MAX_CONCURRENT_FRAMES),
      // The sample uses a combined image + sampler descriptor to sample the
      // texture in the fragment shader We need multiple descriptors (NOT
      // images) due to how we set up the descriptor bindings in this sample
      initializers::descriptorPoolSize(
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_CONCURRENT_FRAMES)};
  VkDescriptorPoolCreateInfo descriptorPoolInfo =
      initializers::descriptorPoolCreateInfo(poolSizes, 2);
  VK_CHECK_RESULT(vkCreateDescriptorPool(currentDevice, &descriptorPoolInfo,
                                         nullptr, &currentDescriptorPool));

  // Layout
  vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
      // Binding 0 : Vertex shader uniform buffer
      initializers::descriptorSetLayoutBinding(
          VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
      // Binding 1 : Fragment shader image sampler
      initializers::descriptorSetLayoutBinding(
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          VK_SHADER_STAGE_FRAGMENT_BIT, 1)};
  VkDescriptorSetLayoutCreateInfo descriptorLayout =
      initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
  VK_CHECK_RESULT(vkCreateDescriptorSetLayout(currentDevice, &descriptorLayout,
                                              nullptr, &descriptorSetLayout));

  // Setup a descriptor image info for the current texture to be used as a
  // descriptor for a combined image sampler
  VkDescriptorImageInfo textureDescriptor{};
  // The image's view (images are never directly accessed by the shader, but
  // rather through views defining subresources)
  textureDescriptor.imageView = texture.view;
  // The sampler (Telling the pipeline how to sample the texture, including
  // repeat, border, etc.)
  textureDescriptor.sampler = texture.sampler;
  // The current layout of the image(Note: Should always fit the actual use,
  // e.g.shader read)
  textureDescriptor.imageLayout = texture.imageLayout;

  // Sets per frame, just like the buffers themselves
  VkDescriptorSetAllocateInfo allocInfo =
      initializers::descriptorSetAllocateInfo(currentDescriptorPool,
                                              &descriptorSetLayout, 1);
  DEBUG_LOG("Uniformbuffer size: %zu", uniformBuffers.size());
  DEBUG_LOGF("Texture view for descriptor: %p", LogLevel::SUCCESS,
             (void *)textureDescriptor.imageView);
  for (uint32_t i = 0; i < uniformBuffers.size(); i++) {
    VK_CHECK_RESULT(vkAllocateDescriptorSets(currentDevice, &allocInfo,
                                             &descriptorSets[i]));

    vector<VkWriteDescriptorSet> writeDescriptorSets = {
        // Binding 0 : Vertex shader uniform buffer
        initializers::writeDescriptorSet(descriptorSets[i],
                                         VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0,
                                         &uniformBuffers[i].descriptor),
        // Binding 1 : Fragment shader texture sampler
        initializers::writeDescriptorSet(
            descriptorSets[i], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
            &textureDescriptor)};

    DEBUG_LOG("Writing descriptor set %u with %zu writes", i,
              writeDescriptorSets.size());
    vkUpdateDescriptorSets(currentDevice,
                           static_cast<uint32_t>(writeDescriptorSets.size()),
                           writeDescriptorSets.data(), 0, nullptr);
    DEBUG_LOGF("Success to write descriptor set %u", LogLevel::SUCCESS, i);
  }
}

// void TextureBase::updateUniformBuffers(){
//     uniformData.projection = camera.matrices.perspective;
//     uniformData.modelView = camera.matrices.view;
//     uniformData.viewPos = camera.viewPos;
//     memcpy(uniformBuffers[currentBuffer].mapped, &uniformData,
//     sizeof(uniformData));
// }

void TextureBase::createCommandBuffers() {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = currentOffscreenCommandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = static_cast<uint32_t>(drawCmdBuffers.size());

  VK_CHECK_RESULT(vkAllocateCommandBuffers(currentDevice, &allocInfo,
                                           drawCmdBuffers.data()));
  DEBUG_LOG("Allocated %zu command buffers for texture rendering",
            drawCmdBuffers.size());
}

void TextureBase::cleanupPipeline() {
  if (pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(currentDevice, pipeline, nullptr);
    pipeline = VK_NULL_HANDLE;
  }
  if (pipelineCache != VK_NULL_HANDLE) {
    vkDestroyPipelineCache(currentDevice, pipelineCache, nullptr);
    pipelineCache = VK_NULL_HANDLE;
  }
  if (pipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(currentDevice, pipelineLayout, nullptr);
    pipelineLayout = VK_NULL_HANDLE;
  }
}

void TextureBase::preparePipelines() {
  // If pipeline already exists, clean it up before recreating (e.g. during
  // resize)
  if (pipeline != VK_NULL_HANDLE || pipelineLayout != VK_NULL_HANDLE) {
    cleanupPipeline();
  }

  try {
    // Validasi render pass sebelum membuat pipeline
    if (currentRenderPass == VK_NULL_HANDLE) {
      throw std::runtime_error(
          "Render pass not initialized! Call setupRenderPass() first.");
    } else {
      cout << "Render pass: " << currentRenderPass << endl;
    }

    if (currentDevice == VK_NULL_HANDLE) {
      throw std::runtime_error("Device not initialized!");
    }

    if (pipelineLayout == VK_NULL_HANDLE) {
      // Create pipeline layout
      if (descriptorSetLayout == VK_NULL_HANDLE) {
        throw std::runtime_error("Descriptor set layout not initialized! Call "
                                 "setupDescriptors() first.");
      } else {
        cout << "Descriptor set layout: " << descriptorSetLayout << endl;
      }
      VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo =
          initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);

      VK_CHECK_RESULT(vkCreatePipelineLayout(
          currentDevice, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));
      Log("Pipeline layout created successfully", LogLevel::SUCCESS);
    }

    // Setup pipeline states
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
        initializers::pipelineInputAssemblyStateCreateInfo(
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

    VkPipelineRasterizationStateCreateInfo rasterizationState =
        initializers::pipelineRasterizationStateCreateInfo(
            VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT,
            VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);

    VkPipelineColorBlendAttachmentState blendAttachmentState =
        initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);

    VkPipelineColorBlendStateCreateInfo colorBlendState =
        initializers::pipelineColorBlendStateCreateInfo(1,
                                                        &blendAttachmentState);

    VkPipelineDepthStencilStateCreateInfo depthStencilState =
        initializers::pipelineDepthStencilStateCreateInfo(
            VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);

    VkPipelineViewportStateCreateInfo viewportState =
        initializers::pipelineViewportStateCreateInfo(1, 1, 0);

    VkPipelineMultisampleStateCreateInfo multisampleState =
        initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT,
                                                         0);

    vector<VkDynamicState> dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT,
                                                  VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamicState =
        initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);

    // Create graphics pipeline
    VkGraphicsPipelineCreateInfo pipelineCreateInfo =
        initializers::pipelineCreateInfo(pipelineLayout, currentRenderPass, 0);

    assert(pipelineLayout != VK_NULL_HANDLE);

    VkPipelineCacheCreateInfo pipelineCacheCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    VK_CHECK_RESULT(vkCreatePipelineCache(
        currentDevice, &pipelineCacheCreateInfo, nullptr, &pipelineCache));
    // Load shader stages
    array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    try {
      shaderStages[0] = loadShader(
          (folderShadersVulkan() + "templatetexture.vert.spv").c_str(),
          VK_SHADER_STAGE_VERTEX_BIT);
      shaderStages[1] = loadShader(
          (folderShadersVulkan() + "templatetexture.frag.spv").c_str(),
          VK_SHADER_STAGE_FRAGMENT_BIT);

      Log("Shaders loaded successfully", LogLevel::SUCCESS);
      DEBUG_LOG("Vert shader module: %p", (void *)shaderStages[0].module);
      DEBUG_LOG("Frag shader module: %p", (void *)shaderStages[1].module);

    } catch (const std::exception &e) {
      throw std::runtime_error("Failed to load shaders: " +
                               std::string(e.what()));
    }

    // Setup vertex input state
    vector<VkVertexInputBindingDescription> vertexInputBindings = {
        initializers::vertexInputBindingDescription(
            0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX)};

    vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
        initializers::vertexInputAttributeDescription(
            0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)),
        initializers::vertexInputAttributeDescription(
            0, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)),
        initializers::vertexInputAttributeDescription(
            0, 2, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)),
    };

    VkPipelineVertexInputStateCreateInfo vertexInputState =
        initializers::pipelineVertexInputStateCreateInfo();
    vertexInputState.vertexBindingDescriptionCount =
        static_cast<uint32_t>(vertexInputBindings.size());
    vertexInputState.pVertexBindingDescriptions = vertexInputBindings.data();
    vertexInputState.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(vertexInputAttributes.size());
    vertexInputState.pVertexAttributeDescriptions =
        vertexInputAttributes.data();

    // LogPointer("Vertex input attributes: ", vertexInputAttributes.data());
    // LogPointer("Vertex input bindings: ", vertexInputBindings.data());
    // LogPointer("Input assembly state: ", inputAssemblyState);
    // LogPointer("Rasterization state: ", rasterizationState);
    // LogPointer("Colorblend state: ", colorBlendState);
    // LogPointer("Multisample state: ", multisampleState);
    // LogPointer("Viewport state: ", viewportState);
    // LogPointer("Depth stencil state: ", depthStencilState);
    pipelineCreateInfo.pVertexInputState = &vertexInputState;
    pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
    pipelineCreateInfo.pRasterizationState = &rasterizationState;
    pipelineCreateInfo.pColorBlendState = &colorBlendState;
    pipelineCreateInfo.pMultisampleState = &multisampleState;
    pipelineCreateInfo.pViewportState = &viewportState;
    pipelineCreateInfo.pDepthStencilState = &depthStencilState;
    pipelineCreateInfo.pDynamicState = &dynamicState;
    pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCreateInfo.pStages = shaderStages.data();

    // LogPointer("Pipeline Layout: ", pipelineLayout);
    // Log("stageCount=%u", pipelineCreateInfo.stageCount);
    // Log("vertexBindingCount=%u",
    // vertexInputState.vertexBindingDescriptionCount);
    // Log("vertexAttrCount=%u",
    // vertexInputState.vertexAttributeDescriptionCount); Log("viewportCount=%u
    // scissorCount=%u", viewportState.viewportCount,
    // viewportState.scissorCount); Log("colorBlendAttachmentCount=%u",
    // colorBlendState.attachmentCount); LogPointer("pStages",
    // pipelineCreateInfo.pStages);

    // // Log debugging info
    // Log("Creating graphics pipeline...");
    // Log("Shader stages: %i",  shaderStages.size());
    // Log("Vertex input bindings: %i",  vertexInputBindings.size());
    // Log("Vertex input attributes: %i",  vertexInputAttributes.size());
    VkRenderPass renderPass = currentRenderPass;
    DEBUG_LOG("Renderpass %p", renderPass);
    // LogPointer("Device ", currentDevice);

    if (vkCreateGraphicsPipelines(currentDevice, pipelineCache, 1,
                                  &pipelineCreateInfo, nullptr,
                                  &pipeline) != VK_SUCCESS) {
      throw runtime_error("Failed to create graphics pipeline!");
    } else {
      DEBUG_LOG("Graphic Pipeline info: %p", pipeline);
      // cout << "Graphic Pipeline: " << graphicsPipeline << endl;
    }
    // VK_CHECK_RESULT(vkCreateGraphicsPipelines(currentDevice, pipelineCache,
    // 1, &pipelineCreateInfo, nullptr, &pipeline));
    Log("Graphics pipeline created successfully!", LogLevel::SUCCESS);

  } catch (const std::exception &e) {
    Log("Error in preparePipelines: " + std::string(e.what()), LogLevel::CRASH);
    throw e;
  } catch (...) {
    Log("Unknown error in preparePipelines", LogLevel::CRASH);
    throw;
  }
}

void TextureBase::prepareUniformBuffers() {
  for (auto &buffer : uniformBuffers) {
    VK_CHECK_RESULT(
        vulkanDevice.createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  &buffer, sizeof(UniformData), &uniformData));
    VK_CHECK_RESULT(buffer.map());
    for (int i = 0; i < buffer.size; i++) {
      DEBUG_LOGF("Buffer map: %i", LogLevel::SUCCESS,
                 static_cast<int>(buffer[i]));
    }
  }
  Log("Success to prepare uniform buffers texture !", LogLevel::SUCCESS);
}

string TextureBase::convertToKtx(const string &imageNameWillConvert) {
  int width = 0, height = 0, channels = 0;

  // Load with stb_image (force RGBA)
  stbi_uc *pixels = stbi_load(imageNameWillConvert.c_str(), &width, &height,
                              &channels, STBI_rgb_alpha);
  if (!pixels) {
    throw std::runtime_error("Failed to load image: " + imageNameWillConvert);
  }

  // Auto-free pixels with RAII
  std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixelGuard(
      pixels, stbi_image_free);

  // KTX creation info
  ktxTextureCreateInfo createInfo{};
  createInfo.vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
  createInfo.baseWidth = width;
  createInfo.baseHeight = height;
  createInfo.baseDepth = 1;
  createInfo.numDimensions = 2;
  createInfo.numLevels = 1;
  createInfo.numLayers = 1;
  createInfo.numFaces = 1;
  createInfo.isArray = KTX_FALSE;
  createInfo.generateMipmaps = KTX_FALSE;

  // Create KTX texture
  ktxTexture2 *kTexture = nullptr;
  KTX_error_code result = ktxTexture2_Create(
      &createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &kTexture);
  if (result != KTX_SUCCESS) {
    throw std::runtime_error("Failed to create KTX texture");
  }

  // RAII guard for KTX texture
  ktxTexture *ktxUnique = reinterpret_cast<ktxTexture *>(kTexture);
  std::unique_ptr<ktxTexture2, void (*)(ktxTexture2 *)> ktxGuard(
      kTexture, ktxTexture2_Destroy);

  // Upload pixel data
  ktx_size_t dataSize = width * height * 4;
  result =
      ktxTexture_SetImageFromMemory(reinterpret_cast<ktxTexture *>(kTexture), 0,
                                    0, 0, pixelGuard.get(), dataSize);

  if (result != KTX_SUCCESS) {
    throw std::runtime_error("Failed to upload image data to KTX");
  }

  // Output filename: file.png → file.ktx
  std::filesystem::path outPath = imageNameWillConvert;
  outPath.replace_extension(".ktx");

  result = ktxTexture_WriteToNamedFile(reinterpret_cast<ktxTexture *>(kTexture),
                                       outPath.string().c_str());
  if (result != KTX_SUCCESS) {
    throw std::runtime_error("Failed to write KTX file: " + outPath.string());
  }
  Log("KTX file created: " + outPath.string(), LogLevel::SUCCESS);
  return outPath.string();
}

void TextureBase::loadTexture(const string &ktxTexturePath) {
  Log("Load Texture now: " + ktxTexturePath, LogLevel::SUCCESS);
  // We use the Khronos texture format
  // (https://www.khronos.org/opengles/sdk/tools/KTX/file_format_spec/)
  std::string filename = ktxTexturePath;
  // Texture data contains 4 channels (RGBA) with unnormalized 8-bit values,
  // this is the most commonly supported format
  VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

  ktxResult result;
  ktxTexture *ktxTexture;

#if defined(__ANDROID__)
  // Textures are stored inside the apk on Android (compressed)
  // So they need to be loaded via the asset manager
  AAsset *asset = AAssetManager_open(androidApp->activity->assetManager,
                                     filename.c_str(), AASSET_MODE_STREAMING);
  if (!asset) {
    tools::exitFatal("Could not load texture from " + filename +
                         "\n\nMake sure the assets submodule has been checked "
                         "out and is up-to-date.",
                     -1);
  }
  size_t size = AAsset_getLength(asset);
  assert(size > 0);

  ktx_uint8_t *textureData = new ktx_uint8_t[size];
  AAsset_read(asset, textureData, size);
  AAsset_close(asset);
  result = ktxTexture_CreateFromMemory(
      textureData, size, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
  delete[] textureData;
#else
  if (!tools::fileExists(filename)) {
    tools::exitFatal("Could not load texture from " + filename +
                         "\n\nMake sure the assets submodule has been checked "
                         "out and is up-to-date.",
                     -1);
  }
  result = ktxTexture_CreateFromNamedFile(
      filename.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
#endif
  assert(result == KTX_SUCCESS);

  // Get properties required for using and upload texture data from the ktx
  // texture object
  texture.width = ktxTexture->baseWidth;
  texture.height = ktxTexture->baseHeight;
  texture.mipLevels = ktxTexture->numLevels;
  ktx_uint8_t *ktxTextureData = ktxTexture_GetData(ktxTexture);
  ktx_size_t ktxTextureSize = ktxTexture_GetDataSize(ktxTexture);

  // We prefer using staging to copy the texture data to a device local optimal
  // image
  VkBool32 useStaging = true;

  // Only use linear tiling if forced
  bool forceLinearTiling = false;
  if (forceLinearTiling) {
    // Don't use linear if format is not supported for (linear) shader sampling
    // Get device properties for the requested texture format
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(currentPhysicalDevice, format,
                                        &formatProperties);
    useStaging = !(formatProperties.linearTilingFeatures &
                   VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
  }

  VkMemoryAllocateInfo memAllocInfo = initializers::memoryAllocateInfo();
  VkMemoryRequirements memReqs = {};

  if (useStaging) {
    // Copy data to an optimal tiled image
    // This loads the texture data into a host local buffer that is copied to
    // the optimal tiled image on the device

    // Create a host-visible staging buffer that contains the raw image data
    // This buffer will be the data source for copying texture data to the
    // optimal tiled image on the device
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    VkBufferCreateInfo bufferCreateInfo = initializers::bufferCreateInfo();
    bufferCreateInfo.size = ktxTextureSize;
    // This buffer is used as a transfer source for the buffer copy
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK_RESULT(vkCreateBuffer(currentDevice, &bufferCreateInfo, nullptr,
                                   &stagingBuffer));

    // Get memory requirements for the staging buffer (alignment, memory type
    // bits)
    vkGetBufferMemoryRequirements(currentDevice, stagingBuffer, &memReqs);
    memAllocInfo.allocationSize = memReqs.size;
    // Get memory type index for a host visible buffer
    memAllocInfo.memoryTypeIndex = vulkanDevice.getMemoryType(
        memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK_RESULT(vkAllocateMemory(currentDevice, &memAllocInfo, nullptr,
                                     &stagingMemory));
    VK_CHECK_RESULT(
        vkBindBufferMemory(currentDevice, stagingBuffer, stagingMemory, 0));

    // Copy texture data into host local staging buffer
    uint8_t *data;
    VK_CHECK_RESULT(vkMapMemory(currentDevice, stagingMemory, 0, memReqs.size,
                                0, (void **)&data));
    memcpy(data, ktxTextureData, ktxTextureSize);
    vkUnmapMemory(currentDevice, stagingMemory);

    // Setup buffer copy regions for each mip level
    std::vector<VkBufferImageCopy> bufferCopyRegions;
    uint32_t offset = 0;

    for (uint32_t i = 0; i < texture.mipLevels; i++) {
      // Calculate offset into staging buffer for the current mip level
      ktx_size_t offset;
      KTX_error_code ret =
          ktxTexture_GetImageOffset(ktxTexture, i, 0, 0, &offset);
      assert(ret == KTX_SUCCESS);
      // Setup a buffer image copy structure for the current mip level
      VkBufferImageCopy bufferCopyRegion = {};
      bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      bufferCopyRegion.imageSubresource.mipLevel = i;
      bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
      bufferCopyRegion.imageSubresource.layerCount = 1;
      bufferCopyRegion.imageExtent.width = ktxTexture->baseWidth >> i;
      bufferCopyRegion.imageExtent.height = ktxTexture->baseHeight >> i;
      bufferCopyRegion.imageExtent.depth = 1;
      bufferCopyRegion.bufferOffset = offset;
      bufferCopyRegions.push_back(bufferCopyRegion);
    }

    // Create optimal tiled target image on the device
    VkImageCreateInfo imageCreateInfo = initializers::imageCreateInfo();
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = format;
    imageCreateInfo.mipLevels = texture.mipLevels;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    // Set initial layout of the image to undefined
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageCreateInfo.extent = {texture.width, texture.height, 1};
    imageCreateInfo.usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VK_CHECK_RESULT(vkCreateImage(currentDevice, &imageCreateInfo, nullptr,
                                  &texture.image));

    vkGetImageMemoryRequirements(currentDevice, texture.image, &memReqs);
    memAllocInfo.allocationSize = memReqs.size;
    memAllocInfo.memoryTypeIndex = vulkanDevice.getMemoryType(
        memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK_RESULT(vkAllocateMemory(currentDevice, &memAllocInfo, nullptr,
                                     &texture.deviceMemory));
    VK_CHECK_RESULT(vkBindImageMemory(currentDevice, texture.image,
                                      texture.deviceMemory, 0));

    VkCommandBuffer copyCmd =
        vulkanDevice.createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

    // Image memory barriers for the texture image

    // The sub resource range describes the regions of the image that will be
    // transitioned using the memory barriers below
    VkImageSubresourceRange subresourceRange = {};
    // Image only contains color data
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    // Start at first mip level
    subresourceRange.baseMipLevel = 0;
    // We will transition on all mip levels
    subresourceRange.levelCount = texture.mipLevels;
    // The 2D texture only has one layer
    subresourceRange.layerCount = 1;

    // Transition the texture image layout to transfer target, so we can safely
    // copy our buffer data to it.
    VkImageMemoryBarrier imageMemoryBarrier =
        initializers::imageMemoryBarrier();
    imageMemoryBarrier.image = texture.image;
    imageMemoryBarrier.subresourceRange = subresourceRange;
    imageMemoryBarrier.srcAccessMask = 0;
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    // Insert a memory dependency at the proper pipeline stages that will
    // execute the image layout transition Source pipeline stage is host
    // write/read execution (VK_PIPELINE_STAGE_HOST_BIT) Destination pipeline
    // stage is copy command execution (VK_PIPELINE_STAGE_TRANSFER_BIT)
    vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_HOST_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &imageMemoryBarrier);

    // Copy mip levels from staging buffer
    vkCmdCopyBufferToImage(copyCmd, stagingBuffer, texture.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(bufferCopyRegions.size()),
                           bufferCopyRegions.data());

    // Once the data has been uploaded we transfer to the texture image to the
    // shader read layout, so it can be sampled from
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Insert a memory dependency at the proper pipeline stages that will
    // execute the image layout transition Source pipeline stage is copy command
    // execution (VK_PIPELINE_STAGE_TRANSFER_BIT) Destination pipeline stage
    // fragment shader access (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
    vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &imageMemoryBarrier);

    // Store current layout for later reuse
    texture.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    vulkanDevice.flushCommandBuffer(copyCmd, currentGraphicsQueue, true);

    // Clean up staging resources
    vkFreeMemory(currentDevice, stagingMemory, nullptr);
    vkDestroyBuffer(currentDevice, stagingBuffer, nullptr);
  } else {
    // Copy data to a linear tiled image

    VkImage mappableImage;
    VkDeviceMemory mappableMemory;

    // Load mip map level 0 to linear tiling image
    VkImageCreateInfo imageCreateInfo = initializers::imageCreateInfo();
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = format;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
    imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
    imageCreateInfo.extent = {texture.width, texture.height, 1};
    VK_CHECK_RESULT(vkCreateImage(currentDevice, &imageCreateInfo, nullptr,
                                  &mappableImage));

    // Get memory requirements for this image like size and alignment
    vkGetImageMemoryRequirements(currentDevice, mappableImage, &memReqs);
    // Set memory allocation size to required memory size
    memAllocInfo.allocationSize = memReqs.size;
    // Get memory type that can be mapped to host memory
    memAllocInfo.memoryTypeIndex = vulkanDevice.getMemoryType(
        memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK_RESULT(vkAllocateMemory(currentDevice, &memAllocInfo, nullptr,
                                     &mappableMemory));
    VK_CHECK_RESULT(
        vkBindImageMemory(currentDevice, mappableImage, mappableMemory, 0));

    // Map image memory
    void *data;
    VK_CHECK_RESULT(
        vkMapMemory(currentDevice, mappableMemory, 0, memReqs.size, 0, &data));
    // Copy image data of the first mip level into memory
    memcpy(data, ktxTextureData, memReqs.size);
    vkUnmapMemory(currentDevice, mappableMemory);

    // Linear tiled images don't need to be staged and can be directly used as
    // textures
    texture.image = mappableImage;
    texture.deviceMemory = mappableMemory;
    texture.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Setup image memory barrier transfer image to shader read layout
    VkCommandBuffer copyCmd =
        vulkanDevice.createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

    // The sub resource range describes the regions of the image we will be
    // transition
    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.layerCount = 1;

    // Transition the texture image layout to shader read, so it can be sampled
    // from
    VkImageMemoryBarrier imageMemoryBarrier =
        initializers::imageMemoryBarrier();
    imageMemoryBarrier.image = texture.image;
    imageMemoryBarrier.subresourceRange = subresourceRange;
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Insert a memory dependency at the proper pipeline stages that will
    // execute the image layout transition Source pipeline stage is host
    // write/read execution (VK_PIPELINE_STAGE_HOST_BIT) Destination pipeline
    // stage fragment shader access (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
    vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_HOST_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &imageMemoryBarrier);

    vulkanDevice.flushCommandBuffer(copyCmd, currentGraphicsQueue, true);
  }

  ktxTexture_Destroy(ktxTexture);

  // Create a texture sampler
  // In Vulkan textures are accessed by samplers
  // This separates all the sampling information from the texture data. This
  // means you could have multiple sampler objects for the same texture with
  // different settings Note: Similar to the samplers available with OpenGL 3.3
  VkSamplerCreateInfo sampler = initializers::samplerCreateInfo();
  sampler.magFilter = VK_FILTER_LINEAR;
  sampler.minFilter = VK_FILTER_LINEAR;
  sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler.mipLodBias = 0.0f;
  sampler.compareOp = VK_COMPARE_OP_NEVER;
  sampler.minLod = 0.0f;
  // Set max level-of-detail to mip level count of the texture
  sampler.maxLod = (useStaging) ? (float)texture.mipLevels : 0.0f;
  // Enable anisotropic filtering
  // This feature is optional, so we must check if it's supported on the device
  if (vulkanDevice.features.samplerAnisotropy) {
    // Use max. level of anisotropy for this example
    sampler.maxAnisotropy = vulkanDevice.properties.limits.maxSamplerAnisotropy;
    sampler.anisotropyEnable = VK_TRUE;
  } else {
    // The device does not support anisotropic filtering
    sampler.maxAnisotropy = 1.0;
    sampler.anisotropyEnable = VK_FALSE;
  }
  sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
  VK_CHECK_RESULT(
      vkCreateSampler(currentDevice, &sampler, nullptr, &texture.sampler));

  // Create image view
  // Textures are not directly accessed by the shaders and
  // are abstracted by image views containing additional
  // information and sub resource ranges
  VkImageViewCreateInfo view = initializers::imageViewCreateInfo();
  view.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view.format = format;
  // The subresource range describes the set of mip levels (and array layers)
  // that can be accessed through this image view It's possible to create
  // multiple image views for a single image referring to different (and/or
  // overlapping) ranges of the image
  view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view.subresourceRange.baseMipLevel = 0;
  view.subresourceRange.baseArrayLayer = 0;
  view.subresourceRange.layerCount = 1;
  // Linear tiling usually won't support mip maps
  // Only set mip map count if optimal tiling is used
  view.subresourceRange.levelCount = (useStaging) ? texture.mipLevels : 1;
  // The view will be based on the texture's image
  view.image = texture.image;
  VK_CHECK_RESULT(
      vkCreateImageView(currentDevice, &view, nullptr, &texture.view));
  Log("Successfully to create a texture");
}

void TextureBase::buildCommandBuffer() {
  // DEBUG_LOGF("Building command buffer %d", LogLevel::INFO, currentFrame);
  VkCommandBuffer cmdBuffer = drawCmdBuffers[currentFrame];

  // Reset command buffer before recording new commands
  VK_CHECK_RESULT(vkResetCommandBuffer(cmdBuffer, 0));

  // Safety check for handles
  if (currentFrameBuffers.empty() ||
      currentFrame >= currentFrameBuffers.size() ||
      currentFrameBuffers[currentFrame] == VK_NULL_HANDLE ||
      currentRenderPass == VK_NULL_HANDLE) {
    return;
  }

  VkCommandBufferBeginInfo cmdBufInfo = initializers::commandBufferBeginInfo(0);

  VkClearValue clearValues[2]{};
  clearValues[0].color = defaultClearColor;
  clearValues[1].depthStencil = {1.0f, 0};

  VkRenderPassBeginInfo renderPassBeginInfo =
      initializers::renderPassBeginInfo();
  renderPassBeginInfo.pNext = nullptr;
  renderPassBeginInfo.renderPass = currentRenderPass;
  renderPassBeginInfo.renderArea.offset.x = 0;
  renderPassBeginInfo.renderArea.offset.y = 0;
  renderPassBeginInfo.renderArea.extent = {width, height};
  renderPassBeginInfo.clearValueCount = 2;
  renderPassBeginInfo.pClearValues = clearValues;
  renderPassBeginInfo.framebuffer =
      currentFrameBuffers[currentFrame]; // Use current frame's framebuffer

  const VkCommandBuffer commandBuffer = drawCmdBuffers[currentFrame];
  VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &cmdBufInfo));

  vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo,
                       VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport =
      initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

  VkRect2D scissor = initializers::rect2D(width, height, 0, 0);
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

  // This will bind the descriptor set that contains our image (texture), so it
  // can be accessed in the fragment shader
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipelineLayout, 0, 1, &descriptorSets[currentFrame],
                          0, nullptr);
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

  VkDeviceSize offsets[1] = {0};
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer.buffer, offsets);
  vkCmdBindIndexBuffer(commandBuffer, indexBuffer.buffer, 0,
                       VK_INDEX_TYPE_UINT32);

  vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);

  // drawUI(cmdBuffer);

  vkCmdEndRenderPass(commandBuffer);

  VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));
}

void TextureBase::setupRenderPassTexture() {
  std::array<VkAttachmentDescription, 2> attachments{
      // Color attachment
      VkAttachmentDescription{.format = VK_FORMAT_B8G8R8A8_UNORM,
                              .samples = VK_SAMPLE_COUNT_1_BIT,
                              .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                              .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                              .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                              .stencilStoreOp =
                                  VK_ATTACHMENT_STORE_OP_DONT_CARE,
                              .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                              .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
      // Depth attachment
      VkAttachmentDescription{
          .format = depthFormat,
          .samples = VK_SAMPLE_COUNT_1_BIT,
          .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
          .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}};

  VkAttachmentReference colorReference{
      .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference depthReference{
      .attachment = 1,
      .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

  VkSubpassDescription subpassDescription{
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount = 1,
      .pColorAttachments = &colorReference,
      .pDepthStencilAttachment = &depthReference,
  };

  // Subpass dependencies for layout transitions
  std::array<VkSubpassDependency, 2> dependencies{
      VkSubpassDependency{
          .srcSubpass = VK_SUBPASS_EXTERNAL,
          .dstSubpass = 0,
          .srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
          .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
          .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
      },
      VkSubpassDependency{
          .srcSubpass = VK_SUBPASS_EXTERNAL,
          .dstSubpass = 0,
          .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          .srcAccessMask = 0,
          .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
      }};

  VkRenderPassCreateInfo renderPassInfo{
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = static_cast<uint32_t>(attachments.size()),
      .pAttachments = attachments.data(),
      .subpassCount = 1,
      .pSubpasses = &subpassDescription,
      .dependencyCount = static_cast<uint32_t>(dependencies.size()),
      .pDependencies = dependencies.data(),
  };
  VK_CHECK_RESULT(vkCreateRenderPass(currentDevice, &renderPassInfo, nullptr,
                                     &currentRenderPass));
}

void TextureBase::updateUniformBuffers(uint32_t &currentBuffer) {
  // DEBUG_LOGF("Update uniform buffer %d", LogLevel::INFO, currentBuffer);
  currentFrame = currentBuffer;
  uniformData.projection = camera.matrices.perspective;
  uniformData.modelView = camera.matrices.view;
  uniformData.viewPos = camera.viewPos;
  memcpy(uniformBuffers[currentFrame].mapped, &uniformData,
         sizeof(uniformData));
}

void TextureBase::prepareFrame() {
  VK_CHECK_RESULT(vkWaitForFences(currentDevice, 1, &waitFences[currentFrame],
                                  VK_TRUE, UINT64_MAX));
  VK_CHECK_RESULT(vkResetFences(currentDevice, 1, &waitFences[currentFrame]));
}

void TextureBase::createSynchronization() {
  for (uint32_t i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
    VkFenceCreateInfo fenceCI{};
    fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    // Create the fences in signaled state (so we don't wait on first render of
    // each command buffer)
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    // Fence used to ensure that command buffer has completed exection before
    // using it again
    VK_CHECK_RESULT(
        vkCreateFence(currentDevice, &fenceCI, nullptr, &waitFences[i]));
  }
  // Semaphores are used for correct command ordering within a queue
  // Used to ensure that image presentation is complete before starting to
  // submit again
  presentCompleteSemaphores.resize(MAX_CONCURRENT_FRAMES);
  for (auto &semaphore : presentCompleteSemaphores) {
    VkSemaphoreCreateInfo semaphoreCI{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK_CHECK_RESULT(
        vkCreateSemaphore(currentDevice, &semaphoreCI, nullptr, &semaphore));
  }
  // Render completion
  // Use MAX_CONCURRENT_FRAMES since TextureBase uses parent's swapchain
  renderCompleteSemaphores.resize(MAX_CONCURRENT_FRAMES);
  for (auto &semaphore : renderCompleteSemaphores) {
    VkSemaphoreCreateInfo semaphoreCI{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK_CHECK_RESULT(
        vkCreateSemaphore(currentDevice, &semaphoreCI, nullptr, &semaphore));
  }
  Log("TextureBase synchronization objects created", LogLevel::SUCCESS);
}
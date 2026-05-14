#include "../../../include/core_engine/SceneRenderer2D.hpp"
#include "../../../include/core_engine/Debugger.hpp"
#include <VkTools.hpp>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui_impl_vulkan.h>
#include <iostream>
#include <sstream>
#include <vector>

using namespace Debug;
using namespace std;

SceneRenderer2D::SceneRenderer2D(int width, int height)
    : width(width), height(height), cameraZoom(1.0f), gridVisible(false),
      gridSize(10.0f), snapToGrid(false), cameraPosition(0.0f, 0.0f) {
  cout << "Creating SceneRenderer2D (Vulkan): " << width << "x" << height
       << endl;
}

SceneRenderer2D::~SceneRenderer2D() {
  if (device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device);
    DestroyOffscreenResources();

    // Destroy pipelines
    if (spritePipeline != VK_NULL_HANDLE)
      vkDestroyPipeline(device, spritePipeline, nullptr);
    if (spritePipelineLayout != VK_NULL_HANDLE)
      vkDestroyPipelineLayout(device, spritePipelineLayout, nullptr);
    if (spriteDescriptorSetLayout != VK_NULL_HANDLE)
      vkDestroyDescriptorSetLayout(device, spriteDescriptorSetLayout, nullptr);
    if (gridPipeline != VK_NULL_HANDLE)
      vkDestroyPipeline(device, gridPipeline, nullptr);
    if (gridPipelineLayout != VK_NULL_HANDLE)
      vkDestroyPipelineLayout(device, gridPipelineLayout, nullptr);
    if (gizmoPipeline != VK_NULL_HANDLE)
      vkDestroyPipeline(device, gizmoPipeline, nullptr);
    if (gizmoPipelineLayout != VK_NULL_HANDLE)
      vkDestroyPipelineLayout(device, gizmoPipelineLayout, nullptr);

    // Destroy buffers
    if (quadBuffer.buffer != VK_NULL_HANDLE)
      vkDestroyBuffer(device, quadBuffer.buffer, nullptr);
    if (quadBuffer.memory != VK_NULL_HANDLE)
      vkFreeMemory(device, quadBuffer.memory, nullptr);
    if (gridBuffer.buffer != VK_NULL_HANDLE)
      vkDestroyBuffer(device, gridBuffer.buffer, nullptr);
    if (gridBuffer.memory != VK_NULL_HANDLE)
      vkFreeMemory(device, gridBuffer.memory, nullptr);
  }
}

void SceneRenderer2D::SetVulkanContext(VkDevice device,
                                       VkPhysicalDevice physicalDevice,
                                       VkQueue graphicsQueue,
                                       VkCommandPool commandPool,
                                       VkDescriptorPool descriptorPool) {
  this->device = device;
  this->physicalDevice = physicalDevice;
  this->graphicsQueue = graphicsQueue;
  this->commandPool = commandPool;
  this->descriptorPool = descriptorPool;

  InitVulkanResources();
}

void SceneRenderer2D::InitVulkanResources() {
  if (device == VK_NULL_HANDLE)
    return;

  CreateOffscreenResources();
  InitPipelines();

  // Create Quad Vertex Buffer (for sprites)
  struct Vertex {
    glm::vec2 pos;
    glm::vec2 uv;
  };

  std::vector<Vertex> vertices = {{{-0.5f, -0.5f}, {0.0f, 0.0f}},
                                  {{0.5f, -0.5f}, {1.0f, 0.0f}},
                                  {{0.5f, 0.5f}, {1.0f, 1.0f}},
                                  {{-0.5f, 0.5f}, {0.0f, 1.0f}}};

  VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = bufferSize;
  bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(device, &bufferInfo, nullptr, &quadBuffer.buffer) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create vertex buffer!");
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(device, quadBuffer.buffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryType(
      memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (vkAllocateMemory(device, &allocInfo, nullptr, &quadBuffer.memory) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to allocate vertex buffer memory!");
  }

  vkBindBufferMemory(device, quadBuffer.buffer, quadBuffer.memory, 0);

  void *data;
  vkMapMemory(device, quadBuffer.memory, 0, bufferSize, 0, &data);
  memcpy(data, vertices.data(), (size_t)bufferSize);
  vkUnmapMemory(device, quadBuffer.memory);
}

void SceneRenderer2D::SetViewportSize(int newWidth, int newHeight) {
  if (width == newWidth && height == newHeight)
    return;

  width = newWidth;
  height = newHeight;

  if (device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device);
    DestroyOffscreenResources();
    CreateOffscreenResources();
  }
}

void SceneRenderer2D::CreateOffscreenResources() {
  // 1. Create Image
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = offscreen.format;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

  if (vkCreateImage(device, &imageInfo, nullptr, &offscreen.image) !=
      VK_SUCCESS) {
    throw runtime_error("Failed to create offscreen image!");
  }

  // 2. Memory Requirements & Allocation
  VkMemoryRequirements memReqs;
  vkGetImageMemoryRequirements(device, offscreen.image, &memReqs);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex = findMemoryType(
      memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (vkAllocateMemory(device, &allocInfo, nullptr, &offscreen.memory) !=
      VK_SUCCESS) {
    throw runtime_error("Failed to allocate offscreen image memory!");
  }
  vkBindImageMemory(device, offscreen.image, offscreen.memory, 0);

  // 3. Create ImageView
  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = offscreen.image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = offscreen.format;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(device, &viewInfo, nullptr, &offscreen.view) !=
      VK_SUCCESS) {
    throw runtime_error("Failed to create offscreen image view!");
  }

  // 4. Create Sampler
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
  if (vkCreateSampler(device, &samplerInfo, nullptr, &offscreen.sampler) !=
      VK_SUCCESS) {
    throw runtime_error("Failed to create offscreen sampler!");
  }

  // 5. Create Render Pass
  VkAttachmentDescription colorAttachment{};
  colorAttachment.format = offscreen.format;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkAttachmentReference colorAttachmentRef{};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;

  VkRenderPassCreateInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = 1;
  renderPassInfo.pAttachments = &colorAttachment;
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;

  if (vkCreateRenderPass(device, &renderPassInfo, nullptr,
                         &offscreen.renderPass) != VK_SUCCESS) {
    throw runtime_error("Failed to create offscreen render pass!");
  }

  // 6. Create Framebuffer
  VkFramebufferCreateInfo framebufferInfo{};
  framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebufferInfo.renderPass = offscreen.renderPass;
  framebufferInfo.attachmentCount = 1;
  framebufferInfo.pAttachments = &offscreen.view;
  framebufferInfo.width = width;
  framebufferInfo.height = height;
  framebufferInfo.layers = 1;

  if (vkCreateFramebuffer(device, &framebufferInfo, nullptr,
                          &offscreen.framebuffer) != VK_SUCCESS) {
    throw runtime_error("Failed to create offscreen framebuffer!");
  }

  // 7. Descriptor Set for ImGui
  offscreen.descriptorSet =
      ImGui_ImplVulkan_AddTexture(offscreen.sampler, offscreen.view,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void SceneRenderer2D::DestroyOffscreenResources() {
  if (offscreen.framebuffer != VK_NULL_HANDLE)
    vkDestroyFramebuffer(device, offscreen.framebuffer, nullptr);
  if (offscreen.renderPass != VK_NULL_HANDLE)
    vkDestroyRenderPass(device, offscreen.renderPass, nullptr);
  if (offscreen.sampler != VK_NULL_HANDLE)
    vkDestroySampler(device, offscreen.sampler, nullptr);
  if (offscreen.view != VK_NULL_HANDLE)
    vkDestroyImageView(device, offscreen.view, nullptr);
  if (offscreen.image != VK_NULL_HANDLE)
    vkDestroyImage(device, offscreen.image, nullptr);
  if (offscreen.memory != VK_NULL_HANDLE)
    vkFreeMemory(device, offscreen.memory, nullptr);

  offscreen.framebuffer = VK_NULL_HANDLE;
  offscreen.renderPass = VK_NULL_HANDLE;
  offscreen.sampler = VK_NULL_HANDLE;
  offscreen.view = VK_NULL_HANDLE;
  offscreen.image = VK_NULL_HANDLE;
  offscreen.memory = VK_NULL_HANDLE;
  offscreen.descriptorSet = VK_NULL_HANDLE;
}

uint32_t SceneRenderer2D::findMemoryType(uint32_t typeFilter,
                                         VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags &
                                    properties) == properties) {
      return i;
    }
  }

  throw std::runtime_error("failed to find suitable memory type!");
}

void SceneRenderer2D::RenderSceneToTexture(const Scene &scene) {
  if (device == VK_NULL_HANDLE || offscreen.framebuffer == VK_NULL_HANDLE)
    return;

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer cmd;
  if (vkAllocateCommandBuffers(device, &allocInfo, &cmd) != VK_SUCCESS) {
    return;
  }

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(cmd, &beginInfo);

  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = offscreen.renderPass;
  renderPassInfo.framebuffer = offscreen.framebuffer;
  renderPassInfo.renderArea.offset = {0, 0};
  renderPassInfo.renderArea.extent = {(uint32_t)width, (uint32_t)height};

  VkClearValue clearValues[1];
  clearValues[0].color = {{bgColor.x, bgColor.y, bgColor.z, bgColor.w}};
  renderPassInfo.clearValueCount = 1;
  renderPassInfo.pClearValues = clearValues;

  vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = (float)width;
  viewport.height = (float)height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(cmd, 0, 1, &viewport);

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = {(uint32_t)width, (uint32_t)height};
  vkCmdSetScissor(cmd, 0, 1, &scissor);

  glm::mat4 projection = glm::ortho(
      -width * 0.5f / cameraZoom, width * 0.5f / cameraZoom,
      -height * 0.5f / cameraZoom, height * 0.5f / cameraZoom, -1.0f, 1.0f);

  glm::mat4 view = glm::translate(
      glm::mat4(1.0f), glm::vec3(-cameraPosition.x, -cameraPosition.y, 0.0f));

  if (gridVisible) {
    DrawGrid(cmd, projection, view);
  }

  for (const auto &obj : scene.objects) {
    VkDescriptorSet textureDesc =
        textureManager.GetTextureDescriptor(obj.spritePath);

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(obj.x, obj.y, 0.0f));
    model = glm::rotate(model, glm::radians(obj.rotation), glm::vec3(0, 0, 1));
    model = glm::scale(model, glm::vec3(obj.width * obj.scaleX,
                                        obj.height * obj.scaleY, 1.0f));

    glm::mat4 mvp = projection * view * model;
    DrawSprite(cmd, textureDesc, mvp);
  }

  vkCmdEndRenderPass(cmd);
  vkEndCommandBuffer(cmd);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;

  vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(graphicsQueue);

  vkFreeCommandBuffers(device, commandPool, 1, &cmd);
}

VkDescriptorSet SceneRenderer2D::GetViewportDescriptorSet() const {
  return offscreen.descriptorSet;
}

void SceneRenderer2D::InitPipelines() {
  if (device == VK_NULL_HANDLE)
    return;

  // Load Shaders
  VkShaderModule vertShader =
      LoadShader("assets/shaders/vulkan/sprite.vert.spv");
  VkShaderModule fragShader =
      LoadShader("assets/shaders/vulkan/sprite.frag.spv");

  // Descriptor Set Layout for Sprites
  VkDescriptorSetLayoutBinding samplerLayoutBinding{};
  samplerLayoutBinding.binding = 0;
  samplerLayoutBinding.descriptorType =
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  samplerLayoutBinding.descriptorCount = 1;
  samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  samplerLayoutBinding.pImmutableSamplers = nullptr;

  VkDescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = 1;
  layoutInfo.pBindings = &samplerLayoutBinding;

  if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                  &spriteDescriptorSetLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor set layout!");
  }

  VkPipelineShaderStageCreateInfo shaderStages[2];
  shaderStages[0] = {};
  shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  shaderStages[0].module = vertShader;
  shaderStages[0].pName = "main";

  shaderStages[1] = {};
  shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderStages[1].module = fragShader;
  shaderStages[1].pName = "main";

  // Vertex Input
  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(glm::vec2) * 2; // pos + uv
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription attributeDescriptions[2];
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[0].offset = 0;

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[1].offset = sizeof(glm::vec2);

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  vertexInputInfo.vertexAttributeDescriptionCount = 2;
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

  // Input Assembly
  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  // Viewport and Scissor (Dynamic)
  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  // Rasterizer
  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_NONE;
  rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;

  // Multisampling
  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  // Color Blending
  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_TRUE;
  colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  colorBlendAttachment.dstColorBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;

  // Dynamic State
  std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                               VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
  dynamicState.pDynamicStates = dynamicStates.data();

  // Pipeline Layout
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(glm::mat4);

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &spriteDescriptorSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

  if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                             &spritePipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }

  // Create Pipeline
  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = shaderStages;
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.layout = spritePipelineLayout;
  pipelineInfo.renderPass = offscreen.renderPass;
  pipelineInfo.subpass = 0;
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                nullptr, &spritePipeline) != VK_SUCCESS) {
    throw std::runtime_error("failed to create graphics pipeline!");
  }

  vkDestroyShaderModule(device, vertShader, nullptr);
  vkDestroyShaderModule(device, fragShader, nullptr);
}

void SceneRenderer2D::DrawGrid(VkCommandBuffer cmd, const glm::mat4 &projection,
                               const glm::mat4 &view) {
  if (gridPipeline == VK_NULL_HANDLE)
    return;
}

void SceneRenderer2D::DrawSprite(VkCommandBuffer cmd,
                                 VkDescriptorSet textureDescriptor,
                                 const glm::mat4 &mvp) {
  if (spritePipeline == VK_NULL_HANDLE)
    return;

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, spritePipeline);

  // Bind Push Constants
  vkCmdPushConstants(cmd, spritePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                     sizeof(glm::mat4), glm::value_ptr(mvp));

  if (textureDescriptor != VK_NULL_HANDLE) {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            spritePipelineLayout, 0, 1, &textureDescriptor, 0,
                            nullptr);
  }

  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(cmd, 0, 1, &quadBuffer.buffer, offsets);

  vkCmdDraw(cmd, 4, 1, 0, 0);
}

void SceneRenderer2D::DrawSelectionGizmo(const GameObject &obj) {}

void SceneRenderer2D::SetEditMode(EditMode mode) { currentMode = mode; }
void SceneRenderer2D::SetGridVisible(bool visible) { gridVisible = visible; }
void SceneRenderer2D::SetGridSize(float size) { gridSize = size; }
void SceneRenderer2D::SetSnapToGrid(bool snap) { snapToGrid = snap; }

void SceneRenderer2D::ResetCamera() {
  cameraPosition = glm::vec2(0.0f, 0.0f);
  cameraZoom = 1.0f;
}

void SceneRenderer2D::SetCameraZoom(float zoom) {
  this->cameraZoom = std::max(0.1f, std::min(zoom, 10.0f));
}

void SceneRenderer2D::SetGridColor(float r, float g, float b, float a) {
  gridColor = ImVec4(r, g, b, a);
}

void SceneRenderer2D::SetBackgroundColor(float r, float g, float b, float a) {
  bgColor = ImVec4(r, g, b, a);
}

glm::vec2 SceneRenderer2D::ViewportToWorldPosition(float viewX,
                                                   float viewY) const {
  float worldX = (viewX - width * 0.5f) / cameraZoom + cameraPosition.x;
  float worldY = (viewY - height * 0.5f) / cameraZoom + cameraPosition.y;
  return glm::vec2(worldX, worldY);
}

glm::vec2 SceneRenderer2D::WorldToViewportPosition(float worldX,
                                                   float worldY) const {
  float viewX = (worldX - cameraPosition.x) * cameraZoom + width * 0.5f;
  float viewY = (worldY - cameraPosition.y) * cameraZoom + height * 0.5f;
  return glm::vec2(viewX, viewY);
}

void SceneRenderer2D::HandleClick(float worldX, float worldY) {
  selectedObject = nullptr;
  selectedObjectIndex = -1;
  for (int i = (int)currentScene.objects.size() - 1; i >= 0; i--) {
    GameObject &obj = currentScene.objects[i];
    if (worldX >= obj.x && worldX <= obj.x + obj.width * obj.scaleX &&
        worldY >= obj.y && worldY <= obj.y + obj.height * obj.scaleY) {
      selectedObject = &obj;
      selectedObjectIndex = i;
      break;
    }
  }
}

void SceneRenderer2D::HandleDrag(float deltaX, float deltaY) {
  float scaledDeltaX = deltaX / cameraZoom;
  float scaledDeltaY = deltaY / cameraZoom;
  if (selectedObject != nullptr && currentMode == EditMode::MOVE) {
    selectedObject->x += scaledDeltaX;
    selectedObject->y += scaledDeltaY;
    if (snapToGrid) {
      selectedObject->x = round(selectedObject->x / gridSize) * gridSize;
      selectedObject->y = round(selectedObject->y / gridSize) * gridSize;
    }
  } else if (selectedObject != nullptr && currentMode == EditMode::ROTATE) {
    selectedObject->rotation += scaledDeltaX * 0.5f;
  } else if (selectedObject != nullptr && currentMode == EditMode::SCALE) {
    selectedObject->scaleX =
        std::max(0.1f, selectedObject->scaleX + scaledDeltaX * 0.01f);
    selectedObject->scaleY =
        std::max(0.1f, selectedObject->scaleY + scaledDeltaY * 0.01f);
  } else {
    cameraPosition.x -= scaledDeltaX;
    cameraPosition.y -= scaledDeltaY;
  }
}

void SceneRenderer2D::HandleZoom(float delta) {
  float zoomFactor = 0.1f;
  cameraZoom =
      std::max(0.1f, std::min(cameraZoom * (1.0f + delta * zoomFactor), 5.0f));
}

void SceneRenderer2D::MoveSelected(float deltaX, float deltaY) {
  if (selectedObject) {
    selectedObject->x += deltaX;
    selectedObject->y += deltaY;
  }
}

void SceneRenderer2D::DeleteSelected() {
  if (selectedObjectIndex >= 0 &&
      selectedObjectIndex < (int)currentScene.objects.size()) {
    currentScene.objects.erase(currentScene.objects.begin() +
                               selectedObjectIndex);
    selectedObject = nullptr;
    selectedObjectIndex = -1;
  }
}

bool SceneRenderer2D::HasSelectedObject() const {
  return selectedObject != nullptr;
}

VkShaderModule SceneRenderer2D::LoadShader(const std::string &path) {
  std::ifstream file(path, std::ios::ate | std::ios::binary);
  if (!file.is_open())
    throw std::runtime_error("Failed to open shader file: " + path);

  size_t fileSize = (size_t)file.tellg();
  std::vector<char> buffer(fileSize);
  file.seekg(0);
  file.read(buffer.data(), fileSize);
  file.close();

  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = buffer.size();
  createInfo.pCode = reinterpret_cast<const uint32_t *>(buffer.data());

  VkShaderModule shaderModule;
  if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) !=
      VK_SUCCESS)
    throw std::runtime_error("Failed to create shader module!");

  return shaderModule;
}

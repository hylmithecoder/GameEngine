#include "../../../include/core_engine/SceneRenderer2D.hpp"
#include "../../../include/core_engine/Debugger.hpp"
#include <VkTools.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui_impl_vulkan.h>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
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
    if (testTrianglePipeline != VK_NULL_HANDLE)
      vkDestroyPipeline(device, testTrianglePipeline, nullptr);
    if (testTrianglePipelineLayout != VK_NULL_HANDLE)
      vkDestroyPipelineLayout(device, testTrianglePipelineLayout, nullptr);
    if (meshPipeline != VK_NULL_HANDLE)
      vkDestroyPipeline(device, meshPipeline, nullptr);
    if (meshPipelineLayout != VK_NULL_HANDLE)
      vkDestroyPipelineLayout(device, meshPipelineLayout, nullptr);
    if (grid3d.pipeline != VK_NULL_HANDLE)
      vkDestroyPipeline(device, grid3d.pipeline, nullptr);
    if (grid3d.layout != VK_NULL_HANDLE)
      vkDestroyPipelineLayout(device, grid3d.layout, nullptr);
    if (grid3d.vertexBuffer != VK_NULL_HANDLE)
      vkDestroyBuffer(device, grid3d.vertexBuffer, nullptr);
    if (grid3d.vertexMemory != VK_NULL_HANDLE)
      vkFreeMemory(device, grid3d.vertexMemory, nullptr);

    // Destroy 3D mesh resources
    for (auto &m : meshes3d) {
      DestroyMesh(m);
    }
    meshes3d.clear();

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

  // 4b. Create Depth Image + view (needed for 3D rendering; 2D paths
  // simply leave depth test disabled so the new attachment is harmless
  // for them).
  VkImageCreateInfo depthImageInfo{};
  depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
  depthImageInfo.format = offscreen.depthFormat;
  depthImageInfo.extent.width = width;
  depthImageInfo.extent.height = height;
  depthImageInfo.extent.depth = 1;
  depthImageInfo.mipLevels = 1;
  depthImageInfo.arrayLayers = 1;
  depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkCreateImage(device, &depthImageInfo, nullptr, &offscreen.depthImage) !=
      VK_SUCCESS) {
    throw runtime_error("Failed to create offscreen depth image!");
  }
  VkMemoryRequirements depthMemReqs;
  vkGetImageMemoryRequirements(device, offscreen.depthImage, &depthMemReqs);
  VkMemoryAllocateInfo depthAllocInfo{};
  depthAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  depthAllocInfo.allocationSize = depthMemReqs.size;
  depthAllocInfo.memoryTypeIndex = findMemoryType(
      depthMemReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (vkAllocateMemory(device, &depthAllocInfo, nullptr,
                       &offscreen.depthMemory) != VK_SUCCESS) {
    throw runtime_error("Failed to allocate offscreen depth memory!");
  }
  vkBindImageMemory(device, offscreen.depthImage, offscreen.depthMemory, 0);

  VkImageViewCreateInfo depthViewInfo{};
  depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  depthViewInfo.image = offscreen.depthImage;
  depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  depthViewInfo.format = offscreen.depthFormat;
  depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  depthViewInfo.subresourceRange.levelCount = 1;
  depthViewInfo.subresourceRange.layerCount = 1;
  if (vkCreateImageView(device, &depthViewInfo, nullptr,
                        &offscreen.depthView) != VK_SUCCESS) {
    throw runtime_error("Failed to create offscreen depth view!");
  }

  // 5. Create Render Pass
  VkAttachmentDescription attachments[2]{};
  attachments[0].format = offscreen.format;
  attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  attachments[1].format = offscreen.depthFormat;
  attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorAttachmentRef{};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depthAttachmentRef{};
  depthAttachmentRef.attachment = 1;
  depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;
  subpass.pDepthStencilAttachment = &depthAttachmentRef;

  VkRenderPassCreateInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = 2;
  renderPassInfo.pAttachments = attachments;
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;

  if (vkCreateRenderPass(device, &renderPassInfo, nullptr,
                         &offscreen.renderPass) != VK_SUCCESS) {
    throw runtime_error("Failed to create offscreen render pass!");
  }

  // 6. Create Framebuffer
  VkImageView fbAttachments[2] = {offscreen.view, offscreen.depthView};
  VkFramebufferCreateInfo framebufferInfo{};
  framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebufferInfo.renderPass = offscreen.renderPass;
  framebufferInfo.attachmentCount = 2;
  framebufferInfo.pAttachments = fbAttachments;
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
  if (offscreen.depthView != VK_NULL_HANDLE)
    vkDestroyImageView(device, offscreen.depthView, nullptr);
  if (offscreen.depthImage != VK_NULL_HANDLE)
    vkDestroyImage(device, offscreen.depthImage, nullptr);
  if (offscreen.depthMemory != VK_NULL_HANDLE)
    vkFreeMemory(device, offscreen.depthMemory, nullptr);

  offscreen.framebuffer = VK_NULL_HANDLE;
  offscreen.renderPass = VK_NULL_HANDLE;
  offscreen.sampler = VK_NULL_HANDLE;
  offscreen.view = VK_NULL_HANDLE;
  offscreen.image = VK_NULL_HANDLE;
  offscreen.memory = VK_NULL_HANDLE;
  offscreen.depthView = VK_NULL_HANDLE;
  offscreen.depthImage = VK_NULL_HANDLE;
  offscreen.depthMemory = VK_NULL_HANDLE;
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

  VkClearValue clearValues[2];
  clearValues[0].color = {{bgColor.x, bgColor.y, bgColor.z, bgColor.w}};
  clearValues[1].depthStencil = {1.0f, 0};
  renderPassInfo.clearValueCount = 2;
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

  // Smoke test: a coloured triangle in clip space, ignored by camera
  // transform. Useful to confirm the offscreen render pass + ImGui
  // descriptor pipeline is intact when the scene has no objects yet.
  if (showTestTriangle) {
    DrawTestTriangle(cmd);
  }

  // Draw any loaded 3D meshes with the FPS camera. Always compute the
  // VP matrix when we have a 3D context so the grid follows the camera
  // even without meshes loaded.
  {
    float aspect = (height > 0) ? (float)width / (float)height : 1.0f;
    float yawR = glm::radians(camera3d.yaw);
    float pitchR = glm::radians(camera3d.pitch);
    glm::vec3 forward;
    forward.x = std::cos(yawR) * std::cos(pitchR);
    forward.y = std::sin(pitchR);
    forward.z = std::sin(yawR) * std::cos(pitchR);
    forward = glm::normalize(forward);

    glm::mat4 view3d =
        glm::lookAt(camera3d.position, camera3d.position + forward,
                    glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj3d = glm::perspective(glm::radians(camera3d.fovDeg), aspect,
                                        camera3d.nearPlane, camera3d.farPlane);
    // No Y-flip on projection: the Scene panel already flips the
    // sampled image via ImGui UVs (V from 1→0), so applying a
    // projection Y-flip on top would render the model upside down.
    glm::mat4 vp3d = proj3d * view3d;

    if (grid3dVisible && grid3d.pipeline != VK_NULL_HANDLE)
      DrawGrid3D(cmd, vp3d);

    if (!meshes3d.empty() && meshPipeline != VK_NULL_HANDLE)
      DrawMeshes(cmd, vp3d);
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

  // Depth: disabled for sprite pipeline so the new depth attachment
  // doesn't reject blended quads. Render pass requires the state to be
  // present though.
  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_FALSE;
  depthStencil.depthWriteEnable = VK_FALSE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;

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
  pipelineInfo.pDepthStencilState = &depthStencil;
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

  InitTestTrianglePipeline();
  InitMeshPipeline();
  InitGridResources();
}

void SceneRenderer2D::InitTestTrianglePipeline() {
  if (device == VK_NULL_HANDLE)
    return;

  VkShaderModule vert =
      LoadShader("assets/shaders/vulkan/scene_triangle.vert.spv");
  VkShaderModule frag =
      LoadShader("assets/shaders/vulkan/scene_triangle.frag.spv");

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag;
  stages[1].pName = "main";

  // Shader supplies its own vertex data via gl_VertexIndex, so no
  // bindings or attributes are declared.
  VkPipelineVertexInputStateCreateInfo vertexInput{};
  vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo raster{};
  raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo ms{};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState blendAttach{};
  blendAttach.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  blendAttach.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo blend{};
  blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  blend.attachmentCount = 1;
  blend.pAttachments = &blendAttach;

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_FALSE;
  depthStencil.depthWriteEnable = VK_FALSE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;

  std::vector<VkDynamicState> dyn = {VK_DYNAMIC_STATE_VIEWPORT,
                                     VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = static_cast<uint32_t>(dyn.size());
  dynamicState.pDynamicStates = dyn.data();

  // Empty pipeline layout: no descriptors, no push constants.
  VkPipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  if (vkCreatePipelineLayout(device, &layoutInfo, nullptr,
                             &testTrianglePipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create test triangle pipeline layout");
  }

  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = stages;
  pipelineInfo.pVertexInputState = &vertexInput;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &raster;
  pipelineInfo.pMultisampleState = &ms;
  pipelineInfo.pColorBlendState = &blend;
  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.pDepthStencilState = &depthStencil;
  pipelineInfo.layout = testTrianglePipelineLayout;
  pipelineInfo.renderPass = offscreen.renderPass;
  pipelineInfo.subpass = 0;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                nullptr, &testTrianglePipeline) != VK_SUCCESS) {
    throw std::runtime_error("failed to create test triangle pipeline");
  }

  vkDestroyShaderModule(device, vert, nullptr);
  vkDestroyShaderModule(device, frag, nullptr);
}

void SceneRenderer2D::DrawTestTriangle(VkCommandBuffer cmd) {
  if (testTrianglePipeline == VK_NULL_HANDLE)
    return;
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, testTrianglePipeline);
  vkCmdDraw(cmd, 3, 1, 0, 0);
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

// ---------------------------------------------------------------------------
// 3D mesh pipeline + OBJ loading + FPS camera
// ---------------------------------------------------------------------------

void SceneRenderer2D::InitMeshPipeline() {
  if (device == VK_NULL_HANDLE)
    return;

  VkShaderModule vert = LoadShader("assets/shaders/vulkan/scene_mesh.vert.spv");
  VkShaderModule frag = LoadShader("assets/shaders/vulkan/scene_mesh.frag.spv");

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag;
  stages[1].pName = "main";

  VkVertexInputBindingDescription binding{};
  binding.binding = 0;
  binding.stride = sizeof(MeshVertex);
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription attrs[2]{};
  attrs[0].binding = 0;
  attrs[0].location = 0;
  attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attrs[0].offset = offsetof(MeshVertex, pos);
  attrs[1].binding = 0;
  attrs[1].location = 1;
  attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attrs[1].offset = offsetof(MeshVertex, normal);

  VkPipelineVertexInputStateCreateInfo vi{};
  vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vi.vertexBindingDescriptionCount = 1;
  vi.pVertexBindingDescriptions = &binding;
  vi.vertexAttributeDescriptionCount = 2;
  vi.pVertexAttributeDescriptions = attrs;

  VkPipelineInputAssemblyStateCreateInfo ia{};
  ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo vp{};
  vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vp.viewportCount = 1;
  vp.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rs{};
  rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  // Two-sided: many OBJ exports have inconsistent winding and we are
  // rendering into a UV-flipped target which would otherwise invert
  // back/front. Disabling cull keeps the model visible from any angle.
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo ms{};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState cba{};
  cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  cba.blendEnable = VK_FALSE;
  VkPipelineColorBlendStateCreateInfo cb{};
  cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  cb.attachmentCount = 1;
  cb.pAttachments = &cba;

  VkPipelineDepthStencilStateCreateInfo ds{};
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  ds.depthTestEnable = VK_TRUE;
  ds.depthWriteEnable = VK_TRUE;
  ds.depthCompareOp = VK_COMPARE_OP_LESS;

  std::vector<VkDynamicState> dyn = {VK_DYNAMIC_STATE_VIEWPORT,
                                     VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynState{};
  dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynState.dynamicStateCount = (uint32_t)dyn.size();
  dynState.pDynamicStates = dyn.data();

  VkPushConstantRange pc{};
  pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  pc.offset = 0;
  pc.size = sizeof(glm::mat4) * 2; // mvp + model

  VkPipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &pc;
  if (vkCreatePipelineLayout(device, &layoutInfo, nullptr,
                             &meshPipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create mesh pipeline layout");
  }

  VkGraphicsPipelineCreateInfo pi{};
  pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pi.stageCount = 2;
  pi.pStages = stages;
  pi.pVertexInputState = &vi;
  pi.pInputAssemblyState = &ia;
  pi.pViewportState = &vp;
  pi.pRasterizationState = &rs;
  pi.pMultisampleState = &ms;
  pi.pColorBlendState = &cb;
  pi.pDepthStencilState = &ds;
  pi.pDynamicState = &dynState;
  pi.layout = meshPipelineLayout;
  pi.renderPass = offscreen.renderPass;
  pi.subpass = 0;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr,
                                &meshPipeline) != VK_SUCCESS) {
    throw std::runtime_error("failed to create mesh pipeline");
  }

  vkDestroyShaderModule(device, vert, nullptr);
  vkDestroyShaderModule(device, frag, nullptr);
}

void SceneRenderer2D::DrawMeshes(VkCommandBuffer cmd,
                                 const glm::mat4 &viewProj) {
  if (meshPipeline == VK_NULL_HANDLE)
    return;
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline);

  for (const auto &m : meshes3d) {
    if (m.indexCount == 0)
      continue;
    glm::mat4 model = m.ComputeModel();
    glm::mat4 mvp = viewProj * model;
    struct PC {
      glm::mat4 mvp;
      glm::mat4 model;
    } pc{mvp, model};
    vkCmdPushConstants(cmd, meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(PC), &pc);
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &m.vertexBuffer, offsets);
    vkCmdBindIndexBuffer(cmd, m.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, m.indexCount, 1, 0, 0, 0);
  }
}

bool SceneRenderer2D::UploadMeshBuffers(Mesh3D &mesh,
                                        const std::vector<MeshVertex> &verts,
                                        const std::vector<uint32_t> &indices) {
  if (device == VK_NULL_HANDLE || verts.empty() || indices.empty())
    return false;

  auto makeBuf = [&](VkDeviceSize size, VkBufferUsageFlags usage,
                     VkBuffer &outBuf, VkDeviceMemory &outMem) -> bool {
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bi, nullptr, &outBuf) != VK_SUCCESS)
      return false;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device, outBuf, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = findMemoryType(
        mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device, &ai, nullptr, &outMem) != VK_SUCCESS)
      return false;
    vkBindBufferMemory(device, outBuf, outMem, 0);
    return true;
  };

  VkDeviceSize vSize = sizeof(MeshVertex) * verts.size();
  if (!makeBuf(vSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertexBuffer,
               mesh.vertexMemory))
    return false;
  void *vmap = nullptr;
  vkMapMemory(device, mesh.vertexMemory, 0, vSize, 0, &vmap);
  std::memcpy(vmap, verts.data(), (size_t)vSize);
  vkUnmapMemory(device, mesh.vertexMemory);

  VkDeviceSize iSize = sizeof(uint32_t) * indices.size();
  if (!makeBuf(iSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.indexBuffer,
               mesh.indexMemory))
    return false;
  void *imap = nullptr;
  vkMapMemory(device, mesh.indexMemory, 0, iSize, 0, &imap);
  std::memcpy(imap, indices.data(), (size_t)iSize);
  vkUnmapMemory(device, mesh.indexMemory);

  mesh.indexCount = (uint32_t)indices.size();
  return true;
}

void SceneRenderer2D::DestroyMesh(Mesh3D &mesh) {
  if (mesh.vertexBuffer != VK_NULL_HANDLE)
    vkDestroyBuffer(device, mesh.vertexBuffer, nullptr);
  if (mesh.vertexMemory != VK_NULL_HANDLE)
    vkFreeMemory(device, mesh.vertexMemory, nullptr);
  if (mesh.indexBuffer != VK_NULL_HANDLE)
    vkDestroyBuffer(device, mesh.indexBuffer, nullptr);
  if (mesh.indexMemory != VK_NULL_HANDLE)
    vkFreeMemory(device, mesh.indexMemory, nullptr);
  mesh.vertexBuffer = VK_NULL_HANDLE;
  mesh.vertexMemory = VK_NULL_HANDLE;
  mesh.indexBuffer = VK_NULL_HANDLE;
  mesh.indexMemory = VK_NULL_HANDLE;
  mesh.indexCount = 0;
}

namespace {
// Pack two int32 indices into a single 64-bit key for dedup.
struct VNKey {
  int32_t v;
  int32_t n;
  bool operator==(const VNKey &o) const noexcept {
    return v == o.v && n == o.n;
  }
};
struct VNKeyHash {
  size_t operator()(const VNKey &k) const noexcept {
    return std::hash<uint64_t>()(((uint64_t)(uint32_t)k.v) |
                                 (((uint64_t)(uint32_t)k.n) << 32));
  }
};

// Parse "v/t/n" or "v//n" or "v/t" or "v". Returns false on empty.
bool parseFaceVert(const char *s, int &vi, int &ti, int &ni) {
  vi = ti = ni = 0;
  if (!*s)
    return false;
  char *end = nullptr;
  vi = (int)std::strtol(s, &end, 10);
  if (end == s)
    return false;
  if (*end == '/') {
    s = end + 1;
    if (*s != '/') {
      ti = (int)std::strtol(s, &end, 10);
    } else {
      end = const_cast<char *>(s);
    }
    if (*end == '/') {
      s = end + 1;
      ni = (int)std::strtol(s, &end, 10);
    }
  }
  return true;
}
} // namespace

bool SceneRenderer2D::LoadObjMesh(const std::string &path) {
  if (device == VK_NULL_HANDLE) {
    cout << "[OBJ] Vulkan context not ready, deferring load: " << path << endl;
    return false;
  }

  std::ifstream in(path);
  if (!in.is_open()) {
    cout << "[OBJ] Failed to open: " << path << endl;
    return false;
  }

  std::vector<glm::vec3> positions;
  std::vector<glm::vec3> normals;
  positions.reserve(50000);
  normals.reserve(50000);

  std::vector<MeshVertex> outVerts;
  std::vector<uint32_t> outIndices;
  outVerts.reserve(60000);
  outIndices.reserve(150000);

  std::unordered_map<VNKey, uint32_t, VNKeyHash> dedup;
  dedup.reserve(60000);

  glm::vec3 mn(std::numeric_limits<float>::max());
  glm::vec3 mx(-std::numeric_limits<float>::max());

  std::string line;
  line.reserve(256);
  while (std::getline(in, line)) {
    if (line.empty())
      continue;
    if (line[0] == 'v' && line.size() > 1) {
      if (line[1] == ' ') {
        glm::vec3 p;
        if (std::sscanf(line.c_str() + 2, "%f %f %f", &p.x, &p.y, &p.z) == 3) {
          positions.push_back(p);
          mn = glm::min(mn, p);
          mx = glm::max(mx, p);
        }
      } else if (line[1] == 'n' && line.size() > 2 && line[2] == ' ') {
        glm::vec3 n;
        if (std::sscanf(line.c_str() + 3, "%f %f %f", &n.x, &n.y, &n.z) == 3) {
          normals.push_back(n);
        }
      }
    } else if (line[0] == 'f' && line.size() > 2 && line[1] == ' ') {
      // Tokenize face entries
      const char *p = line.c_str() + 2;
      int faceIdx[16];
      int faceCount = 0;
      while (*p && faceCount < 16) {
        while (*p == ' ' || *p == '\t')
          ++p;
        if (!*p || *p == '\r' || *p == '\n')
          break;
        int vi = 0, ti = 0, ni = 0;
        if (!parseFaceVert(p, vi, ti, ni))
          break;
        // advance past current token
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
          ++p;
        // resolve negative indices
        int vIndex = (vi > 0) ? (vi - 1) : ((int)positions.size() + vi);
        int nIndex =
            (ni > 0) ? (ni - 1) : ((ni < 0) ? (int)normals.size() + ni : -1);
        if (vIndex < 0 || vIndex >= (int)positions.size())
          break;
        VNKey k{vIndex, nIndex};
        uint32_t outIdx;
        auto it = dedup.find(k);
        if (it == dedup.end()) {
          MeshVertex mv;
          mv.pos = positions[vIndex];
          if (nIndex >= 0 && nIndex < (int)normals.size())
            mv.normal = normals[nIndex];
          else
            mv.normal = glm::vec3(0.0f, 1.0f, 0.0f);
          outIdx = (uint32_t)outVerts.size();
          outVerts.push_back(mv);
          dedup.emplace(k, outIdx);
        } else {
          outIdx = it->second;
        }
        faceIdx[faceCount++] = (int)outIdx;
      }
      // Fan-triangulate
      for (int i = 1; i + 1 < faceCount; ++i) {
        outIndices.push_back((uint32_t)faceIdx[0]);
        outIndices.push_back((uint32_t)faceIdx[i]);
        outIndices.push_back((uint32_t)faceIdx[i + 1]);
      }
    }
  }

  if (outVerts.empty() || outIndices.empty()) {
    cout << "[OBJ] No geometry parsed from " << path << endl;
    return false;
  }

  // If the file had no normals, derive flat normals so lighting works.
  if (normals.empty()) {
    std::vector<glm::vec3> accum(outVerts.size(), glm::vec3(0.0f));
    for (size_t i = 0; i + 2 < outIndices.size(); i += 3) {
      uint32_t a = outIndices[i + 0];
      uint32_t b = outIndices[i + 1];
      uint32_t c = outIndices[i + 2];
      glm::vec3 e1 = outVerts[b].pos - outVerts[a].pos;
      glm::vec3 e2 = outVerts[c].pos - outVerts[a].pos;
      glm::vec3 n = glm::cross(e1, e2);
      accum[a] += n;
      accum[b] += n;
      accum[c] += n;
    }
    for (size_t i = 0; i < outVerts.size(); ++i) {
      glm::vec3 n = accum[i];
      float len = glm::length(n);
      outVerts[i].normal = (len > 1e-8f) ? (n / len) : glm::vec3(0, 1, 0);
    }
  }

  Mesh3D mesh;
  mesh.path = path;
  mesh.aabbMin = mn;
  mesh.aabbMax = mx;

  // Auto-fit: center the mesh on origin and scale so its longest axis
  // is ~1.5 units. Keeps the default camera placement (0,1.6,3) from
  // staring into the inside of a giant model.
  glm::vec3 center = (mn + mx) * 0.5f;
  glm::vec3 extent = mx - mn;
  float longest = std::max(extent.x, std::max(extent.y, extent.z));
  float fitScale = (longest > 1e-6f) ? (1.5f / longest) : 1.0f;
  glm::mat4 M(1.0f);
  M = glm::scale(M, glm::vec3(fitScale));
  M = glm::translate(M, -center);
  mesh.autoFit = M;
  mesh.vertexCount = (uint32_t)outVerts.size();

  if (!UploadMeshBuffers(mesh, outVerts, outIndices)) {
    cout << "[OBJ] Failed to upload mesh buffers" << endl;
    DestroyMesh(mesh);
    return false;
  }

  cout << "[OBJ] Loaded " << path << " — verts: " << outVerts.size()
       << ", tris: " << (outIndices.size() / 3) << endl;
  // Derive a display name from the file stem.
  std::string stem = path;
  size_t slash = stem.find_last_of("/\\");
  if (slash != std::string::npos)
    stem = stem.substr(slash + 1);
  size_t dot = stem.find_last_of('.');
  if (dot != std::string::npos)
    stem = stem.substr(0, dot);
  mesh.displayName = stem.empty() ? "Mesh" : stem;
  meshes3d.push_back(std::move(mesh));
  return true;
}

void SceneRenderer2D::UpdateCamera3D(const ViewportInput &in) {
  if (!in.hovered)
    return;

  // Mouselook only while the right mouse button is held — matches Unity
  // / Blender / UE conventions and avoids fighting ImGui hover state.
  if (in.rmbDown) {
    camera3d.yaw += in.mouseDeltaX * camera3d.mouseSensitivity;
    camera3d.pitch -= in.mouseDeltaY * camera3d.mouseSensitivity;
    if (camera3d.pitch > 89.0f)
      camera3d.pitch = 89.0f;
    if (camera3d.pitch < -89.0f)
      camera3d.pitch = -89.0f;
  }

  float yawR = glm::radians(camera3d.yaw);
  float pitchR = glm::radians(camera3d.pitch);
  glm::vec3 forward;
  forward.x = std::cos(yawR) * std::cos(pitchR);
  forward.y = std::sin(pitchR);
  forward.z = std::sin(yawR) * std::cos(pitchR);
  forward = glm::normalize(forward);
  glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
  glm::vec3 up = glm::vec3(0, 1, 0);

  float speed =
      camera3d.moveSpeed * (in.shiftDown ? 4.0f : 1.0f) * in.deltaTime;
  glm::vec3 delta(0.0f);
  if (in.wDown)
    delta += forward;
  if (in.sDown)
    delta -= forward;
  if (in.dDown)
    delta += right;
  if (in.aDown)
    delta -= right;
  if (in.eDown)
    delta += up;
  if (in.qDown)
    delta -= up;

  if (glm::dot(delta, delta) > 0.0f)
    camera3d.position += glm::normalize(delta) * speed;

  if (in.scroll != 0.0f) {
    camera3d.position += forward * in.scroll * 0.5f;
  }
}

// ---------------------------------------------------------------------------
// Per-mesh transform composition
// ---------------------------------------------------------------------------

glm::mat4 SceneRenderer2D::Mesh3D::ComputeModel() const {
  glm::mat4 M(1.0f);
  M = glm::translate(M, userPosition);
  M = glm::rotate(M, glm::radians(userRotation.z), glm::vec3(0, 0, 1));
  M = glm::rotate(M, glm::radians(userRotation.y), glm::vec3(0, 1, 0));
  M = glm::rotate(M, glm::radians(userRotation.x), glm::vec3(1, 0, 0));
  M = glm::scale(M, userScale);
  return M * autoFit;
}

glm::vec3 SceneRenderer2D::GetMesh3DPosition(size_t i) const {
  if (i >= meshes3d.size())
    return glm::vec3(0.0f);
  return meshes3d[i].userPosition;
}
glm::vec3 SceneRenderer2D::GetMesh3DRotation(size_t i) const {
  if (i >= meshes3d.size())
    return glm::vec3(0.0f);
  return meshes3d[i].userRotation;
}
glm::vec3 SceneRenderer2D::GetMesh3DScale(size_t i) const {
  if (i >= meshes3d.size())
    return glm::vec3(1.0f);
  return meshes3d[i].userScale;
}
const std::string &SceneRenderer2D::GetMesh3DPath(size_t i) const {
  static const std::string empty;
  if (i >= meshes3d.size())
    return empty;
  return meshes3d[i].path;
}
uint32_t SceneRenderer2D::GetMesh3DVertexCount(size_t i) const {
  if (i >= meshes3d.size())
    return 0;
  return meshes3d[i].vertexCount;
}
uint32_t SceneRenderer2D::GetMesh3DTriangleCount(size_t i) const {
  if (i >= meshes3d.size())
    return 0;
  return meshes3d[i].indexCount / 3;
}
void SceneRenderer2D::SetMesh3DTransform(size_t i, const glm::vec3 &position,
                                         const glm::vec3 &rotationEuler,
                                         const glm::vec3 &scale) {
  if (i >= meshes3d.size())
    return;
  meshes3d[i].userPosition = position;
  meshes3d[i].userRotation = rotationEuler;
  meshes3d[i].userScale = scale;
}

void SceneRenderer2D::DragMesh3DScreen(size_t i, float dxPx, float dyPx,
                                       int viewportHeight) {
  if (i >= meshes3d.size() || viewportHeight <= 0)
    return;
  // Build camera basis identical to the render path.
  float yawR = glm::radians(camera3d.yaw);
  float pitchR = glm::radians(camera3d.pitch);
  glm::vec3 forward;
  forward.x = std::cos(yawR) * std::cos(pitchR);
  forward.y = std::sin(pitchR);
  forward.z = std::sin(yawR) * std::cos(pitchR);
  forward = glm::normalize(forward);
  glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
  glm::vec3 up = glm::normalize(glm::cross(right, forward));

  glm::vec3 toObj = meshes3d[i].userPosition - camera3d.position;
  float depth = std::max(0.1f, glm::dot(toObj, forward));

  // World units per pixel at the object's depth: vertical FOV maps the
  // viewport height to 2*depth*tan(fov/2) world units.
  float worldPerPixel = 2.0f * depth *
                        std::tan(glm::radians(camera3d.fovDeg) * 0.5f) /
                        (float)viewportHeight;

  glm::vec3 move = right * (dxPx * worldPerPixel) - up * (dyPx * worldPerPixel);
  meshes3d[i].userPosition += move;
}

// ---------------------------------------------------------------------------
// 3D grid
// ---------------------------------------------------------------------------

void SceneRenderer2D::InitGridResources() {
  if (device == VK_NULL_HANDLE)
    return;

  // Build vertex buffer: lines along X and Z at unit spacing, spanning
  // [-10, 10]. Coordinate axes coloured brighter so origin is readable.
  struct V {
    glm::vec3 pos;
    glm::vec3 color;
  };
  std::vector<V> verts;
  const int half = 10;
  const float step = 1.0f;
  glm::vec3 colMinor(0.30f, 0.30f, 0.32f);
  glm::vec3 colMajor(0.55f, 0.55f, 0.58f);
  glm::vec3 colXAxis(0.85f, 0.25f, 0.25f);
  glm::vec3 colZAxis(0.25f, 0.55f, 0.95f);
  for (int i = -half; i <= half; ++i) {
    float f = (float)i * step;
    glm::vec3 c = (i == 0) ? colZAxis : (i % 5 == 0 ? colMajor : colMinor);
    verts.push_back({glm::vec3(f, 0.0f, -half * step), c});
    verts.push_back({glm::vec3(f, 0.0f, half * step), c});

    c = (i == 0) ? colXAxis : (i % 5 == 0 ? colMajor : colMinor);
    verts.push_back({glm::vec3(-half * step, 0.0f, f), c});
    verts.push_back({glm::vec3(half * step, 0.0f, f), c});
  }

  VkDeviceSize bytes = sizeof(V) * verts.size();
  VkBufferCreateInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bi.size = bytes;
  bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(device, &bi, nullptr, &grid3d.vertexBuffer) != VK_SUCCESS)
    throw std::runtime_error("failed to create grid vertex buffer");
  VkMemoryRequirements mr;
  vkGetBufferMemoryRequirements(device, grid3d.vertexBuffer, &mr);
  VkMemoryAllocateInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  ai.allocationSize = mr.size;
  ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (vkAllocateMemory(device, &ai, nullptr, &grid3d.vertexMemory) !=
      VK_SUCCESS)
    throw std::runtime_error("failed to allocate grid vertex memory");
  vkBindBufferMemory(device, grid3d.vertexBuffer, grid3d.vertexMemory, 0);
  void *dst = nullptr;
  vkMapMemory(device, grid3d.vertexMemory, 0, bytes, 0, &dst);
  std::memcpy(dst, verts.data(), (size_t)bytes);
  vkUnmapMemory(device, grid3d.vertexMemory);
  grid3d.vertexCount = (uint32_t)verts.size();

  // Pipeline
  VkShaderModule vert = LoadShader("assets/shaders/vulkan/scene_grid.vert.spv");
  VkShaderModule frag = LoadShader("assets/shaders/vulkan/scene_grid.frag.spv");

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag;
  stages[1].pName = "main";

  VkVertexInputBindingDescription binding{};
  binding.binding = 0;
  binding.stride = sizeof(V);
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription attrs[2]{};
  attrs[0].binding = 0;
  attrs[0].location = 0;
  attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attrs[0].offset = offsetof(V, pos);
  attrs[1].binding = 0;
  attrs[1].location = 1;
  attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attrs[1].offset = offsetof(V, color);

  VkPipelineVertexInputStateCreateInfo vi{};
  vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vi.vertexBindingDescriptionCount = 1;
  vi.pVertexBindingDescriptions = &binding;
  vi.vertexAttributeDescriptionCount = 2;
  vi.pVertexAttributeDescriptions = attrs;

  VkPipelineInputAssemblyStateCreateInfo ia{};
  ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  ia.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

  VkPipelineViewportStateCreateInfo vp{};
  vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vp.viewportCount = 1;
  vp.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rs{};
  rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo ms{};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState cba{};
  cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  cba.blendEnable = VK_FALSE;
  VkPipelineColorBlendStateCreateInfo cb{};
  cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  cb.attachmentCount = 1;
  cb.pAttachments = &cba;

  VkPipelineDepthStencilStateCreateInfo ds{};
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  ds.depthTestEnable = VK_TRUE;
  // Don't write depth so the grid never occludes the mesh (or vice
  // versa) regardless of draw order.
  ds.depthWriteEnable = VK_FALSE;
  ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

  std::vector<VkDynamicState> dyn = {VK_DYNAMIC_STATE_VIEWPORT,
                                     VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynState{};
  dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynState.dynamicStateCount = (uint32_t)dyn.size();
  dynState.pDynamicStates = dyn.data();

  VkPushConstantRange pc{};
  pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  pc.offset = 0;
  pc.size = sizeof(glm::mat4);

  VkPipelineLayoutCreateInfo li{};
  li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  li.pushConstantRangeCount = 1;
  li.pPushConstantRanges = &pc;
  if (vkCreatePipelineLayout(device, &li, nullptr, &grid3d.layout) !=
      VK_SUCCESS)
    throw std::runtime_error("failed to create grid pipeline layout");

  VkGraphicsPipelineCreateInfo pi{};
  pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pi.stageCount = 2;
  pi.pStages = stages;
  pi.pVertexInputState = &vi;
  pi.pInputAssemblyState = &ia;
  pi.pViewportState = &vp;
  pi.pRasterizationState = &rs;
  pi.pMultisampleState = &ms;
  pi.pColorBlendState = &cb;
  pi.pDepthStencilState = &ds;
  pi.pDynamicState = &dynState;
  pi.layout = grid3d.layout;
  pi.renderPass = offscreen.renderPass;
  pi.subpass = 0;
  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr,
                                &grid3d.pipeline) != VK_SUCCESS)
    throw std::runtime_error("failed to create grid pipeline");

  vkDestroyShaderModule(device, vert, nullptr);
  vkDestroyShaderModule(device, frag, nullptr);
}

void SceneRenderer2D::DrawGrid3D(VkCommandBuffer cmd,
                                 const glm::mat4 &viewProj) {
  if (grid3d.pipeline == VK_NULL_HANDLE || grid3d.vertexCount == 0)
    return;
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, grid3d.pipeline);
  vkCmdPushConstants(cmd, grid3d.layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                     sizeof(glm::mat4), glm::value_ptr(viewProj));
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(cmd, 0, 1, &grid3d.vertexBuffer, offsets);
  vkCmdDraw(cmd, grid3d.vertexCount, 1, 0, 0);
}

// ---------------------------------------------------------------------------
// Procedural primitives
// ---------------------------------------------------------------------------

bool SceneRenderer2D::LoadCube(const std::string &name, float size) {
  if (device == VK_NULL_HANDLE)
    return false;
  std::vector<MeshVertex> verts;
  std::vector<uint32_t> idx;
  verts.reserve(36);
  idx.reserve(36);
  float s = size * 0.5f;

  auto tri = [&](const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c) {
    glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
    uint32_t base = (uint32_t)verts.size();
    verts.push_back({a, n});
    verts.push_back({b, n});
    verts.push_back({c, n});
    idx.push_back(base);
    idx.push_back(base + 1);
    idx.push_back(base + 2);
  };

  // 6 faces, each as 2 triangles. Build flat-shaded so normals are
  // exact per-face (no shared verts across faces).
  glm::vec3 v[8] = {{-s, -s, -s}, {s, -s, -s}, {s, s, -s}, {-s, s, -s},
                    {-s, -s, s},  {s, -s, s},  {s, s, s},  {-s, s, s}};
  // -Z
  tri(v[0], v[2], v[1]);
  tri(v[0], v[3], v[2]);
  // +Z
  tri(v[4], v[5], v[6]);
  tri(v[4], v[6], v[7]);
  // -X
  tri(v[0], v[7], v[3]);
  tri(v[0], v[4], v[7]);
  // +X
  tri(v[1], v[2], v[6]);
  tri(v[1], v[6], v[5]);
  // -Y
  tri(v[0], v[1], v[5]);
  tri(v[0], v[5], v[4]);
  // +Y
  tri(v[3], v[7], v[6]);
  tri(v[3], v[6], v[2]);

  Mesh3D mesh;
  mesh.path = "<primitive:cube>";
  mesh.displayName = name;
  mesh.aabbMin = glm::vec3(-s);
  mesh.aabbMax = glm::vec3(s);
  mesh.autoFit = glm::mat4(1.0f);
  if (!UploadMeshBuffers(mesh, verts, idx)) {
    DestroyMesh(mesh);
    return false;
  }
  meshes3d.push_back(std::move(mesh));
  cout << "[Prim] Cube '" << name << "' (verts " << verts.size() << ", tris "
       << idx.size() / 3 << ")" << endl;
  return true;
}

bool SceneRenderer2D::LoadSphere(const std::string &name, float radius,
                                 int segments, int rings) {
  if (device == VK_NULL_HANDLE)
    return false;
  if (segments < 3)
    segments = 3;
  if (rings < 2)
    rings = 2;

  std::vector<MeshVertex> verts;
  std::vector<uint32_t> idx;
  verts.reserve((rings + 1) * (segments + 1));
  // UV-sphere: parametric (theta, phi). Normals point outward = vert / r.
  for (int ring = 0; ring <= rings; ++ring) {
    float v = (float)ring / (float)rings;
    float phi = v * 3.14159265358979f;
    float y = std::cos(phi);
    float r = std::sin(phi);
    for (int seg = 0; seg <= segments; ++seg) {
      float u = (float)seg / (float)segments;
      float theta = u * 2.0f * 3.14159265358979f;
      glm::vec3 p(r * std::cos(theta), y, r * std::sin(theta));
      glm::vec3 normal = p; // unit-length already
      verts.push_back({p * radius, normal});
    }
  }
  int stride = segments + 1;
  for (int ring = 0; ring < rings; ++ring) {
    for (int seg = 0; seg < segments; ++seg) {
      uint32_t a = (uint32_t)(ring * stride + seg);
      uint32_t b = (uint32_t)(ring * stride + seg + 1);
      uint32_t c = (uint32_t)((ring + 1) * stride + seg);
      uint32_t d = (uint32_t)((ring + 1) * stride + seg + 1);
      idx.push_back(a);
      idx.push_back(c);
      idx.push_back(b);
      idx.push_back(b);
      idx.push_back(c);
      idx.push_back(d);
    }
  }

  Mesh3D mesh;
  mesh.path = "<primitive:sphere>";
  mesh.displayName = name;
  mesh.aabbMin = glm::vec3(-radius);
  mesh.aabbMax = glm::vec3(radius);
  mesh.autoFit = glm::mat4(1.0f);
  if (!UploadMeshBuffers(mesh, verts, idx)) {
    DestroyMesh(mesh);
    return false;
  }
  meshes3d.push_back(std::move(mesh));
  cout << "[Prim] Sphere '" << name << "' (verts " << verts.size() << ", tris "
       << idx.size() / 3 << ")" << endl;
  return true;
}

bool SceneRenderer2D::LoadPlane(const std::string &name, float size) {
  if (device == VK_NULL_HANDLE)
    return false;
  std::vector<MeshVertex> verts;
  std::vector<uint32_t> idx;
  float s = size * 0.5f;
  glm::vec3 n(0.0f, 1.0f, 0.0f);
  verts.push_back({{-s, 0.0f, -s}, n});
  verts.push_back({{s, 0.0f, -s}, n});
  verts.push_back({{s, 0.0f, s}, n});
  verts.push_back({{-s, 0.0f, s}, n});
  idx = {0, 2, 1, 0, 3, 2};
  Mesh3D mesh;
  mesh.path = "<primitive:plane>";
  mesh.displayName = name;
  mesh.aabbMin = glm::vec3(-s, 0.0f, -s);
  mesh.aabbMax = glm::vec3(s, 0.0f, s);
  mesh.autoFit = glm::mat4(1.0f);
  if (!UploadMeshBuffers(mesh, verts, idx)) {
    DestroyMesh(mesh);
    return false;
  }
  meshes3d.push_back(std::move(mesh));
  return true;
}

void SceneRenderer2D::ClearMeshes3D() {
  if (device != VK_NULL_HANDLE)
    vkDeviceWaitIdle(device);
  for (auto &m : meshes3d)
    DestroyMesh(m);
  meshes3d.clear();
}

const std::string &SceneRenderer2D::GetMesh3DName(size_t i) const {
  static const std::string empty;
  if (i >= meshes3d.size())
    return empty;
  return meshes3d[i].displayName;
}

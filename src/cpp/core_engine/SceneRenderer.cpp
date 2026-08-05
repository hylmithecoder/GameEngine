#include "../../../include/core_engine/SceneRenderer.hpp"
#include "../../../include/core_engine/Debugger.hpp"
#include "../../../include/core_engine/PMXLoader.hpp"
#include <VkTools.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui_impl_vulkan.h>
#include <iostream>
#include <sstream>
#include <string>
#include <ufbx.h>
#include <unordered_map>
#include <vector>

using namespace Debug;
using namespace std;

SceneRenderer::SceneRenderer(int width, int height)
    : width(width), height(height), cameraZoom(1.0f), gridVisible(true),
      gridSize(50.0f), snapToGrid(false), cameraPosition(0.0f, 0.0f) {
  cout << "Creating SceneRenderer (Vulkan): " << width << "x" << height << endl;
}

SceneRenderer::~SceneRenderer() {
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
    if (meshTextureSetLayout != VK_NULL_HANDLE)
      vkDestroyDescriptorSetLayout(device, meshTextureSetLayout, nullptr);
    if (whiteView != VK_NULL_HANDLE)
      vkDestroyImageView(device, whiteView, nullptr);
    if (whiteImage != VK_NULL_HANDLE)
      vkDestroyImage(device, whiteImage, nullptr);
    if (whiteMemory != VK_NULL_HANDLE)
      vkFreeMemory(device, whiteMemory, nullptr);
    if (grid3d.pipeline != VK_NULL_HANDLE)
      vkDestroyPipeline(device, grid3d.pipeline, nullptr);
    if (grid3d.layout != VK_NULL_HANDLE)
      vkDestroyPipelineLayout(device, grid3d.layout, nullptr);
    if (grid3d.vertexBuffer != VK_NULL_HANDLE)
      vkDestroyBuffer(device, grid3d.vertexBuffer, nullptr);
    if (grid3d.vertexMemory != VK_NULL_HANDLE)
      vkFreeMemory(device, grid3d.vertexMemory, nullptr);
    if (sun.pipeline != VK_NULL_HANDLE)
      vkDestroyPipeline(device, sun.pipeline, nullptr);
    if (sun.layout != VK_NULL_HANDLE)
      vkDestroyPipelineLayout(device, sun.layout, nullptr);
    if (sun.vertexBuffer != VK_NULL_HANDLE)
      vkDestroyBuffer(device, sun.vertexBuffer, nullptr);
    if (sun.vertexMemory != VK_NULL_HANDLE)
      vkFreeMemory(device, sun.vertexMemory, nullptr);
    if (sun.indexBuffer != VK_NULL_HANDLE)
      vkDestroyBuffer(device, sun.indexBuffer, nullptr);
    if (sun.indexMemory != VK_NULL_HANDLE)
      vkFreeMemory(device, sun.indexMemory, nullptr);
    if (gizmoVertexBuffer != VK_NULL_HANDLE)
      vkDestroyBuffer(device, gizmoVertexBuffer, nullptr);
    if (gizmoVertexMemory != VK_NULL_HANDLE)
      vkFreeMemory(device, gizmoVertexMemory, nullptr);
    DestroyPreviewResources();

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

void SceneRenderer::SetVulkanContext(VkDevice device,
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

void SceneRenderer::InitVulkanResources() {
  if (device == VK_NULL_HANDLE)
    return;

  CreateOffscreenResources();
  InitPipelines();
  // Player-camera preview target (own render pass; fixed size).
  CreatePreviewResources();

  // Create Quad Vertex Buffer (for sprites)
  struct Vertex {
    glm::vec2 pos;
    glm::vec2 uv;
  };

  // UVs: V is flipped so the texture renders upright in the final
  // ImGui-displayed image. Why: stbi loads row 0 = image top, Vulkan
  // NDC has +Y pointing down, the renderer uses an unflipped GL-style
  // ortho, and ImGui then samples the framebuffer with V going 1→0.
  // The net of all three flips inverts the texture unless we pre-flip
  // V on the quad. Top of quad (+Y) → V=0 = image top.
  std::vector<Vertex> vertices = {{{-0.5f, -0.5f}, {0.0f, 1.0f}},
                                  {{0.5f, -0.5f}, {1.0f, 1.0f}},
                                  {{0.5f, 0.5f}, {1.0f, 0.0f}},
                                  {{-0.5f, 0.5f}, {0.0f, 0.0f}}};

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

  // Hand the TextureManager the same Vulkan handles so 2D sprite loads
  // can allocate buffers/images. Without this, GetTextureDescriptor()
  // calls vkCreateBuffer with a null device → SIGABRT inside the loader.
  // offscreen.sampler is already created by CreateOffscreenResources()
  // above; we reuse it as the default sprite sampler.
  textureManager.SetVulkanContext(device, physicalDevice, graphicsQueue,
                                  commandPool, descriptorPool,
                                  offscreen.sampler);

  // 1x1 white fallback for untextured submeshes. Safe here: the ImGui
  // Vulkan backend is already initialized (CreateOffscreenResources above
  // registers its own descriptor the same way).
  InitWhiteTexture();
}

void SceneRenderer::SetViewportSize(int newWidth, int newHeight) {
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

void SceneRenderer::CreateOffscreenResources() {
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

void SceneRenderer::DestroyOffscreenResources() {
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

uint32_t SceneRenderer::findMemoryType(uint32_t typeFilter,
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

void SceneRenderer::RenderSceneToTexture(const Scene &scene) {
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
    glm::vec3 forward = GetCameraForward();
    glm::mat4 view3d =
        glm::lookAt(camera3d.position, camera3d.position + forward,
                    glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj3d = glm::perspective(glm::radians(camera3d.fovDeg), aspect,
                                        camera3d.nearPlane, camera3d.farPlane);
    // No Y-flip on projection: the Scene panel already flips the sampled
    // image via ImGui UVs (V from 1→0). Editor view draws gizmos.
    RecordWorld(cmd, view3d, proj3d, /*drawGizmos=*/true);
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

VkDescriptorSet SceneRenderer::GetViewportDescriptorSet() const {
  return offscreen.descriptorSet;
}

void SceneRenderer::InitPipelines() {
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
  InitSunResources();
  InitGizmoPipeline();
}

void SceneRenderer::InitTestTrianglePipeline() {
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

void SceneRenderer::DrawTestTriangle(VkCommandBuffer cmd) {
  if (testTrianglePipeline == VK_NULL_HANDLE)
    return;
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, testTrianglePipeline);
  vkCmdDraw(cmd, 3, 1, 0, 0);
}

void SceneRenderer::DrawGrid(VkCommandBuffer cmd, const glm::mat4 &projection,
                             const glm::mat4 &view) {
  if (gridPipeline == VK_NULL_HANDLE)
    return;
}

void SceneRenderer::DrawSprite(VkCommandBuffer cmd,
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

void SceneRenderer::DrawSelectionGizmo(const GameObject &obj) {}

void SceneRenderer::SetEditMode(EditMode mode) { currentMode = mode; }
void SceneRenderer::SetGridVisible(bool visible) { gridVisible = visible; }
void SceneRenderer::SetGridSize(float size) { gridSize = size; }
void SceneRenderer::SetSnapToGrid(bool snap) { snapToGrid = snap; }

void SceneRenderer::ResetCamera() {
  cameraPosition = glm::vec2(0.0f, 0.0f);
  cameraZoom = 1.0f;
}

void SceneRenderer::SetCameraZoom(float zoom) {
  this->cameraZoom = std::max(0.1f, std::min(zoom, 10.0f));
}

void SceneRenderer::SetGridColor(float r, float g, float b, float a) {
  gridColor = ImVec4(r, g, b, a);
}

void SceneRenderer::SetBackgroundColor(float r, float g, float b, float a) {
  bgColor = ImVec4(r, g, b, a);
}

glm::vec2 SceneRenderer::ViewportToWorldPosition(float viewX,
                                                 float viewY) const {
  float worldX = (viewX - width * 0.5f) / cameraZoom + cameraPosition.x;
  float worldY = (viewY - height * 0.5f) / cameraZoom + cameraPosition.y;
  return glm::vec2(worldX, worldY);
}

glm::vec2 SceneRenderer::WorldToViewportPosition(float worldX,
                                                 float worldY) const {
  float viewX = (worldX - cameraPosition.x) * cameraZoom + width * 0.5f;
  float viewY = (worldY - cameraPosition.y) * cameraZoom + height * 0.5f;
  return glm::vec2(viewX, viewY);
}

void SceneRenderer::HandleClick(float worldX, float worldY) {
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

void SceneRenderer::HandleDrag(float deltaX, float deltaY) {
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

void SceneRenderer::HandleZoom(float delta) {
  float zoomFactor = 0.1f;
  cameraZoom =
      std::max(0.1f, std::min(cameraZoom * (1.0f + delta * zoomFactor), 5.0f));
}

void SceneRenderer::MoveSelected(float deltaX, float deltaY) {
  if (selectedObject) {
    selectedObject->x += deltaX;
    selectedObject->y += deltaY;
  }
}

void SceneRenderer::DeleteSelected() {
  if (selectedObjectIndex >= 0 &&
      selectedObjectIndex < (int)currentScene.objects.size()) {
    currentScene.objects.erase(currentScene.objects.begin() +
                               selectedObjectIndex);
    selectedObject = nullptr;
    selectedObjectIndex = -1;
  }
}

bool SceneRenderer::HasSelectedObject() const {
  return selectedObject != nullptr;
}

VkShaderModule SceneRenderer::LoadShader(const std::string &path) {
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

void SceneRenderer::InitMeshPipeline() {
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

  VkVertexInputAttributeDescription attrs[3]{};
  attrs[0].binding = 0;
  attrs[0].location = 0;
  attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attrs[0].offset = offsetof(MeshVertex, pos);
  attrs[1].binding = 0;
  attrs[1].location = 1;
  attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attrs[1].offset = offsetof(MeshVertex, normal);
  attrs[2].binding = 0;
  attrs[2].location = 2;
  attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
  attrs[2].offset = offsetof(MeshVertex, uv);

  VkPipelineVertexInputStateCreateInfo vi{};
  vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vi.vertexBindingDescriptionCount = 1;
  vi.pVertexBindingDescriptions = &binding;
  vi.vertexAttributeDescriptionCount = 3;
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

  // Descriptor set 0: one combined image sampler (the albedo texture).
  // Defined identically to ImGui's layout so TextureManager's descriptors
  // (allocated via ImGui_ImplVulkan_AddTexture) are layout-compatible here.
  if (meshTextureSetLayout == VK_NULL_HANDLE) {
    VkDescriptorSetLayoutBinding texBinding{};
    texBinding.binding = 0;
    texBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texBinding.descriptorCount = 1;
    texBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dslInfo{};
    dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslInfo.bindingCount = 1;
    dslInfo.pBindings = &texBinding;
    if (vkCreateDescriptorSetLayout(device, &dslInfo, nullptr,
                                    &meshTextureSetLayout) != VK_SUCCESS) {
      throw std::runtime_error("failed to create mesh texture set layout");
    }
  }

  VkPushConstantRange pc{};
  pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pc.offset = 0;
  pc.size = sizeof(glm::mat4) * 2 +
            sizeof(glm::vec4) * 5; // mvp + model + lightPosOrDir +
                                   // lightColorType + lightDir + lightParams +
                                   // material (rgb diffuse, w = hasTexture)

  VkPipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &meshTextureSetLayout;
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

void SceneRenderer::InitWhiteTexture() {
  if (device == VK_NULL_HANDLE || whiteDescriptor != VK_NULL_HANDLE)
    return;

  const uint32_t pixel = 0xFFFFFFFFu; // RGBA8 white
  const VkDeviceSize imageSize = 4;

  // Staging buffer
  VkBuffer staging = VK_NULL_HANDLE;
  VkDeviceMemory stagingMem = VK_NULL_HANDLE;
  VkBufferCreateInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bi.size = imageSize;
  bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  vkCreateBuffer(device, &bi, nullptr, &staging);
  VkMemoryRequirements mr;
  vkGetBufferMemoryRequirements(device, staging, &mr);
  VkMemoryAllocateInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  ai.allocationSize = mr.size;
  ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  vkAllocateMemory(device, &ai, nullptr, &stagingMem);
  vkBindBufferMemory(device, staging, stagingMem, 0);
  void *map = nullptr;
  vkMapMemory(device, stagingMem, 0, imageSize, 0, &map);
  std::memcpy(map, &pixel, (size_t)imageSize);
  vkUnmapMemory(device, stagingMem);

  // Image
  VkImageCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.extent = {1, 1, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.format = VK_FORMAT_R8G8B8A8_UNORM;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  vkCreateImage(device, &ici, nullptr, &whiteImage);
  vkGetImageMemoryRequirements(device, whiteImage, &mr);
  ai.allocationSize = mr.size;
  ai.memoryTypeIndex =
      findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  vkAllocateMemory(device, &ai, nullptr, &whiteMemory);
  vkBindImageMemory(device, whiteImage, whiteMemory, 0);

  // Upload (same UNDEFINED->TRANSFER_DST->SHADER_READ dance as TextureManager)
  VkCommandBufferAllocateInfo cba{};
  cba.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cba.commandPool = commandPool;
  cba.commandBufferCount = 1;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  vkAllocateCommandBuffers(device, &cba, &cmd);
  VkCommandBufferBeginInfo cbi{};
  cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &cbi);
  VkImageMemoryBarrier toDst{};
  toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toDst.image = whiteImage;
  toDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  toDst.subresourceRange.levelCount = 1;
  toDst.subresourceRange.layerCount = 1;
  toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &toDst);
  VkBufferImageCopy region{};
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.layerCount = 1;
  region.imageExtent = {1, 1, 1};
  vkCmdCopyBufferToImage(cmd, staging, whiteImage,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  VkImageMemoryBarrier toShader = toDst;
  toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &toShader);
  vkEndCommandBuffer(cmd);
  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;
  vkQueueSubmit(graphicsQueue, 1, &submit, VK_NULL_HANDLE);
  vkQueueWaitIdle(graphicsQueue);
  vkFreeCommandBuffers(device, commandPool, 1, &cmd);

  VkImageViewCreateInfo vci{};
  vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  vci.image = whiteImage;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = VK_FORMAT_R8G8B8A8_UNORM;
  vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  vci.subresourceRange.levelCount = 1;
  vci.subresourceRange.layerCount = 1;
  vkCreateImageView(device, &vci, nullptr, &whiteView);

  whiteDescriptor = ImGui_ImplVulkan_AddTexture(
      offscreen.sampler, whiteView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  vkDestroyBuffer(device, staging, nullptr);
  vkFreeMemory(device, stagingMem, nullptr);
}

VkDescriptorSet
SceneRenderer::ResolveTextureDescriptor(const std::string &path) {
  if (path.empty())
    return whiteDescriptor;
  VkDescriptorSet ds = textureManager.GetTextureDescriptor(path);
  return ds != VK_NULL_HANDLE ? ds : whiteDescriptor;
}

void SceneRenderer::DrawMeshes(VkCommandBuffer cmd, const glm::mat4 &viewProj) {
  if (meshPipeline == VK_NULL_HANDLE)
    return;
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline);

  // 1. Scan for the first active Light GameObject in the scene
  const Mesh3D *lightMesh = nullptr;
  for (const auto &m : meshes3d) {
    if (m.isLight) {
      lightMesh = &m;
      break;
    }
  }

  // 2. Prepare the light parameters (fallback to global sun if no light object)
  glm::vec3 posOrDir{0.0f};
  glm::vec3 lightColor{1.0f, 0.96f, 0.88f};
  glm::vec3 lightDir{0.0f, 0.0f, -1.0f}; // spotlight forward vector
  float intensity = 0.90f;
  int type = 0; // 0 = Directional, 1 = Point, 2 = Spotlight
  float range = 10.0f;
  float spotAngleRad = glm::radians(15.0f);
  float spotCosOuter = std::cos(glm::radians(15.0f));
  float gamma = 1.05f;

  if (lightMesh) {
    lightColor = lightMesh->lightColor;
    intensity = lightMesh->lightIntensity;
    type = lightMesh->lightType;
    range = lightMesh->lightRange;
    spotAngleRad = glm::radians(lightMesh->lightSpotAngle * 0.5f);
    spotCosOuter = std::cos(glm::radians(lightMesh->lightSpotAngle * 0.5f));
    gamma = lightMesh->lightGamma;

    // Compute light forward direction from Euler rotation (degrees)
    float pitch = glm::radians(lightMesh->userRotation.x);
    float yaw = glm::radians(lightMesh->userRotation.y - 90.0f);
    glm::vec3 forward;
    forward.x = std::cos(yaw) * std::cos(pitch);
    forward.y = std::sin(pitch);
    forward.z = std::sin(yaw) * std::cos(pitch);
    lightDir = glm::normalize(forward);

    if (type == 0) { // Directional
      // Shader expects direction pointing TOWARD the light source
      posOrDir = -lightDir;
    } else { // Point or Spotlight
      posOrDir = lightMesh->userPosition;
    }
  } else {
    // Fallback global directional light
    posOrDir = glm::normalize(sunLight.direction);
    intensity = sunLight.intensity;
    lightColor = sunLight.color;
  }

  // 3. Render all meshes in the scene
  for (const auto &m : meshes3d) {
    if (m.indexCount == 0)
      continue;
    glm::mat4 model = m.ComputeModel();
    glm::mat4 mvp = viewProj * model;

    struct PC {
      glm::mat4 mvp;
      glm::mat4 model;
      glm::vec4 lightPosOrDir;  // xyz = pos/dir, w = intensity
      glm::vec4 lightColorType; // xyz = color, w = type (special -1.0 =
                                // emissive/unlit)
      glm::vec4 lightDir;       // xyz = spotlight forward vector, w = unused
      glm::vec4 lightParams;    // x = range, y = spotAngleRad, z = gamma, w =
                                // spotCosOuter
      glm::vec4 material;       // xyz = diffuse tint, w = hasTexture
    } pc;

    pc.mvp = mvp;
    pc.model = model;

    if (m.isLight) {
      // Draw the light source itself as an unlit solid sphere matching its
      // light color
      pc.lightPosOrDir = glm::vec4(0.0f);
      pc.lightColorType =
          glm::vec4(m.lightColor, -1.0f); // -1.0 signals unlit emissive shading
      pc.lightDir = glm::vec4(0.0f);
      pc.lightParams = glm::vec4(0.0f);
    } else {
      pc.lightPosOrDir = glm::vec4(posOrDir, intensity);
      pc.lightColorType = glm::vec4(lightColor, (float)type);
      pc.lightDir = glm::vec4(lightDir, 0.0f);
      pc.lightParams = glm::vec4(range, spotAngleRad, gamma, spotCosOuter);
    }

    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &m.vertexBuffer, offsets);
    vkCmdBindIndexBuffer(cmd, m.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    auto drawRange = [&](uint32_t indexOffset, uint32_t indexCount,
                         const glm::vec3 &diffuse, bool hasTex,
                         VkDescriptorSet tex) {
      pc.material = glm::vec4(diffuse, hasTex ? 1.0f : 0.0f);
      VkDescriptorSet ds = (tex != VK_NULL_HANDLE) ? tex : whiteDescriptor;
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              meshPipelineLayout, 0, 1, &ds, 0, nullptr);
      vkCmdPushConstants(cmd, meshPipelineLayout,
                         VK_SHADER_STAGE_VERTEX_BIT |
                             VK_SHADER_STAGE_FRAGMENT_BIT,
                         0, sizeof(PC), &pc);
      vkCmdDrawIndexed(cmd, indexCount, 1, indexOffset, 0, 0);
    };

    // One draw per surface (submesh) so each binds its own texture. Older
    // meshes with no submeshes fall back to a single untextured draw.
    if (m.submeshes.empty()) {
      drawRange(0, m.indexCount, glm::vec3(0.84f, 0.80f, 0.74f), false,
                whiteDescriptor);
    } else {
      for (const auto &sm : m.submeshes) {
        if (sm.indexCount == 0)
          continue;
        drawRange(sm.indexOffset, sm.indexCount, sm.diffuse, sm.hasTexture,
                  sm.textureDescriptor);
      }
    }
  }
}

bool SceneRenderer::UploadMeshBuffers(Mesh3D &mesh,
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

  // Default surface + pickable CPU geometry for callers that didn't set them
  // (primitives). PMX/OBJ loaders populate these before calling, so the
  // guards below leave their richer data intact.
  if (mesh.submeshes.empty()) {
    SubMesh sm;
    sm.indexCount = (uint32_t)indices.size();
    sm.name = "Surface";
    mesh.submeshes.push_back(std::move(sm));
  }
  if (mesh.cpuPositions.empty()) {
    mesh.cpuPositions.resize(verts.size());
    for (size_t i = 0; i < verts.size(); ++i)
      mesh.cpuPositions[i] = verts[i].pos;
    mesh.cpuIndices = indices;
  }
  return true;
}

void SceneRenderer::DestroyMesh(Mesh3D &mesh) {
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
  // Texture descriptors are owned by TextureManager (or the white fallback),
  // so we only drop our references here, never destroy them.
  mesh.submeshes.clear();
  mesh.cpuPositions.clear();
  mesh.cpuIndices.clear();
}

namespace {
// Dedup key over (position, texcoord, normal) indices so distinct UVs at a
// shared position produce distinct vertices.
struct VNKey {
  int32_t v;
  int32_t t;
  int32_t n;
  bool operator==(const VNKey &o) const noexcept {
    return v == o.v && t == o.t && n == o.n;
  }
};
struct VNKeyHash {
  size_t operator()(const VNKey &k) const noexcept {
    uint64_t h = 1469598103934665603ull; // FNV-1a over the three indices
    for (int32_t x : {k.v, k.t, k.n}) {
      h ^= (uint64_t)(uint32_t)x;
      h *= 1099511628211ull;
    }
    return (size_t)h;
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

bool SceneRenderer::LoadObjMesh(const std::string &path) {
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
  std::vector<glm::vec2> texcoords;
  positions.reserve(50000);
  normals.reserve(50000);
  texcoords.reserve(50000);

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
      } else if (line[1] == 't' && line.size() > 2 && line[2] == ' ') {
        glm::vec2 t;
        if (std::sscanf(line.c_str() + 3, "%f %f", &t.x, &t.y) >= 2) {
          texcoords.push_back(t);
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
        int tIndex =
            (ti > 0) ? (ti - 1) : ((ti < 0) ? (int)texcoords.size() + ti : -1);
        if (vIndex < 0 || vIndex >= (int)positions.size())
          break;
        VNKey k{vIndex, tIndex, nIndex};
        uint32_t outIdx;
        auto it = dedup.find(k);
        if (it == dedup.end()) {
          MeshVertex mv;
          mv.pos = positions[vIndex];
          if (nIndex >= 0 && nIndex < (int)normals.size())
            mv.normal = normals[nIndex];
          else
            mv.normal = glm::vec3(0.0f, 1.0f, 0.0f);
          if (tIndex >= 0 && tIndex < (int)texcoords.size()) {
            // OBJ uses a bottom-left UV origin; flip V for Vulkan's top-left.
            mv.uv = glm::vec2(texcoords[tIndex].x, 1.0f - texcoords[tIndex].y);
          }
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

  // OBJ has no material grouping here yet — one surface over the whole mesh.
  {
    SubMesh sm;
    sm.indexCount = (uint32_t)outIndices.size();
    sm.name = "Surface";
    mesh.submeshes.push_back(std::move(sm));
  }
  mesh.cpuPositions.resize(outVerts.size());
  for (size_t i = 0; i < outVerts.size(); ++i)
    mesh.cpuPositions[i] = outVerts[i].pos;
  mesh.cpuIndices = outIndices;

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

// ---------------------------------------------------------------------------
// PMX (MikuMikuDance) model loading
// ---------------------------------------------------------------------------

bool SceneRenderer::LoadPMXMesh(const std::string &path) {
  if (device == VK_NULL_HANDLE) {
    cout << "[PMX] Vulkan context not ready, deferring load: " << path << endl;
    return false;
  }

  pmx::PMXModel pmxModel;
  if (!pmx::LoadPMX(path, pmxModel)) {
    cout << "[PMX] Failed to parse: " << path << endl;
    return false;
  }

  if (pmxModel.vertices.empty() || pmxModel.indices.empty()) {
    cout << "[PMX] No geometry in: " << path << endl;
    return false;
  }

  // Convert PMX vertices to engine MeshVertex (pos + normal + uv).
  // PMX uses a left-handed coordinate system (Y-up, Z-forward);
  // our engine is right-handed (Y-up, Z-toward-viewer). We negate Z
  // to convert, and also reverse winding order. PMX UVs already use the
  // DirectX/Vulkan top-left origin, so they are kept as-is.
  std::vector<MeshVertex> outVerts(pmxModel.vertices.size());
  glm::vec3 mn(std::numeric_limits<float>::max());
  glm::vec3 mx(-std::numeric_limits<float>::max());

  for (size_t i = 0; i < pmxModel.vertices.size(); ++i) {
    const auto &pv = pmxModel.vertices[i];
    MeshVertex &mv = outVerts[i];
    // Convert left-handed → right-handed: negate Z
    mv.pos = glm::vec3(pv.position.x, pv.position.y, -pv.position.z);
    mv.normal = glm::vec3(pv.normal.x, pv.normal.y, -pv.normal.z);
    mv.uv = pv.uv;
    mn = glm::min(mn, mv.pos);
    mx = glm::max(mx, mv.pos);
  }

  // Reverse winding order (CW → CCW) after Z-negate
  std::vector<uint32_t> outIndices(pmxModel.indices.size());
  for (size_t i = 0; i + 2 < pmxModel.indices.size(); i += 3) {
    outIndices[i + 0] = pmxModel.indices[i + 0];
    outIndices[i + 1] = pmxModel.indices[i + 2]; // swap 1 and 2
    outIndices[i + 2] = pmxModel.indices[i + 1];
  }

  Mesh3D mesh;
  mesh.path = path;
  mesh.aabbMin = mn;
  mesh.aabbMax = mx;
  mesh.vertexCount = (uint32_t)outVerts.size();
  mesh.bones = std::move(pmxModel.bones);

  // Auto-fit: center + scale to ~1.5 units
  glm::vec3 center = (mn + mx) * 0.5f;
  glm::vec3 extent = mx - mn;
  float longest = std::max(extent.x, std::max(extent.y, extent.z));
  float fitScale = (longest > 1e-6f) ? (1.5f / longest) : 1.0f;
  glm::mat4 M(1.0f);
  M = glm::scale(M, glm::vec3(fitScale));
  M = glm::translate(M, -center);
  mesh.autoFit = M;

  // Build per-material surface ranges ("surfaces" the user can re-texture)
  // and auto-load each material's diffuse texture from the .pmx directory.
  std::string pmxDir;
  {
    size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos)
      pmxDir = path.substr(0, slash);
  }
  auto resolvePmxTexture = [&](int texIndex) -> std::string {
    if (texIndex < 0 || texIndex >= (int)pmxModel.texturePaths.size())
      return "";
    std::string rel = pmxModel.texturePaths[texIndex];
    // PMX stores Windows-style backslash paths; normalize for POSIX.
    for (char &c : rel)
      if (c == '\\')
        c = '/';
    if (rel.empty())
      return "";
    return pmxDir.empty() ? rel : (pmxDir + "/" + rel);
  };

  const uint32_t totalIndices = (uint32_t)outIndices.size();
  uint32_t runningOffset = 0;
  int loadedTextures = 0;
  for (const auto &mat : pmxModel.materials) {
    if (mat.indexCount <= 0)
      continue;
    SubMesh sm;
    sm.indexOffset = runningOffset;
    sm.indexCount = (uint32_t)mat.indexCount;
    runningOffset += (uint32_t)mat.indexCount;
    // Clamp draw range to the actual buffer (defensive against malformed
    // files).
    if (sm.indexOffset >= totalIndices) {
      continue;
    }
    if (sm.indexOffset + sm.indexCount > totalIndices)
      sm.indexCount = totalIndices - sm.indexOffset;
    sm.name = !mat.nameEN.empty() ? mat.nameEN : mat.nameJP;
    if (sm.name.empty())
      sm.name = "Material " + std::to_string(mesh.submeshes.size());
    sm.diffuse = glm::vec3(mat.diffuse);
    std::string texPath = resolvePmxTexture(mat.textureIndex);
    if (!texPath.empty()) {
      VkDescriptorSet ds = textureManager.GetTextureDescriptor(texPath);
      if (ds != VK_NULL_HANDLE) {
        sm.texturePath = texPath;
        sm.textureDescriptor = ds;
        sm.hasTexture = true;
        ++loadedTextures;
      } else {
        cout << "[PMX] Texture not loaded (using diffuse color): " << texPath
             << endl;
      }
    }
    mesh.submeshes.push_back(std::move(sm));
  }
  // Fallback: no usable materials → expose the whole buffer as one surface.
  if (mesh.submeshes.empty()) {
    SubMesh sm;
    sm.indexCount = totalIndices;
    sm.name = "Surface";
    mesh.submeshes.push_back(std::move(sm));
  }

  // CPU copy of geometry for click-picking (indices match the GPU buffer).
  mesh.cpuPositions.resize(outVerts.size());
  for (size_t i = 0; i < outVerts.size(); ++i)
    mesh.cpuPositions[i] = outVerts[i].pos;
  mesh.cpuIndices = outIndices;

  if (!UploadMeshBuffers(mesh, outVerts, outIndices)) {
    cout << "[PMX] Failed to upload mesh buffers" << endl;
    DestroyMesh(mesh);
    return false;
  }

  cout << "[PMX] Loaded " << path << " — verts: " << outVerts.size()
       << ", tris: " << (outIndices.size() / 3)
       << ", bones: " << mesh.bones.size()
       << ", surfaces: " << mesh.submeshes.size() << " (" << loadedTextures
       << " textured)" << endl;

  // Derive display name from file stem
  std::string stem = path;
  size_t slash = stem.find_last_of("/\\");
  if (slash != std::string::npos)
    stem = stem.substr(slash + 1);
  size_t dot = stem.find_last_of('.');
  if (dot != std::string::npos)
    stem = stem.substr(0, dot);
  mesh.displayName = stem.empty() ? "PMXModel" : stem;
  meshes3d.push_back(std::move(mesh));
  return true;
}

// ---------------------------------------------------------------------------
// FBX model loading (via vendored ufbx)
// ---------------------------------------------------------------------------

bool SceneRenderer::LoadFbxMesh(const std::string &path) {
  if (device == VK_NULL_HANDLE) {
    cout << "[FBX] Vulkan context not ready, deferring load: " << path << endl;
    return false;
  }

  ufbx_load_opts opts{};
  // Normalize to the engine's space (right-handed, Y-up, meters) and fill in
  // normals if the file lacks them, so downstream code needs no per-axis
  // fixups.
  opts.target_axes = ufbx_axes_right_handed_y_up;
  opts.target_unit_meters = 1.0f;
  opts.generate_missing_normals = true;

  ufbx_error err;
  ufbx_scene *scene = ufbx_load_file(path.c_str(), &opts, &err);
  if (!scene) {
    cout << "[FBX] Failed to load " << path << ": "
         << std::string(err.description.data, err.description.length) << endl;
    return false;
  }

  std::string fbxDir;
  {
    size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos)
      fbxDir = path.substr(0, slash);
  }
  auto fileExists = [](const std::string &p) {
    std::ifstream f(p);
    return f.good();
  };
  auto ufbxStr = [](ufbx_string s) {
    return std::string(s.data ? s.data : "", s.length);
  };
  // Resolve an FBX texture reference to an on-disk path we can actually load.
  auto resolveFbxTexture = [&](ufbx_texture *tex) -> std::string {
    if (!tex)
      return "";
    std::string abs = ufbxStr(tex->absolute_filename);
    if (!abs.empty() && fileExists(abs))
      return abs;
    std::string rel = ufbxStr(tex->relative_filename);
    if (rel.empty())
      rel = ufbxStr(tex->filename);
    for (char &c : rel)
      if (c == '\\')
        c = '/';
    if (rel.empty())
      return "";
    if (!fbxDir.empty() && fileExists(fbxDir + "/" + rel))
      return fbxDir + "/" + rel;
    // Last resort: just the basename inside the .fbx directory.
    size_t s = rel.find_last_of('/');
    std::string base = (s == std::string::npos) ? rel : rel.substr(s + 1);
    if (!fbxDir.empty() && fileExists(fbxDir + "/" + base))
      return fbxDir + "/" + base;
    return fileExists(rel) ? rel : "";
  };
  auto toGlm3 = [](ufbx_vec3 v) {
    return glm::vec3((float)v.x, (float)v.y, (float)v.z);
  };

  std::vector<MeshVertex> outVerts;
  std::vector<uint32_t> outIndices;
  std::vector<SubMesh> submeshes;
  glm::vec3 mn(std::numeric_limits<float>::max());
  glm::vec3 mx(-std::numeric_limits<float>::max());
  int loadedTextures = 0;

  for (size_t mi = 0; mi < scene->meshes.count; ++mi) {
    ufbx_mesh *mesh = scene->meshes.data[mi];
    if (mesh->faces.count == 0)
      continue;

    // Bake the first instance's world transform so multi-part models keep
    // their relative placement; identity if the mesh has no node.
    ufbx_matrix geomToWorld;
    bool hasXform = false;
    if (mesh->instances.count > 0) {
      geomToWorld = mesh->instances.data[0]->geometry_to_world;
      hasXform = true;
    }

    std::vector<uint32_t> triIdx(
        (mesh->max_face_triangles ? mesh->max_face_triangles : 1) * 3);

    // Emit one submesh from a set of face indices sharing one material.
    auto emitPart = [&](ufbx_material *mat, const uint32_t *faces,
                        size_t faceCount) {
      if (faceCount == 0)
        return;
      SubMesh sm;
      sm.indexOffset = (uint32_t)outIndices.size();
      if (mat) {
        sm.name = ufbxStr(mat->name);
        ufbx_material_map dm = mat->fbx.diffuse_color;
        if (dm.value_components >= 3)
          sm.diffuse = glm::vec3((float)dm.value_vec3.x, (float)dm.value_vec3.y,
                                 (float)dm.value_vec3.z);
        ufbx_texture *tex =
            dm.texture ? dm.texture : mat->pbr.base_color.texture;
        std::string texPath = resolveFbxTexture(tex);
        if (!texPath.empty()) {
          VkDescriptorSet ds = textureManager.GetTextureDescriptor(texPath);
          if (ds != VK_NULL_HANDLE) {
            sm.texturePath = texPath;
            sm.textureDescriptor = ds;
            sm.hasTexture = true;
            ++loadedTextures;
          }
        }
      }
      if (sm.name.empty())
        sm.name = "Material " + std::to_string(submeshes.size());

      for (size_t fi = 0; fi < faceCount; ++fi) {
        ufbx_face face = mesh->faces.data[faces[fi]];
        uint32_t numTris =
            ufbx_triangulate_face(triIdx.data(), triIdx.size(), mesh, face);
        for (uint32_t c = 0; c < numTris * 3; ++c) {
          uint32_t ci = triIdx[c]; // corner index into mesh attribute streams
          ufbx_vec3 p = ufbx_get_vertex_vec3(&mesh->vertex_position, ci);
          if (hasXform)
            p = ufbx_transform_position(&geomToWorld, p);
          MeshVertex mv;
          mv.pos = toGlm3(p);
          mv.normal = glm::vec3(0.0f, 1.0f, 0.0f);
          if (mesh->vertex_normal.exists) {
            ufbx_vec3 n = ufbx_get_vertex_vec3(&mesh->vertex_normal, ci);
            if (hasXform)
              n = ufbx_transform_direction(&geomToWorld, n);
            glm::vec3 gn = toGlm3(n);
            float len = glm::length(gn);
            mv.normal = (len > 1e-8f) ? gn / len : glm::vec3(0, 1, 0);
          }
          if (mesh->vertex_uv.exists) {
            ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, ci);
            // FBX UV origin is bottom-left; flip V for Vulkan's top-left.
            mv.uv = glm::vec2((float)uv.x, 1.0f - (float)uv.y);
          }
          mn = glm::min(mn, mv.pos);
          mx = glm::max(mx, mv.pos);
          outIndices.push_back((uint32_t)outVerts.size());
          outVerts.push_back(mv);
        }
      }
      sm.indexCount = (uint32_t)outIndices.size() - sm.indexOffset;
      if (sm.indexCount > 0)
        submeshes.push_back(std::move(sm));
    };

    if (mesh->material_parts.count > 0) {
      for (size_t pi = 0; pi < mesh->material_parts.count; ++pi) {
        const ufbx_mesh_part &part = mesh->material_parts.data[pi];
        ufbx_material *mat =
            (pi < mesh->materials.count) ? mesh->materials.data[pi] : nullptr;
        emitPart(mat, part.face_indices.data, part.face_indices.count);
      }
    } else {
      std::vector<uint32_t> allFaces(mesh->faces.count);
      for (size_t i = 0; i < mesh->faces.count; ++i)
        allFaces[i] = (uint32_t)i;
      ufbx_material *mat =
          mesh->materials.count > 0 ? mesh->materials.data[0] : nullptr;
      emitPart(mat, allFaces.data(), allFaces.size());
    }
  }

  ufbx_free_scene(scene);

  if (outVerts.empty() || outIndices.empty()) {
    cout << "[FBX] No geometry in " << path << endl;
    return false;
  }

  Mesh3D mesh;
  mesh.path = path;
  mesh.aabbMin = mn;
  mesh.aabbMax = mx;
  mesh.vertexCount = (uint32_t)outVerts.size();
  mesh.submeshes = std::move(submeshes);

  // Auto-fit: center + scale longest axis to ~1.5 units (matches OBJ/PMX).
  glm::vec3 center = (mn + mx) * 0.5f;
  glm::vec3 extent = mx - mn;
  float longest = std::max(extent.x, std::max(extent.y, extent.z));
  float fitScale = (longest > 1e-6f) ? (1.5f / longest) : 1.0f;
  glm::mat4 M(1.0f);
  M = glm::scale(M, glm::vec3(fitScale));
  M = glm::translate(M, -center);
  mesh.autoFit = M;

  mesh.cpuPositions.resize(outVerts.size());
  for (size_t i = 0; i < outVerts.size(); ++i)
    mesh.cpuPositions[i] = outVerts[i].pos;
  mesh.cpuIndices = outIndices;

  if (!UploadMeshBuffers(mesh, outVerts, outIndices)) {
    cout << "[FBX] Failed to upload mesh buffers" << endl;
    DestroyMesh(mesh);
    return false;
  }

  std::string stem = path;
  size_t slash = stem.find_last_of("/\\");
  if (slash != std::string::npos)
    stem = stem.substr(slash + 1);
  size_t dot = stem.find_last_of('.');
  if (dot != std::string::npos)
    stem = stem.substr(0, dot);
  mesh.displayName = stem.empty() ? "FBXModel" : stem;

  cout << "[FBX] Loaded " << path << " — verts: " << outVerts.size()
       << ", tris: " << (outIndices.size() / 3)
       << ", surfaces: " << mesh.submeshes.size() << " (" << loadedTextures
       << " textured)" << endl;
  meshes3d.push_back(std::move(mesh));
  return true;
}

void SceneRenderer::UpdateCamera3D(const ViewportInput &in) {
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

glm::mat4 SceneRenderer::Mesh3D::ComputeModel() const {
  glm::mat4 M(1.0f);
  M = glm::translate(M, userPosition);
  M = glm::rotate(M, glm::radians(userRotation.z), glm::vec3(0, 0, 1));
  M = glm::rotate(M, glm::radians(userRotation.y), glm::vec3(0, 1, 0));
  M = glm::rotate(M, glm::radians(userRotation.x), glm::vec3(1, 0, 0));
  M = glm::scale(M, userScale);
  return M * autoFit;
}

glm::vec3 SceneRenderer::GetMesh3DPosition(size_t i) const {
  if (i >= meshes3d.size())
    return glm::vec3(0.0f);
  return meshes3d[i].userPosition;
}
glm::vec3 SceneRenderer::GetMesh3DRotation(size_t i) const {
  if (i >= meshes3d.size())
    return glm::vec3(0.0f);
  return meshes3d[i].userRotation;
}
glm::vec3 SceneRenderer::GetMesh3DScale(size_t i) const {
  if (i >= meshes3d.size())
    return glm::vec3(1.0f);
  return meshes3d[i].userScale;
}
const std::string &SceneRenderer::GetMesh3DPath(size_t i) const {
  static const std::string empty;
  if (i >= meshes3d.size())
    return empty;
  return meshes3d[i].path;
}
uint32_t SceneRenderer::GetMesh3DVertexCount(size_t i) const {
  if (i >= meshes3d.size())
    return 0;
  return meshes3d[i].vertexCount;
}
uint32_t SceneRenderer::GetMesh3DTriangleCount(size_t i) const {
  if (i >= meshes3d.size())
    return 0;
  return meshes3d[i].indexCount / 3;
}
uint32_t SceneRenderer::GetMesh3DBoneCount(size_t i) const {
  if (i >= meshes3d.size())
    return 0;
  return (uint32_t)meshes3d[i].bones.size();
}
void SceneRenderer::SetMesh3DTransform(size_t i, const glm::vec3 &position,
                                       const glm::vec3 &rotationEuler,
                                       const glm::vec3 &scale) {
  if (i >= meshes3d.size())
    return;
  meshes3d[i].userPosition = position;
  meshes3d[i].userRotation = rotationEuler;
  meshes3d[i].userScale = scale;
}

void SceneRenderer::SetMesh3DDebugSource(size_t i, const char *file, int line) {
  if (i >= meshes3d.size())
    return;
  meshes3d[i].debugSrcFile = file ? file : "";
  meshes3d[i].debugSrcLine = line;
}
const std::string &SceneRenderer::GetMesh3DDebugSrcFile(size_t i) const {
  static const std::string empty;
  if (i >= meshes3d.size())
    return empty;
  return meshes3d[i].debugSrcFile;
}
int SceneRenderer::GetMesh3DDebugSrcLine(size_t i) const {
  if (i >= meshes3d.size())
    return 0;
  return meshes3d[i].debugSrcLine;
}

// ---------------------------------------------------------------------------
// Surfaces (submeshes) + per-surface texturing + click-picking
// ---------------------------------------------------------------------------

uint32_t SceneRenderer::GetMesh3DSubmeshCount(size_t i) const {
  return i < meshes3d.size() ? (uint32_t)meshes3d[i].submeshes.size() : 0;
}
const std::string &SceneRenderer::GetMesh3DSubmeshName(size_t i,
                                                       uint32_t sub) const {
  static const std::string empty;
  if (i >= meshes3d.size() || sub >= meshes3d[i].submeshes.size())
    return empty;
  return meshes3d[i].submeshes[sub].name;
}
const std::string &SceneRenderer::GetMesh3DSubmeshTexture(size_t i,
                                                          uint32_t sub) const {
  static const std::string empty;
  if (i >= meshes3d.size() || sub >= meshes3d[i].submeshes.size())
    return empty;
  return meshes3d[i].submeshes[sub].texturePath;
}
glm::vec3 SceneRenderer::GetMesh3DSubmeshDiffuse(size_t i, uint32_t sub) const {
  if (i >= meshes3d.size() || sub >= meshes3d[i].submeshes.size())
    return glm::vec3(1.0f);
  return meshes3d[i].submeshes[sub].diffuse;
}

bool SceneRenderer::BindMesh3DSubmeshTexture(size_t i, uint32_t sub,
                                             const std::string &path) {
  if (i >= meshes3d.size() || sub >= meshes3d[i].submeshes.size())
    return false;
  SubMesh &sm = meshes3d[i].submeshes[sub];
  if (path.empty()) {
    ClearMesh3DSubmeshTexture(i, sub);
    return true;
  }
  VkDescriptorSet ds = textureManager.GetTextureDescriptor(path);
  if (ds == VK_NULL_HANDLE) {
    cout << "[Tex] Failed to bind texture: " << path << endl;
    return false;
  }
  sm.texturePath = path;
  sm.textureDescriptor = ds;
  sm.hasTexture = true;
  cout << "[Tex] Bound '" << path << "' to surface " << sub << " ("
       << (sm.name.empty() ? "?" : sm.name) << ") of "
       << meshes3d[i].displayName << endl;
  return true;
}

void SceneRenderer::ClearMesh3DSubmeshTexture(size_t i, uint32_t sub) {
  if (i >= meshes3d.size() || sub >= meshes3d[i].submeshes.size())
    return;
  SubMesh &sm = meshes3d[i].submeshes[sub];
  sm.texturePath.clear();
  sm.textureDescriptor = VK_NULL_HANDLE;
  sm.hasTexture = false;
}

bool SceneRenderer::ScreenToRay(float pxX, float pxY, glm::vec3 &outOrigin,
                                glm::vec3 &outDir) const {
  if (width <= 0 || height <= 0)
    return false;
  float aspect = (float)width / (float)height;
  glm::vec3 forward = GetCameraForward();
  glm::mat4 view = glm::lookAt(camera3d.position, camera3d.position + forward,
                               glm::vec3(0.0f, 1.0f, 0.0f));
  glm::mat4 proj = glm::perspective(glm::radians(camera3d.fovDeg), aspect,
                                    camera3d.nearPlane, camera3d.farPlane);
  glm::mat4 invVP = glm::inverse(proj * view);
  // Panel pixel -> NDC (display is V-flipped, same as ScreenToGround).
  float ndcX = 2.0f * (pxX / (float)width) - 1.0f;
  float ndcY = 1.0f - 2.0f * (pxY / (float)height);
  glm::vec4 nh = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
  glm::vec4 fh = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
  if (nh.w == 0.0f || fh.w == 0.0f)
    return false;
  glm::vec3 nw = glm::vec3(nh) / nh.w;
  glm::vec3 fw = glm::vec3(fh) / fh.w;
  glm::vec3 dir = fw - nw;
  if (glm::dot(dir, dir) < 1e-12f)
    return false;
  outOrigin = nw;
  outDir = glm::normalize(dir);
  return true;
}

namespace {
// Möller–Trumbore ray/triangle intersection (two-sided). Returns the
// positive ray parameter t of the hit in `t`.
bool RayTriangleMT(const glm::vec3 &o, const glm::vec3 &d, const glm::vec3 &a,
                   const glm::vec3 &b, const glm::vec3 &c, float &t) {
  const float EPS = 1e-7f;
  glm::vec3 e1 = b - a, e2 = c - a;
  glm::vec3 p = glm::cross(d, e2);
  float det = glm::dot(e1, p);
  if (std::fabs(det) < EPS)
    return false;
  float inv = 1.0f / det;
  glm::vec3 tv = o - a;
  float u = glm::dot(tv, p) * inv;
  if (u < 0.0f || u > 1.0f)
    return false;
  glm::vec3 q = glm::cross(tv, e1);
  float v = glm::dot(d, q) * inv;
  if (v < 0.0f || u + v > 1.0f)
    return false;
  float tt = glm::dot(e2, q) * inv;
  if (tt <= EPS)
    return false;
  t = tt;
  return true;
}
} // namespace

bool SceneRenderer::PickMesh3DSurface(float pxX, float pxY, int &outMeshIndex,
                                      int &outSubmesh) const {
  outMeshIndex = -1;
  outSubmesh = -1;
  glm::vec3 ro, rd;
  if (!ScreenToRay(pxX, pxY, ro, rd))
    return false;

  float bestT = std::numeric_limits<float>::max();
  for (size_t mi = 0; mi < meshes3d.size(); ++mi) {
    const Mesh3D &m = meshes3d[mi];
    if (m.cpuPositions.empty() || m.cpuIndices.empty())
      continue;
    glm::mat4 model = m.ComputeModel();
    for (size_t i = 0; i + 2 < m.cpuIndices.size(); i += 3) {
      uint32_t ia = m.cpuIndices[i + 0];
      uint32_t ib = m.cpuIndices[i + 1];
      uint32_t ic = m.cpuIndices[i + 2];
      if (ia >= m.cpuPositions.size() || ib >= m.cpuPositions.size() ||
          ic >= m.cpuPositions.size())
        continue;
      glm::vec3 a = glm::vec3(model * glm::vec4(m.cpuPositions[ia], 1.0f));
      glm::vec3 b = glm::vec3(model * glm::vec4(m.cpuPositions[ib], 1.0f));
      glm::vec3 c = glm::vec3(model * glm::vec4(m.cpuPositions[ic], 1.0f));
      float t;
      if (RayTriangleMT(ro, rd, a, b, c, t) && t < bestT) {
        bestT = t;
        outMeshIndex = (int)mi;
        // Map the hit triangle's first index to its owning surface range.
        uint32_t firstIndex = (uint32_t)i;
        outSubmesh = m.submeshes.empty() ? 0 : (int)(m.submeshes.size() - 1);
        for (size_t s = 0; s < m.submeshes.size(); ++s) {
          const SubMesh &sm = m.submeshes[s];
          if (firstIndex >= sm.indexOffset &&
              firstIndex < sm.indexOffset + sm.indexCount) {
            outSubmesh = (int)s;
            break;
          }
        }
      }
    }
  }
  return outMeshIndex >= 0;
}

void SceneRenderer::DragMesh3DScreen(size_t i, float dxPx, float dyPx,
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

void SceneRenderer::InitGridResources() {
  if (device == VK_NULL_HANDLE)
    return;

  // Infinite ground grid: one fullscreen triangle whose fragment shader
  // raycasts the y=0 plane and draws grid lines only where the ground is
  // visible. No per-frame vertex data — constant tiny memory, ~zero CPU,
  // and it always covers exactly the visible area (fades to the horizon).
  VkShaderModule vert =
      LoadShader("assets/shaders/vulkan/scene_infgrid.vert.spv");
  VkShaderModule frag =
      LoadShader("assets/shaders/vulkan/scene_infgrid.frag.spv");

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag;
  stages[1].pName = "main";

  // No vertex buffer: the shader emits the fullscreen triangle from
  // gl_VertexIndex.
  VkPipelineVertexInputStateCreateInfo vi{};
  vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo ms{};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  // Alpha blend so the grid lines fade smoothly over the background.
  VkPipelineColorBlendAttachmentState cba{};
  cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  cba.blendEnable = VK_TRUE;
  cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  cba.colorBlendOp = VK_BLEND_OP_ADD;
  cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  cba.alphaBlendOp = VK_BLEND_OP_ADD;
  VkPipelineColorBlendStateCreateInfo cb{};
  cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  cb.attachmentCount = 1;
  cb.pAttachments = &cba;

  VkPipelineDepthStencilStateCreateInfo ds{};
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  // Test against mesh depth (so meshes occlude the grid) but never write
  // depth (so the grid never blocks meshes). The shader writes the true
  // per-fragment ground depth via gl_FragDepth.
  ds.depthTestEnable = VK_TRUE;
  ds.depthWriteEnable = VK_FALSE;
  ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

  std::vector<VkDynamicState> dyn = {VK_DYNAMIC_STATE_VIEWPORT,
                                     VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynState{};
  dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynState.dynamicStateCount = (uint32_t)dyn.size();
  dynState.pDynamicStates = dyn.data();

  // view + proj (128 bytes — within the guaranteed push-constant minimum).
  VkPushConstantRange pc{};
  pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pc.offset = 0;
  pc.size = sizeof(glm::mat4) * 2;

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

  grid3d.vertexCount = 3; // fullscreen triangle
  vkDestroyShaderModule(device, vert, nullptr);
  vkDestroyShaderModule(device, frag, nullptr);
}

void SceneRenderer::DrawGrid3D(VkCommandBuffer cmd, const glm::mat4 &view,
                               const glm::mat4 &proj) {
  if (grid3d.pipeline == VK_NULL_HANDLE)
    return;
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, grid3d.pipeline);
  struct PC {
    glm::mat4 view;
    glm::mat4 proj;
  } pc;
  pc.view = view;
  pc.proj = proj;
  vkCmdPushConstants(cmd, grid3d.layout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, sizeof(PC), &pc);
  vkCmdDraw(cmd, 3, 1, 0, 0);
}

// ---------------------------------------------------------------------------
// Sun (decorative sky sphere)
// ---------------------------------------------------------------------------

void SceneRenderer::InitSunResources() {
  if (device == VK_NULL_HANDLE)
    return;

  // Unit UV-sphere; DrawSun scales/positions it. Pos + normal so the
  // unlit shader can add a faint top-to-bottom gradient.
  const int segments = 24, rings = 16;
  std::vector<MeshVertex> verts;
  std::vector<uint32_t> idx;
  for (int ring = 0; ring <= rings; ++ring) {
    float v = (float)ring / (float)rings;
    float phi = v * 3.14159265358979f;
    float y = std::cos(phi), r = std::sin(phi);
    for (int seg = 0; seg <= segments; ++seg) {
      float u = (float)seg / (float)segments;
      float theta = u * 2.0f * 3.14159265358979f;
      glm::vec3 p(r * std::cos(theta), y, r * std::sin(theta));
      verts.push_back({p, p});
    }
  }
  int stride = segments + 1;
  for (int ring = 0; ring < rings; ++ring)
    for (int seg = 0; seg < segments; ++seg) {
      uint32_t a = (uint32_t)(ring * stride + seg), b = a + 1;
      uint32_t c = (uint32_t)((ring + 1) * stride + seg), d = c + 1;
      idx.insert(idx.end(), {a, c, b, b, c, d});
    }

  auto makeBuf = [&](VkDeviceSize size, VkBufferUsageFlags usage,
                     VkBuffer &outBuf, VkDeviceMemory &outMem) {
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bi, nullptr, &outBuf) != VK_SUCCESS)
      throw std::runtime_error("failed to create sun buffer");
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device, outBuf, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = findMemoryType(
        mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device, &ai, nullptr, &outMem) != VK_SUCCESS)
      throw std::runtime_error("failed to allocate sun memory");
    vkBindBufferMemory(device, outBuf, outMem, 0);
  };

  VkDeviceSize vSize = sizeof(MeshVertex) * verts.size();
  makeBuf(vSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, sun.vertexBuffer,
          sun.vertexMemory);
  void *vmap = nullptr;
  vkMapMemory(device, sun.vertexMemory, 0, vSize, 0, &vmap);
  std::memcpy(vmap, verts.data(), (size_t)vSize);
  vkUnmapMemory(device, sun.vertexMemory);

  VkDeviceSize iSize = sizeof(uint32_t) * idx.size();
  makeBuf(iSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, sun.indexBuffer,
          sun.indexMemory);
  void *imap = nullptr;
  vkMapMemory(device, sun.indexMemory, 0, iSize, 0, &imap);
  std::memcpy(imap, idx.data(), (size_t)iSize);
  vkUnmapMemory(device, sun.indexMemory);
  sun.indexCount = (uint32_t)idx.size();

  VkShaderModule vert = LoadShader("assets/shaders/vulkan/scene_sun.vert.spv");
  VkShaderModule frag = LoadShader("assets/shaders/vulkan/scene_sun.frag.spv");

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

  // Unlit sky decoration: no depth test/write so it always sits behind
  // everything drawn after it.
  VkPipelineDepthStencilStateCreateInfo ds{};
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  ds.depthTestEnable = VK_FALSE;
  ds.depthWriteEnable = VK_FALSE;
  ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;

  std::vector<VkDynamicState> dyn = {VK_DYNAMIC_STATE_VIEWPORT,
                                     VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynState{};
  dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynState.dynamicStateCount = (uint32_t)dyn.size();
  dynState.pDynamicStates = dyn.data();

  VkPushConstantRange pcr{};
  pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pcr.offset = 0;
  pcr.size = sizeof(glm::mat4) + sizeof(glm::vec4); // mvp + color

  VkPipelineLayoutCreateInfo li{};
  li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  li.pushConstantRangeCount = 1;
  li.pPushConstantRanges = &pcr;
  if (vkCreatePipelineLayout(device, &li, nullptr, &sun.layout) != VK_SUCCESS)
    throw std::runtime_error("failed to create sun pipeline layout");

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
  pi.layout = sun.layout;
  pi.renderPass = offscreen.renderPass;
  pi.subpass = 0;
  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr,
                                &sun.pipeline) != VK_SUCCESS)
    throw std::runtime_error("failed to create sun pipeline");

  vkDestroyShaderModule(device, vert, nullptr);
  vkDestroyShaderModule(device, frag, nullptr);
}

void SceneRenderer::DrawSun(VkCommandBuffer cmd, const glm::mat4 &viewProj) {
  if (sun.pipeline == VK_NULL_HANDLE || sun.indexCount == 0)
    return;
  // Anchor the sun to the camera along the light direction so it reads as
  // a fixed, distant sun regardless of where the camera flies.
  const float D = 220.0f;    // distance (well within the 500 far plane)
  const float R = D * 0.06f; // radius -> ~7 degrees of arc
  glm::vec3 dir = glm::normalize(sunLight.direction);
  glm::vec3 center = camera3d.position + dir * D;
  glm::mat4 model = glm::translate(glm::mat4(1.0f), center) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(R));

  struct PC {
    glm::mat4 mvp;
    glm::vec4 color;
  } pc;
  pc.mvp = viewProj * model;
  pc.color = glm::vec4(1.0f, 0.86f, 0.46f, 1.0f);

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sun.pipeline);
  vkCmdPushConstants(cmd, sun.layout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, sizeof(PC), &pc);
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(cmd, 0, 1, &sun.vertexBuffer, offsets);
  vkCmdBindIndexBuffer(cmd, sun.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(cmd, sun.indexCount, 1, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// Camera helpers / picking
// ---------------------------------------------------------------------------

glm::vec3 SceneRenderer::GetCameraForward() const {
  float yawR = glm::radians(camera3d.yaw);
  float pitchR = glm::radians(camera3d.pitch);
  glm::vec3 forward;
  forward.x = std::cos(yawR) * std::cos(pitchR);
  forward.y = std::sin(pitchR);
  forward.z = std::sin(yawR) * std::cos(pitchR);
  return glm::normalize(forward);
}

bool SceneRenderer::ScreenToGround(float pxX, float pxY,
                                   glm::vec3 &outWorld) const {
  if (width <= 0 || height <= 0)
    return false;

  // Rebuild the exact view/projection used by RenderSceneToTexture.
  float aspect = (float)width / (float)height;
  glm::vec3 forward = GetCameraForward();
  glm::mat4 view = glm::lookAt(camera3d.position, camera3d.position + forward,
                               glm::vec3(0.0f, 1.0f, 0.0f));
  glm::mat4 proj = glm::perspective(glm::radians(camera3d.fovDeg), aspect,
                                    camera3d.nearPlane, camera3d.farPlane);
  glm::mat4 invVP = glm::inverse(proj * view);

  // Panel pixel -> NDC. The displayed image is V-flipped (ImGui samples
  // the framebuffer bottom-up), so panel-top maps to +Y in glm NDC.
  float ndcX = 2.0f * (pxX / (float)width) - 1.0f;
  float ndcY = 1.0f - 2.0f * (pxY / (float)height);

  glm::vec4 nh = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
  glm::vec4 fh = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
  if (nh.w == 0.0f || fh.w == 0.0f)
    return false;
  glm::vec3 nw = glm::vec3(nh) / nh.w;
  glm::vec3 fw = glm::vec3(fh) / fh.w;
  glm::vec3 dir = fw - nw;
  if (std::fabs(dir.y) < 1e-6f)
    return false; // ray parallel to the ground

  float t = -nw.y / dir.y;
  glm::vec3 hit = nw + t * dir;
  if (glm::dot(hit - camera3d.position, forward) <= 0.0f)
    return false; // intersection is behind the camera
  outWorld = hit;
  return true;
}

// ---------------------------------------------------------------------------
// Procedural primitives
// ---------------------------------------------------------------------------

bool SceneRenderer::LoadCube(const std::string &name, float size) {
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

bool SceneRenderer::LoadSphere(const std::string &name, float radius,
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

bool SceneRenderer::LoadPlane(const std::string &name, float size) {
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

void SceneRenderer::ClearMeshes3D() {
  if (device != VK_NULL_HANDLE)
    vkDeviceWaitIdle(device);
  for (auto &m : meshes3d)
    DestroyMesh(m);
  meshes3d.clear();
}

SceneRenderer::EditorSnapshot SceneRenderer::CaptureEditorSnapshot() const {
  EditorSnapshot snapshot;
  snapshot.cameraPosition = cameraPosition;
  snapshot.cameraZoom = cameraZoom;
  snapshot.zoom = zoom;
  snapshot.gridSize = gridSize;
  snapshot.gridColor = glm::vec4(gridColor.x, gridColor.y, gridColor.z,
                                 gridColor.w);
  snapshot.backgroundColor =
      glm::vec4(bgColor.x, bgColor.y, bgColor.z, bgColor.w);
  snapshot.editMode = static_cast<int>(currentMode);
  snapshot.gridVisible = gridVisible;
  snapshot.snapToGrid = snapToGrid;
  snapshot.grid3dVisible = grid3dVisible;
  snapshot.sunVisible = sunVisible;
  snapshot.camera3d = camera3d;
  snapshot.sunLight = sunLight;
  snapshot.entities.reserve(meshes3d.size());

  for (const Mesh3D &mesh : meshes3d) {
    EditorEntitySnapshot entity;
    entity.name = mesh.displayName;
    entity.path = mesh.path;
    entity.position = mesh.userPosition;
    entity.rotation = mesh.userRotation;
    entity.scale = mesh.userScale;

    entity.isLight = mesh.isLight;
    entity.lightGamma = mesh.lightGamma;
    entity.lightColor = mesh.lightColor;
    entity.lightIntensity = mesh.lightIntensity;
    entity.lightType = mesh.lightType;
    entity.lightRange = mesh.lightRange;
    entity.lightSpotAngle = mesh.lightSpotAngle;

    entity.isCamera = mesh.isCamera;
    entity.camProjection = mesh.camProjection;
    entity.camFov = mesh.camFov;
    entity.camOrthoSize = mesh.camOrthoSize;
    entity.camNear = mesh.camNear;
    entity.camFar = mesh.camFar;

    entity.hasPhysics = mesh.hasPhysics;
    entity.useGravity = mesh.useGravity;
    entity.isKinematic = mesh.isKinematic;
    entity.mass = mesh.mass;
    entity.drag = mesh.drag;
    entity.gravityY = mesh.gravityY;
    entity.velocity = mesh.velocity;

    entity.hasAudio = mesh.hasAudio;
    entity.audioPath = mesh.audioPath;
    entity.isPlaying = mesh.isPlaying;

    entity.debugSrcFile = mesh.debugSrcFile;
    entity.debugSrcLine = mesh.debugSrcLine;
    entity.submeshTextures.reserve(mesh.submeshes.size());
    for (const SubMesh &submesh : mesh.submeshes)
      entity.submeshTextures.push_back(submesh.texturePath);

    snapshot.entities.push_back(std::move(entity));
  }
  return snapshot;
}

bool SceneRenderer::RestoreEditorSnapshot(const EditorSnapshot &snapshot) {
  cameraPosition = snapshot.cameraPosition;
  cameraZoom = snapshot.cameraZoom;
  zoom = snapshot.zoom;
  gridSize = snapshot.gridSize;
  gridColor = ImVec4(snapshot.gridColor.r, snapshot.gridColor.g,
                     snapshot.gridColor.b, snapshot.gridColor.a);
  bgColor = ImVec4(snapshot.backgroundColor.r, snapshot.backgroundColor.g,
                   snapshot.backgroundColor.b, snapshot.backgroundColor.a);
  if (snapshot.editMode >= static_cast<int>(EditMode::SELECT) &&
      snapshot.editMode <= static_cast<int>(EditMode::SCALE)) {
    currentMode = static_cast<EditMode>(snapshot.editMode);
  }
  gridVisible = snapshot.gridVisible;
  snapToGrid = snapshot.snapToGrid;
  grid3dVisible = snapshot.grid3dVisible;
  sunVisible = snapshot.sunVisible;
  camera3d = snapshot.camera3d;
  sunLight = snapshot.sunLight;

  ClearMeshes3D();
  bool allLoaded = true;

  for (const EditorEntitySnapshot &entity : snapshot.entities) {
    bool loaded = false;
    if (entity.isLight) {
      loaded = LoadLight(entity.name, entity.lightType, entity.lightColor,
                         entity.lightIntensity, entity.lightRange,
                         entity.lightSpotAngle, entity.lightGamma);
    } else if (entity.isCamera) {
      loaded = LoadCamera(entity.name, entity.camProjection, entity.camFov,
                          entity.camOrthoSize, entity.camNear, entity.camFar);
    } else if (entity.path.rfind("<primitive:", 0) == 0) {
      if (entity.path.find("sphere") != std::string::npos)
        loaded = LoadSphere(entity.name);
      else if (entity.path.find("plane") != std::string::npos)
        loaded = LoadPlane(entity.name);
      else
        loaded = LoadCube(entity.name);
    } else if (!entity.path.empty()) {
      std::string ext = std::filesystem::path(entity.path).extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(),
                     [](unsigned char c) { return (char)std::tolower(c); });
      if (ext == ".pmx")
        loaded = LoadPMXMesh(entity.path);
      else if (ext == ".fbx")
        loaded = LoadFbxMesh(entity.path);
      else
        loaded = LoadObjMesh(entity.path);
    } else {
      // A nameless non-light/camera mesh cannot be reconstructed without an
      // asset path. Keep the history operation deterministic and report it
      // to the caller instead of silently inventing a different object.
      allLoaded = false;
      continue;
    }

    if (!loaded || meshes3d.empty()) {
      allLoaded = false;
      continue;
    }

    const size_t index = meshes3d.size() - 1;
    // External loaders derive a display name from the filename. The editor
    // snapshot must win so undo/redo also preserves the hierarchy label.
    meshes3d[index].displayName = entity.name;
    SetMesh3DTransform(index, entity.position, entity.rotation,
                       entity.scale);
    SetMesh3DDebugSource(index, entity.debugSrcFile.c_str(),
                         entity.debugSrcLine);

    Mesh3D &mesh = meshes3d[index];
    mesh.hasPhysics = entity.hasPhysics;
    mesh.useGravity = entity.useGravity;
    mesh.isKinematic = entity.isKinematic;
    mesh.mass = entity.mass;
    mesh.drag = entity.drag;
    mesh.gravityY = entity.gravityY;
    mesh.velocity = entity.velocity;
    mesh.hasAudio = entity.hasAudio;
    mesh.audioPath = entity.audioPath;
    mesh.isPlaying = entity.isPlaying;

    // Loaders may auto-bind the model's original textures. Clear every
    // surface first so an undo to a deliberately cleared surface is exact.
    for (uint32_t sub = 0; sub < mesh.submeshes.size(); ++sub) {
      const std::string texture =
          sub < entity.submeshTextures.size()
              ? entity.submeshTextures[sub]
              : std::string();
      if (texture.empty())
        ClearMesh3DSubmeshTexture(index, sub);
      else if (!BindMesh3DSubmeshTexture(index, sub, texture))
        allLoaded = false;
    }
  }

  return allLoaded;
}

const std::string &SceneRenderer::GetMesh3DName(size_t i) const {
  static const std::string empty;
  if (i >= meshes3d.size())
    return empty;
  return meshes3d[i].displayName;
}

bool SceneRenderer::LoadLight(const std::string &name, int type,
                              const glm::vec3 &color, float intensity,
                              float range, float spotAngle, float gamma) {
  if (device == VK_NULL_HANDLE)
    return false;

  float radius = 0.2f;
  int segments = 16;
  int rings = 12;

  std::vector<MeshVertex> verts;
  std::vector<uint32_t> idx;
  verts.reserve((rings + 1) * (segments + 1));
  for (int ring = 0; ring <= rings; ++ring) {
    float v = (float)ring / (float)rings;
    float phi = v * 3.14159265358979f;
    float y = std::cos(phi);
    float r = std::sin(phi);
    for (int seg = 0; seg <= segments; ++seg) {
      float u = (float)seg / (float)segments;
      float theta = u * 2.0f * 3.14159265358979f;
      glm::vec3 p(r * std::cos(theta), y, r * std::sin(theta));
      glm::vec3 normal = p;
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
  mesh.path = ""; // empty path represents a custom light node
  mesh.displayName = name;
  mesh.aabbMin = glm::vec3(-radius);
  mesh.aabbMax = glm::vec3(radius);
  mesh.autoFit = glm::mat4(1.0f);

  mesh.isLight = true;
  mesh.lightType = type;
  mesh.lightColor = color;
  mesh.lightIntensity = intensity;
  mesh.lightRange = range;
  mesh.lightSpotAngle = spotAngle;
  mesh.lightGamma = gamma;

  if (!UploadMeshBuffers(mesh, verts, idx)) {
    DestroyMesh(mesh);
    return false;
  }
  meshes3d.push_back(std::move(mesh));
  return true;
}

bool SceneRenderer::IsMesh3DLight(size_t i) const {
  if (i >= meshes3d.size())
    return false;
  return meshes3d[i].isLight;
}
float SceneRenderer::GetMesh3DLightGamma(size_t i) const {
  if (i >= meshes3d.size())
    return 1.05f;
  return meshes3d[i].lightGamma;
}
glm::vec3 SceneRenderer::GetMesh3DLightColor(size_t i) const {
  if (i >= meshes3d.size())
    return glm::vec3(1.0f);
  return meshes3d[i].lightColor;
}
float SceneRenderer::GetMesh3DLightIntensity(size_t i) const {
  if (i >= meshes3d.size())
    return 1.0f;
  return meshes3d[i].lightIntensity;
}
int SceneRenderer::GetMesh3DLightType(size_t i) const {
  if (i >= meshes3d.size())
    return 0;
  return meshes3d[i].lightType;
}
float SceneRenderer::GetMesh3DLightRange(size_t i) const {
  if (i >= meshes3d.size())
    return 10.0f;
  return meshes3d[i].lightRange;
}
float SceneRenderer::GetMesh3DLightSpotAngle(size_t i) const {
  if (i >= meshes3d.size())
    return 30.0f;
  return meshes3d[i].lightSpotAngle;
}

void SceneRenderer::SetMesh3DLightGamma(size_t i, float gamma) {
  if (i < meshes3d.size())
    meshes3d[i].lightGamma = gamma;
}
void SceneRenderer::SetMesh3DLightColor(size_t i, const glm::vec3 &color) {
  if (i < meshes3d.size())
    meshes3d[i].lightColor = color;
}
void SceneRenderer::SetMesh3DLightIntensity(size_t i, float intensity) {
  if (i < meshes3d.size())
    meshes3d[i].lightIntensity = intensity;
}
void SceneRenderer::SetMesh3DLightType(size_t i, int type) {
  if (i < meshes3d.size())
    meshes3d[i].lightType = type;
}
void SceneRenderer::SetMesh3DLightRange(size_t i, float range) {
  if (i < meshes3d.size())
    meshes3d[i].lightRange = range;
}
void SceneRenderer::SetMesh3DLightSpotAngle(size_t i, float angle) {
  if (i < meshes3d.size())
    meshes3d[i].lightSpotAngle = angle;
}

// ---------------------------------------------------------------------------
// Player camera object + gizmos + preview
// ---------------------------------------------------------------------------

// Forward direction from Euler degrees, matching the light convention used in
// DrawMeshes (yaw offset by -90 so 0,0,0 faces -Z like the editor camera).
static glm::vec3 ForwardFromEuler(const glm::vec3 &rotDeg) {
  float pitch = glm::radians(rotDeg.x);
  float yaw = glm::radians(rotDeg.y - 90.0f);
  glm::vec3 f;
  f.x = std::cos(yaw) * std::cos(pitch);
  f.y = std::sin(pitch);
  f.z = std::sin(yaw) * std::cos(pitch);
  return glm::normalize(f);
}

bool SceneRenderer::LoadCamera(const std::string &name, int projection,
                               float fov, float orthoSize, float nearP,
                               float farP) {
  if (device == VK_NULL_HANDLE)
    return false;

  // Small box marker for the camera body. The dashed frustum gizmo (drawn
  // separately) is what actually shows where it points.
  std::vector<MeshVertex> verts;
  std::vector<uint32_t> idx;
  float s = 0.15f;
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
  glm::vec3 v[8] = {{-s, -s, -s}, {s, -s, -s}, {s, s, -s}, {-s, s, -s},
                    {-s, -s, s},  {s, -s, s},  {s, s, s},  {-s, s, s}};
  tri(v[0], v[2], v[1]);
  tri(v[0], v[3], v[2]);
  tri(v[4], v[5], v[6]);
  tri(v[4], v[6], v[7]);
  tri(v[0], v[7], v[3]);
  tri(v[0], v[4], v[7]);
  tri(v[1], v[2], v[6]);
  tri(v[1], v[6], v[5]);
  tri(v[0], v[1], v[5]);
  tri(v[0], v[5], v[4]);
  tri(v[3], v[7], v[6]);
  tri(v[3], v[6], v[2]);

  Mesh3D mesh;
  mesh.path = "";
  mesh.displayName = name;
  mesh.aabbMin = glm::vec3(-s);
  mesh.aabbMax = glm::vec3(s);
  mesh.autoFit = glm::mat4(1.0f);
  mesh.isCamera = true;
  mesh.camProjection = projection;
  mesh.camFov = fov;
  mesh.camOrthoSize = orthoSize;
  mesh.camNear = nearP;
  mesh.camFar = farP;
  if (!UploadMeshBuffers(mesh, verts, idx)) {
    DestroyMesh(mesh);
    return false;
  }
  meshes3d.push_back(std::move(mesh));
  cout << "[Camera] '" << name << "' added (proj "
       << (projection == 1 ? "ortho" : "persp") << ")" << endl;
  return true;
}

bool SceneRenderer::IsMesh3DCamera(size_t i) const {
  return i < meshes3d.size() && meshes3d[i].isCamera;
}
int SceneRenderer::GetMesh3DCameraProjection(size_t i) const {
  return i < meshes3d.size() ? meshes3d[i].camProjection : 0;
}
float SceneRenderer::GetMesh3DCameraFov(size_t i) const {
  return i < meshes3d.size() ? meshes3d[i].camFov : 60.0f;
}
float SceneRenderer::GetMesh3DCameraOrthoSize(size_t i) const {
  return i < meshes3d.size() ? meshes3d[i].camOrthoSize : 5.0f;
}
float SceneRenderer::GetMesh3DCameraNear(size_t i) const {
  return i < meshes3d.size() ? meshes3d[i].camNear : 0.1f;
}
float SceneRenderer::GetMesh3DCameraFar(size_t i) const {
  return i < meshes3d.size() ? meshes3d[i].camFar : 100.0f;
}
void SceneRenderer::SetMesh3DCameraProjection(size_t i, int projection) {
  if (i < meshes3d.size())
    meshes3d[i].camProjection = projection;
}
void SceneRenderer::SetMesh3DCameraFov(size_t i, float fov) {
  if (i < meshes3d.size())
    meshes3d[i].camFov = fov;
}
void SceneRenderer::SetMesh3DCameraOrthoSize(size_t i, float size) {
  if (i < meshes3d.size())
    meshes3d[i].camOrthoSize = size;
}
void SceneRenderer::SetMesh3DCameraNear(size_t i, float nearP) {
  if (i < meshes3d.size())
    meshes3d[i].camNear = nearP;
}
void SceneRenderer::SetMesh3DCameraFar(size_t i, float farP) {
  if (i < meshes3d.size())
    meshes3d[i].camFar = farP;
}

bool SceneRenderer::HasPlayerCamera() const {
  for (const auto &m : meshes3d)
    if (m.isCamera)
      return true;
  return false;
}

bool SceneRenderer::ComputePlayerCameraMatrices(float aspect, glm::mat4 &view,
                                                glm::mat4 &proj) const {
  const Mesh3D *cam = nullptr;
  for (const auto &m : meshes3d)
    if (m.isCamera) {
      cam = &m;
      break;
    }
  if (!cam)
    return false;

  glm::vec3 pos = cam->userPosition;
  glm::vec3 fwd = ForwardFromEuler(cam->userRotation);
  glm::vec3 up(0.0f, 1.0f, 0.0f);
  if (std::fabs(glm::dot(fwd, up)) > 0.999f)
    up = glm::vec3(0.0f, 0.0f, 1.0f);
  view = glm::lookAt(pos, pos + fwd, up);

  if (cam->camProjection == 1) {
    float h = cam->camOrthoSize;
    float w = h * aspect;
    proj = glm::ortho(-w, w, -h, h, cam->camNear, cam->camFar);
  } else {
    proj = glm::perspective(glm::radians(cam->camFov), aspect, cam->camNear,
                            cam->camFar);
  }
  return true;
}

// ---------------------------------------------------------------------------
// Gizmo line pipeline
// ---------------------------------------------------------------------------

void SceneRenderer::InitGizmoPipeline() {
  if (device == VK_NULL_HANDLE)
    return;

  VkShaderModule vert =
      LoadShader("assets/shaders/vulkan/scene_gizmo.vert.spv");
  VkShaderModule frag =
      LoadShader("assets/shaders/vulkan/scene_gizmo.frag.spv");

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag;
  stages[1].pName = "main";

  struct GV {
    glm::vec3 pos;
    glm::vec3 color;
  };
  VkVertexInputBindingDescription binding{};
  binding.binding = 0;
  binding.stride = sizeof(GV);
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  VkVertexInputAttributeDescription attrs[2]{};
  attrs[0].binding = 0;
  attrs[0].location = 0;
  attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attrs[0].offset = offsetof(GV, pos);
  attrs[1].binding = 0;
  attrs[1].location = 1;
  attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attrs[1].offset = offsetof(GV, color);

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

  // Editor aid: always visible (no depth test) so the frustum/direction is
  // readable even when behind geometry.
  VkPipelineDepthStencilStateCreateInfo ds{};
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  ds.depthTestEnable = VK_FALSE;
  ds.depthWriteEnable = VK_FALSE;
  ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;

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
  if (vkCreatePipelineLayout(device, &li, nullptr, &gizmoPipelineLayout) !=
      VK_SUCCESS)
    throw std::runtime_error("failed to create gizmo pipeline layout");

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
  pi.layout = gizmoPipelineLayout;
  pi.renderPass = offscreen.renderPass;
  pi.subpass = 0;
  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr,
                                &gizmoPipeline) != VK_SUCCESS)
    throw std::runtime_error("failed to create gizmo pipeline");

  vkDestroyShaderModule(device, vert, nullptr);
  vkDestroyShaderModule(device, frag, nullptr);
}

void SceneRenderer::DrawGizmos(VkCommandBuffer cmd, const glm::mat4 &viewProj) {
  if (gizmoPipeline == VK_NULL_HANDLE)
    return;

  struct GV {
    glm::vec3 pos;
    glm::vec3 color;
  };
  std::vector<GV> verts;

  auto addDashed = [&](const glm::vec3 &a, const glm::vec3 &b,
                       const glm::vec3 &col) {
    float len = glm::length(b - a);
    if (len < 1e-5f)
      return;
    glm::vec3 dir = (b - a) / len;
    const float dash = 0.18f, gap = 0.12f;
    for (float t = 0.0f; t < len;) {
      float e = std::min(t + dash, len);
      verts.push_back({a + dir * t, col});
      verts.push_back({a + dir * e, col});
      t = e + gap;
    }
  };
  auto basis = [&](const glm::vec3 &fwd, glm::vec3 &right, glm::vec3 &up) {
    glm::vec3 wup(0.0f, 1.0f, 0.0f);
    if (std::fabs(glm::dot(fwd, wup)) > 0.999f)
      wup = glm::vec3(0.0f, 0.0f, 1.0f);
    right = glm::normalize(glm::cross(fwd, wup));
    up = glm::normalize(glm::cross(right, fwd));
  };

  const float gizAspect = (float)previewWidth / (float)previewHeight;

  for (const auto &m : meshes3d) {
    if (m.isCamera) {
      glm::vec3 pos = m.userPosition;
      glm::vec3 fwd = ForwardFromEuler(m.userRotation);
      glm::vec3 right, up;
      basis(fwd, right, up);
      glm::vec3 col(0.30f, 0.85f, 0.95f); // cyan
      const float dist = 2.5f;

      if (m.camProjection == 1) {
        // Orthographic: a box from a near rect (at the camera) to a far rect.
        float hH = m.camOrthoSize, hW = hH * gizAspect;
        glm::vec3 nc = pos, fc = pos + fwd * dist;
        glm::vec3 nTL = nc - right * hW + up * hH,
                  nTR = nc + right * hW + up * hH;
        glm::vec3 nBL = nc - right * hW - up * hH,
                  nBR = nc + right * hW - up * hH;
        glm::vec3 fTL = fc - right * hW + up * hH,
                  fTR = fc + right * hW + up * hH;
        glm::vec3 fBL = fc - right * hW - up * hH,
                  fBR = fc + right * hW - up * hH;
        addDashed(nTL, nTR, col);
        addDashed(nTR, nBR, col);
        addDashed(nBR, nBL, col);
        addDashed(nBL, nTL, col);
        addDashed(fTL, fTR, col);
        addDashed(fTR, fBR, col);
        addDashed(fBR, fBL, col);
        addDashed(fBL, fTL, col);
        addDashed(nTL, fTL, col);
        addDashed(nTR, fTR, col);
        addDashed(nBL, fBL, col);
        addDashed(nBR, fBR, col);
      } else {
        // Perspective: apex at the camera, lines to a far rectangle.
        float hH = std::tan(glm::radians(m.camFov) * 0.5f) * dist;
        float hW = hH * gizAspect;
        glm::vec3 fc = pos + fwd * dist;
        glm::vec3 fTL = fc - right * hW + up * hH,
                  fTR = fc + right * hW + up * hH;
        glm::vec3 fBL = fc - right * hW - up * hH,
                  fBR = fc + right * hW - up * hH;
        addDashed(pos, fTL, col);
        addDashed(pos, fTR, col);
        addDashed(pos, fBL, col);
        addDashed(pos, fBR, col);
        addDashed(fTL, fTR, col);
        addDashed(fTR, fBR, col);
        addDashed(fBR, fBL, col);
        addDashed(fBL, fTL, col);
      }
    } else if (m.isLight && m.lightType == 0) {
      // Directional light: dashed ray + small arrowhead along its direction.
      glm::vec3 pos = m.userPosition;
      glm::vec3 fwd = ForwardFromEuler(m.userRotation);
      glm::vec3 right, up;
      basis(fwd, right, up);
      glm::vec3 col = m.lightColor;
      glm::vec3 tip = pos + fwd * 2.0f;
      addDashed(pos, tip, col);
      glm::vec3 back = tip - fwd * 0.3f;
      addDashed(tip, back + right * 0.15f, col);
      addDashed(tip, back - right * 0.15f, col);
      addDashed(tip, back + up * 0.15f, col);
      addDashed(tip, back - up * 0.15f, col);
    }
  }

  if (verts.empty()) {
    gizmoVertexCount = 0;
    return;
  }

  VkDeviceSize bytes = sizeof(GV) * verts.size();
  if (bytes > gizmoCapacity) {
    if (gizmoVertexBuffer != VK_NULL_HANDLE) {
      vkDestroyBuffer(device, gizmoVertexBuffer, nullptr);
      vkFreeMemory(device, gizmoVertexMemory, nullptr);
      gizmoVertexBuffer = VK_NULL_HANDLE;
      gizmoVertexMemory = VK_NULL_HANDLE;
    }
    VkDeviceSize cap = bytes + 8192;
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = cap;
    bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bi, nullptr, &gizmoVertexBuffer) != VK_SUCCESS)
      return;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device, gizmoVertexBuffer, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = findMemoryType(
        mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device, &ai, nullptr, &gizmoVertexMemory) !=
        VK_SUCCESS)
      return;
    vkBindBufferMemory(device, gizmoVertexBuffer, gizmoVertexMemory, 0);
    gizmoCapacity = cap;
  }

  void *dst = nullptr;
  vkMapMemory(device, gizmoVertexMemory, 0, bytes, 0, &dst);
  std::memcpy(dst, verts.data(), (size_t)bytes);
  vkUnmapMemory(device, gizmoVertexMemory);
  gizmoVertexCount = (uint32_t)verts.size();

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gizmoPipeline);
  vkCmdPushConstants(cmd, gizmoPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                     sizeof(glm::mat4), glm::value_ptr(viewProj));
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(cmd, 0, 1, &gizmoVertexBuffer, offsets);
  vkCmdDraw(cmd, gizmoVertexCount, 1, 0, 0);
}

// Shared 3D world recording for both the editor view and the player-camera
// preview. Sprites and the 2D overlay are editor-only and handled elsewhere.
void SceneRenderer::RecordWorld(VkCommandBuffer cmd, const glm::mat4 &view3d,
                                const glm::mat4 &proj3d, bool drawGizmos) {
  glm::mat4 vp3d = proj3d * view3d;
  if (sunVisible && sun.pipeline != VK_NULL_HANDLE)
    DrawSun(cmd, vp3d);
  if (grid3dVisible && grid3d.pipeline != VK_NULL_HANDLE)
    DrawGrid3D(cmd, view3d, proj3d);
  if (!meshes3d.empty() && meshPipeline != VK_NULL_HANDLE)
    DrawMeshes(cmd, vp3d);
  if (drawGizmos && gizmoVisible && gizmoPipeline != VK_NULL_HANDLE)
    DrawGizmos(cmd, vp3d);
}

// ---------------------------------------------------------------------------
// Player-camera preview (second offscreen target, own render pass)
// ---------------------------------------------------------------------------

void SceneRenderer::CreatePreviewResources() {
  if (device == VK_NULL_HANDLE)
    return;
  const uint32_t W = (uint32_t)previewWidth, H = (uint32_t)previewHeight;

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = preview.format;
  imageInfo.extent = {W, H, 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  if (vkCreateImage(device, &imageInfo, nullptr, &preview.image) != VK_SUCCESS)
    throw std::runtime_error("Failed to create preview image!");
  VkMemoryRequirements mr;
  vkGetImageMemoryRequirements(device, preview.image, &mr);
  VkMemoryAllocateInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  ai.allocationSize = mr.size;
  ai.memoryTypeIndex =
      findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (vkAllocateMemory(device, &ai, nullptr, &preview.memory) != VK_SUCCESS)
    throw std::runtime_error("Failed to allocate preview memory!");
  vkBindImageMemory(device, preview.image, preview.memory, 0);

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = preview.image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = preview.format;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.layerCount = 1;
  if (vkCreateImageView(device, &viewInfo, nullptr, &preview.view) !=
      VK_SUCCESS)
    throw std::runtime_error("Failed to create preview view!");

  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
  if (vkCreateSampler(device, &samplerInfo, nullptr, &preview.sampler) !=
      VK_SUCCESS)
    throw std::runtime_error("Failed to create preview sampler!");

  // Depth
  VkImageCreateInfo depthInfo{};
  depthInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  depthInfo.imageType = VK_IMAGE_TYPE_2D;
  depthInfo.format = preview.depthFormat;
  depthInfo.extent = {W, H, 1};
  depthInfo.mipLevels = 1;
  depthInfo.arrayLayers = 1;
  depthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  if (vkCreateImage(device, &depthInfo, nullptr, &preview.depthImage) !=
      VK_SUCCESS)
    throw std::runtime_error("Failed to create preview depth image!");
  VkMemoryRequirements dmr;
  vkGetImageMemoryRequirements(device, preview.depthImage, &dmr);
  VkMemoryAllocateInfo dai{};
  dai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  dai.allocationSize = dmr.size;
  dai.memoryTypeIndex =
      findMemoryType(dmr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (vkAllocateMemory(device, &dai, nullptr, &preview.depthMemory) !=
      VK_SUCCESS)
    throw std::runtime_error("Failed to allocate preview depth memory!");
  vkBindImageMemory(device, preview.depthImage, preview.depthMemory, 0);

  VkImageViewCreateInfo dview{};
  dview.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  dview.image = preview.depthImage;
  dview.viewType = VK_IMAGE_VIEW_TYPE_2D;
  dview.format = preview.depthFormat;
  dview.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  dview.subresourceRange.levelCount = 1;
  dview.subresourceRange.layerCount = 1;
  if (vkCreateImageView(device, &dview, nullptr, &preview.depthView) !=
      VK_SUCCESS)
    throw std::runtime_error("Failed to create preview depth view!");

  // Own render pass (independent of the main offscreen, which resizes).
  VkAttachmentDescription attachments[2]{};
  attachments[0].format = preview.format;
  attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  attachments[1].format = preview.depthFormat;
  attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorRef{};
  colorRef.attachment = 0;
  colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  VkAttachmentReference depthRef{};
  depthRef.attachment = 1;
  depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorRef;
  subpass.pDepthStencilAttachment = &depthRef;
  VkRenderPassCreateInfo rpInfo{};
  rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rpInfo.attachmentCount = 2;
  rpInfo.pAttachments = attachments;
  rpInfo.subpassCount = 1;
  rpInfo.pSubpasses = &subpass;
  if (vkCreateRenderPass(device, &rpInfo, nullptr, &preview.renderPass) !=
      VK_SUCCESS)
    throw std::runtime_error("Failed to create preview render pass!");

  VkImageView fb[2] = {preview.view, preview.depthView};
  VkFramebufferCreateInfo fbInfo{};
  fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  fbInfo.renderPass = preview.renderPass;
  fbInfo.attachmentCount = 2;
  fbInfo.pAttachments = fb;
  fbInfo.width = W;
  fbInfo.height = H;
  fbInfo.layers = 1;
  if (vkCreateFramebuffer(device, &fbInfo, nullptr, &preview.framebuffer) !=
      VK_SUCCESS)
    throw std::runtime_error("Failed to create preview framebuffer!");

  preview.descriptorSet = ImGui_ImplVulkan_AddTexture(
      preview.sampler, preview.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  previewReady = true;
}

void SceneRenderer::DestroyPreviewResources() {
  if (device == VK_NULL_HANDLE)
    return;
  if (preview.framebuffer != VK_NULL_HANDLE)
    vkDestroyFramebuffer(device, preview.framebuffer, nullptr);
  if (preview.renderPass != VK_NULL_HANDLE)
    vkDestroyRenderPass(device, preview.renderPass, nullptr);
  if (preview.sampler != VK_NULL_HANDLE)
    vkDestroySampler(device, preview.sampler, nullptr);
  if (preview.view != VK_NULL_HANDLE)
    vkDestroyImageView(device, preview.view, nullptr);
  if (preview.image != VK_NULL_HANDLE)
    vkDestroyImage(device, preview.image, nullptr);
  if (preview.memory != VK_NULL_HANDLE)
    vkFreeMemory(device, preview.memory, nullptr);
  if (preview.depthView != VK_NULL_HANDLE)
    vkDestroyImageView(device, preview.depthView, nullptr);
  if (preview.depthImage != VK_NULL_HANDLE)
    vkDestroyImage(device, preview.depthImage, nullptr);
  if (preview.depthMemory != VK_NULL_HANDLE)
    vkFreeMemory(device, preview.depthMemory, nullptr);
  preview = Offscreen{};
  previewReady = false;
}

void SceneRenderer::RenderPlayerCameraPreview() {
  if (!previewReady || preview.framebuffer == VK_NULL_HANDLE)
    return;
  float aspect = (float)previewWidth / (float)previewHeight;
  glm::mat4 view, proj;
  if (!ComputePlayerCameraMatrices(aspect, view, proj))
    return;

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;
  VkCommandBuffer cmd;
  if (vkAllocateCommandBuffers(device, &allocInfo, &cmd) != VK_SUCCESS)
    return;

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &beginInfo);

  VkRenderPassBeginInfo rp{};
  rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rp.renderPass = preview.renderPass;
  rp.framebuffer = preview.framebuffer;
  rp.renderArea.offset = {0, 0};
  rp.renderArea.extent = {(uint32_t)previewWidth, (uint32_t)previewHeight};
  VkClearValue clears[2];
  clears[0].color = {{bgColor.x, bgColor.y, bgColor.z, bgColor.w}};
  clears[1].depthStencil = {1.0f, 0};
  rp.clearValueCount = 2;
  rp.pClearValues = clears;
  vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport vpRect{};
  vpRect.x = 0.0f;
  vpRect.y = 0.0f;
  vpRect.width = (float)previewWidth;
  vpRect.height = (float)previewHeight;
  vpRect.minDepth = 0.0f;
  vpRect.maxDepth = 1.0f;
  vkCmdSetViewport(cmd, 0, 1, &vpRect);
  VkRect2D sc{};
  sc.offset = {0, 0};
  sc.extent = {(uint32_t)previewWidth, (uint32_t)previewHeight};
  vkCmdSetScissor(cmd, 0, 1, &sc);

  RecordWorld(cmd, view, proj, false);

  vkCmdEndRenderPass(cmd);
  vkEndCommandBuffer(cmd);

  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;
  vkQueueSubmit(graphicsQueue, 1, &submit, VK_NULL_HANDLE);
  vkQueueWaitIdle(graphicsQueue);
  vkFreeCommandBuffers(device, commandPool, 1, &cmd);
}

VkDescriptorSet SceneRenderer::GetPlayerCameraPreviewDescriptor() const {
  return preview.descriptorSet;
}

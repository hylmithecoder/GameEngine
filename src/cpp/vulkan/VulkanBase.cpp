#include "../../include/vulkan/VulkanBase.hpp"
#include <algorithm>
#include <chrono>
#include <set>
#include <stdexcept>

namespace vkhandler {

VulkanBase::VulkanBase() {}

VulkanBase::~VulkanBase() { Cleanup(); }

bool VulkanBase::Init(const std::string &title, int width, int height) {
  windowWidth = width;
  windowHeight = height;

  // 1. Initialize SDL
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO)) {
    ::Log("SDL could not initialize! SDL Error: " + std::string(SDL_GetError()),
          Debug::LogLevel::CRASH);
    return false;
  }

  // 2. Create Window
  window = SDL_CreateWindow(title.c_str(), width, height,
                            SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  if (!window) {
    ::Log("Window could not be created! SDL Error: " +
              std::string(SDL_GetError()),
          Debug::LogLevel::CRASH);
    return false;
  }

  // 3. Initialize Vulkan core
  if (!CreateInstance())
    return false;
  if (!SDL_Vulkan_CreateSurface(window, ctx.instance, nullptr, &surface)) {
    ::Log("Failed to create surface", Debug::LogLevel::CRASH);
    return false;
  }
  if (!SelectPhysicalDevice())
    return false;
  if (!CreateLogicalDevice())
    return false;

  // 4. Create Swapchain and rendering resources
  if (!CreateSwapChain())
    return false;
  if (!CreateImageViews())
    return false;
  if (!CreateRenderPass())
    return false;
  if (!CreateFramebuffers())
    return false;
  if (!CreateCommandPool())
    return false;
  if (!CreateDescriptorPool())
    return false;
  if (!CreateSyncPrimitives())
    return false;

  // 5. Initialize ImGui
  if (!InitImGui())
    return false;

  OnInit();
  isRunning = true;
  return true;
}

void VulkanBase::Run() {
  auto lastTime = std::chrono::high_resolution_clock::now();

  while (isRunning) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) {
        isRunning = false;
      }
      if (event.type == SDL_EVENT_WINDOW_RESIZED) {
        RecreateSwapChain();
      }
    }

    auto currentTime = std::chrono::high_resolution_clock::now();
    float deltaTime =
        std::chrono::duration<float, std::chrono::seconds::period>(currentTime -
                                                                   lastTime)
            .count();
    lastTime = currentTime;

    OnUpdate(deltaTime);
    DrawFrame();
  }

  vkDeviceWaitIdle(ctx.device);
}

void VulkanBase::Cleanup() {
  OnCleanup();

  if (ctx.device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(ctx.device);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    CleanupSwapChain();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      vkDestroySemaphore(ctx.device, renderFinishedSemaphores[i], nullptr);
      vkDestroySemaphore(ctx.device, imageAvailableSemaphores[i], nullptr);
      vkDestroyFence(ctx.device, inFlightFences[i], nullptr);
    }

    vkDestroyDescriptorPool(ctx.device, ctx.descriptorPool, nullptr);
    vkDestroyCommandPool(ctx.device, ctx.commandPool, nullptr);
    vkDestroyDevice(ctx.device, nullptr);
  }

  if (ctx.instance != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(ctx.instance, surface, nullptr);
    vkDestroyInstance(ctx.instance, nullptr);
  }

  if (window) {
    SDL_DestroyWindow(window);
  }
  SDL_Quit();
}

bool VulkanBase::CreateInstance() {
  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "GameEngineSDL";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "IlmeeEngine";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_2;

  uint32_t extensionCount = 0;
  const char *const *sdlExtensions =
      SDL_Vulkan_GetInstanceExtensions(&extensionCount);
  std::vector<const char *> extensions(sdlExtensions,
                                       sdlExtensions + extensionCount);

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  VK_CHECK_RESULT(vkCreateInstance(&createInfo, nullptr, &ctx.instance));
  return true;
}

bool VulkanBase::SelectPhysicalDevice() {
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(ctx.instance, &deviceCount, nullptr);
  if (deviceCount == 0)
    return false;

  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(ctx.instance, &deviceCount, devices.data());

  // Simple selection: pick discrete GPU if available, else first one
  for (const auto &device : devices) {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(device, &properties);
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
      ctx.physicalDevice = device;
      break;
    }
  }
  if (ctx.physicalDevice == VK_NULL_HANDLE)
    ctx.physicalDevice = devices[0];

  // Find queue families
  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(ctx.physicalDevice,
                                           &queueFamilyCount, nullptr);
  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(
      ctx.physicalDevice, &queueFamilyCount, queueFamilies.data());

  for (uint32_t i = 0; i < queueFamilyCount; i++) {
    if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
      ctx.graphicsQueueFamily = i;
    VkBool32 presentSupport = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(ctx.physicalDevice, i, surface,
                                         &presentSupport);
    if (presentSupport)
      ctx.presentQueueFamily = i;
  }

  return true;
}

bool VulkanBase::CreateLogicalDevice() {
  std::set<uint32_t> uniqueQueueFamilies = {ctx.graphicsQueueFamily,
                                            ctx.presentQueueFamily};
  std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
  float queuePriority = 1.0f;

  for (uint32_t queueFamily : uniqueQueueFamilies) {
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    queueCreateInfos.push_back(queueCreateInfo);
  }

  VkPhysicalDeviceFeatures deviceFeatures{};
  std::vector<const char *> deviceExtensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME};

  VkDeviceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.queueCreateInfoCount =
      static_cast<uint32_t>(queueCreateInfos.size());
  createInfo.pQueueCreateInfos = queueCreateInfos.data();
  createInfo.pEnabledFeatures = &deviceFeatures;
  createInfo.enabledExtensionCount =
      static_cast<uint32_t>(deviceExtensions.size());
  createInfo.ppEnabledExtensionNames = deviceExtensions.data();

  VK_CHECK_RESULT(
      vkCreateDevice(ctx.physicalDevice, &createInfo, nullptr, &ctx.device));
  vkGetDeviceQueue(ctx.device, ctx.graphicsQueueFamily, 0, &ctx.graphicsQueue);
  vkGetDeviceQueue(ctx.device, ctx.presentQueueFamily, 0, &ctx.presentQueue);

  return true;
}

bool VulkanBase::CreateSwapChain() {
  VkSurfaceCapabilitiesKHR capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physicalDevice, surface,
                                            &capabilities);

  uint32_t formatCount;
  vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physicalDevice, surface,
                                       &formatCount, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physicalDevice, surface,
                                       &formatCount, formats.data());

  VkSurfaceFormatKHR surfaceFormat = formats[0];
  for (const auto &availableFormat : formats) {
    if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM &&
        availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      surfaceFormat = availableFormat;
      break;
    }
  }

  swapChainImageFormat = surfaceFormat.format;
  swapChainExtent = capabilities.currentExtent;
  if (swapChainExtent.width == 0xFFFFFFFF) {
    swapChainExtent.width =
        std::clamp((uint32_t)windowWidth, capabilities.minImageExtent.width,
                   capabilities.maxImageExtent.width);
    swapChainExtent.height =
        std::clamp((uint32_t)windowHeight, capabilities.minImageExtent.height,
                   capabilities.maxImageExtent.height);
  }

  uint32_t imageCount = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 &&
      imageCount > capabilities.maxImageCount) {
    imageCount = capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = surface;
  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = surfaceFormat.format;
  createInfo.imageColorSpace = surfaceFormat.colorSpace;
  createInfo.imageExtent = swapChainExtent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  uint32_t queueFamilyIndices[] = {ctx.graphicsQueueFamily,
                                   ctx.presentQueueFamily};
  if (ctx.graphicsQueueFamily != ctx.presentQueueFamily) {
    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    createInfo.queueFamilyIndexCount = 2;
    createInfo.pQueueFamilyIndices = queueFamilyIndices;
  } else {
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }

  createInfo.preTransform = capabilities.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  createInfo.clipped = VK_TRUE;

  VK_CHECK_RESULT(
      vkCreateSwapchainKHR(ctx.device, &createInfo, nullptr, &swapChain));

  vkGetSwapchainImagesKHR(ctx.device, swapChain, &imageCount, nullptr);
  swapChainImages.resize(imageCount);
  vkGetSwapchainImagesKHR(ctx.device, swapChain, &imageCount,
                          swapChainImages.data());

  return true;
}

bool VulkanBase::CreateImageViews() {
  swapChainImageViews.resize(swapChainImages.size());
  for (size_t i = 0; i < swapChainImages.size(); i++) {
    VkImageViewCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = swapChainImages[i];
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = swapChainImageFormat;
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.layerCount = 1;
    VK_CHECK_RESULT(vkCreateImageView(ctx.device, &createInfo, nullptr,
                                      &swapChainImageViews[i]));
  }
  return true;
}

bool VulkanBase::CreateRenderPass() {
  VkAttachmentDescription colorAttachment{};
  colorAttachment.format = swapChainImageFormat;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

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

  VK_CHECK_RESULT(
      vkCreateRenderPass(ctx.device, &renderPassInfo, nullptr, &renderPass));
  return true;
}

bool VulkanBase::CreateFramebuffers() {
  framebuffers.resize(swapChainImageViews.size());
  for (size_t i = 0; i < swapChainImageViews.size(); i++) {
    VkImageView attachments[] = {swapChainImageViews[i]};
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = swapChainExtent.width;
    framebufferInfo.height = swapChainExtent.height;
    framebufferInfo.layers = 1;
    VK_CHECK_RESULT(vkCreateFramebuffer(ctx.device, &framebufferInfo, nullptr,
                                        &framebuffers[i]));
  }
  return true;
}

bool VulkanBase::CreateCommandPool() {
  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.queueFamilyIndex = ctx.graphicsQueueFamily;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  VK_CHECK_RESULT(
      vkCreateCommandPool(ctx.device, &poolInfo, nullptr, &ctx.commandPool));
  return true;
}

bool VulkanBase::CreateDescriptorPool() {
  std::vector<VkDescriptorPoolSize> pool_sizes = {
      {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
      {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};
  VkDescriptorPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = 1000 * static_cast<uint32_t>(pool_sizes.size());
  pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
  pool_info.pPoolSizes = pool_sizes.data();
  VK_CHECK_RESULT(vkCreateDescriptorPool(ctx.device, &pool_info, nullptr,
                                         &ctx.descriptorPool));
  return true;
}

bool VulkanBase::CreateSyncPrimitives() {
  imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
  renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
  inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    VK_CHECK_RESULT(vkCreateSemaphore(ctx.device, &semaphoreInfo, nullptr,
                                      &imageAvailableSemaphores[i]));
    VK_CHECK_RESULT(vkCreateSemaphore(ctx.device, &semaphoreInfo, nullptr,
                                      &renderFinishedSemaphores[i]));
    VK_CHECK_RESULT(
        vkCreateFence(ctx.device, &fenceInfo, nullptr, &inFlightFences[i]));
  }
  return true;
}

bool VulkanBase::InitImGui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  ImGui_ImplSDL3_InitForVulkan(window);

  ImGui_ImplVulkan_InitInfo init_info = {};
  init_info.Instance = ctx.instance;
  init_info.PhysicalDevice = ctx.physicalDevice;
  init_info.Device = ctx.device;
  init_info.QueueFamily = ctx.graphicsQueueFamily;
  init_info.Queue = ctx.graphicsQueue;
  init_info.DescriptorPool = ctx.descriptorPool;
  init_info.MinImageCount = 2;
  init_info.ImageCount = static_cast<uint32_t>(swapChainImages.size());
  init_info.RenderPass = renderPass;

  ImGui_ImplVulkan_Init(&init_info);

  return true;
}

void VulkanBase::DrawFrame() {
  vkWaitForFences(ctx.device, 1, &inFlightFences[currentFrame], VK_TRUE,
                  UINT64_MAX);

  uint32_t imageIndex;
  VkResult result = vkAcquireNextImageKHR(
      ctx.device, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame],
      VK_NULL_HANDLE, &imageIndex);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    RecreateSwapChain();
    return;
  }

  vkResetFences(ctx.device, 1, &inFlightFences[currentFrame]);

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = ctx.commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  vkAllocateCommandBuffers(ctx.device, &allocInfo, &commandBuffer);

  // Start ImGui frame
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = renderPass;
  renderPassInfo.framebuffer = framebuffers[imageIndex];
  renderPassInfo.renderArea.offset = {0, 0};
  renderPassInfo.renderArea.extent = swapChainExtent;

  VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
  renderPassInfo.clearValueCount = 1;
  renderPassInfo.pClearValues = &clearColor;

  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo,
                       VK_SUBPASS_CONTENTS_INLINE);

  // Call implementation render hook
  OnRender(commandBuffer);

  // Render ImGui
  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

  vkCmdEndRenderPass(commandBuffer);
  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
  VkPipelineStageFlags waitStages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  VK_CHECK_RESULT(vkQueueSubmit(ctx.graphicsQueue, 1, &submitInfo,
                                inFlightFences[currentFrame]));

  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores;
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &swapChain;
  presentInfo.pImageIndices = &imageIndex;

  result = vkQueuePresentKHR(ctx.presentQueue, &presentInfo);

  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    RecreateSwapChain();
  }

  currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

  // Cleanup temporary command buffer (in a real engine we would reuse it)
  vkQueueWaitIdle(ctx.graphicsQueue);
  vkFreeCommandBuffers(ctx.device, ctx.commandPool, 1, &commandBuffer);
}

void VulkanBase::RecreateSwapChain() {
  int w, h;
  SDL_GetWindowSize(window, &w, &h);
  while (w == 0 || h == 0) {
    SDL_GetWindowSize(window, &w, &h);
    SDL_WaitEvent(nullptr);
  }

  vkDeviceWaitIdle(ctx.device);

  CleanupSwapChain();

  CreateSwapChain();
  CreateImageViews();
  CreateFramebuffers();

  OnResize(w, h);
}

void VulkanBase::CleanupSwapChain() {
  for (auto framebuffer : framebuffers) {
    vkDestroyFramebuffer(ctx.device, framebuffer, nullptr);
  }
  for (auto imageView : swapChainImageViews) {
    vkDestroyImageView(ctx.device, imageView, nullptr);
  }
  vkDestroySwapchainKHR(ctx.device, swapChain, nullptr);
}

VkCommandBuffer VulkanBase::BeginSingleTimeCommands() {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = ctx.commandPool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  vkAllocateCommandBuffers(ctx.device, &allocInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(commandBuffer, &beginInfo);
  return commandBuffer;
}

void VulkanBase::EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  vkQueueSubmit(ctx.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(ctx.graphicsQueue);

  vkFreeCommandBuffers(ctx.device, ctx.commandPool, 1, &commandBuffer);
}

uint32_t VulkanBase::FindMemoryType(uint32_t typeFilter,
                                    VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(ctx.physicalDevice, &memProperties);

  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags &
                                    properties) == properties) {
      return i;
    }
  }
  throw std::runtime_error("failed to find suitable memory type!");
}

VkPipeline VulkanBase::CreateGraphicsPipeline(const PipelineConfig &config,
                                              VkPipelineLayout layout) {
  VkShaderModule vertShader =
      vkhandler::tools::loadShader(config.vertShader.c_str(), ctx.device);
  VkShaderModule fragShader =
      vkhandler::tools::loadShader(config.fragShader.c_str(), ctx.device);

  VkPipelineShaderStageCreateInfo shaderStages[] = {
      vkhandler::initializers::pipelineShaderStageCreateInfo(
          vertShader, VK_SHADER_STAGE_VERTEX_BIT),
      vkhandler::initializers::pipelineShaderStageCreateInfo(
          fragShader, VK_SHADER_STAGE_FRAGMENT_BIT)};

  VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
  vertexInputInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.pVertexBindingDescriptions = &config.bindingDescription;
  vertexInputInfo.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(config.attributeDescriptions.size());
  vertexInputInfo.pVertexAttributeDescriptions =
      config.attributeDescriptions.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssembly =
      vkhandler::initializers::pipelineInputAssemblyStateCreateInfo(
          config.topology, 0, VK_FALSE);

  VkViewport viewport = vkhandler::initializers::viewport(
      (float)swapChainExtent.width, (float)swapChainExtent.height, 0.0f, 1.0f);
  VkRect2D scissor = vkhandler::initializers::rect2D(
      swapChainExtent.width, swapChainExtent.height, 0, 0);
  VkPipelineViewportStateCreateInfo viewportState =
      vkhandler::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
  viewportState.pViewports = &viewport;
  viewportState.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo rasterizer =
      vkhandler::initializers::pipelineRasterizationStateCreateInfo(
          config.polygonMode, config.cullMode, config.frontFace, 0);
  VkPipelineMultisampleStateCreateInfo multisampling =
      vkhandler::initializers::pipelineMultisampleStateCreateInfo(
          VK_SAMPLE_COUNT_1_BIT, 0);

  VkPipelineColorBlendAttachmentState colorBlendAttachment =
      vkhandler::initializers::pipelineColorBlendAttachmentState(
          VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
          VK_FALSE);
  VkPipelineColorBlendStateCreateInfo colorBlending =
      vkhandler::initializers::pipelineColorBlendStateCreateInfo(
          1, &colorBlendAttachment);

  VkPipelineDepthStencilStateCreateInfo depthStencil =
      vkhandler::initializers::pipelineDepthStencilStateCreateInfo(
          VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);

  VkGraphicsPipelineCreateInfo pipelineInfo = {};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = shaderStages;
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.pDepthStencilState = &depthStencil;
  pipelineInfo.layout = layout;
  pipelineInfo.renderPass = renderPass;
  pipelineInfo.subpass = 0;

  VkPipeline pipeline;
  VK_CHECK_RESULT(vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1,
                                            &pipelineInfo, nullptr, &pipeline));

  vkDestroyShaderModule(ctx.device, fragShader, nullptr);
  vkDestroyShaderModule(ctx.device, vertShader, nullptr);

  return pipeline;
}

} // namespace vkhandler

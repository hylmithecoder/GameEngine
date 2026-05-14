#include "../../include/vulkan/VkTools.hpp"
#include "imgui.h"
#define SDL_MAIN_HANDLED
#include "../../include/core_engine/Debugger.hpp"
#include "../../include/core_engine/test_viewport3d.hpp"

bool Viewport3D::IsExtensionAvailable(
    const ImVector<VkExtensionProperties> &properties, const char *extension) {
  for (const VkExtensionProperties &p : properties)
    if (strcmp(p.extensionName, extension) == 0)
      return true;
  return false;
}

void Viewport3D::SetupVulkan(ImVector<const char *> instance_extensions,
                             SDL_Window *currentWindow) {
  mainWindow = currentWindow;
  VkResult err;
#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
  volkInitialize();
#endif

  // Create Vulkan Instance
  {
    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

    // Enumerate available extensions
    uint32_t properties_count;
    ImVector<VkExtensionProperties> properties;
    vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
    properties.resize(properties_count);
    err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count,
                                                 properties.Data);
    VK_CHECK_RESULT(err);

    const char *validation_layer = "VK_LAYER_KHRONOS_validation";
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    bool validation_found = false;
    for (const auto &layer : available_layers) {
      if (strcmp(validation_layer, layer.layerName) == 0) {
        validation_found = true;
        break;
      }
    }

    if (validation_found) {
      instance_extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
      // Add validation layer
    }

    // Enable required extensions
    if (IsExtensionAvailable(
            properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
      instance_extensions.push_back(
          VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    if (IsExtensionAvailable(properties,
                             VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
      instance_extensions.push_back(
          VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
      create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
#endif

    // Enabling validation layers
#ifdef APP_USE_VULKAN_DEBUG_REPORT
    const char *layers[] = {"VK_LAYER_KHRONOS_validation"};
    create_info.enabledLayerCount = 1;
    create_info.ppEnabledLayerNames = layers;
    instance_extensions.push_back("VK_EXT_debug_report");
#endif

    // Create Vulkan Instance
    create_info.enabledExtensionCount = (uint32_t)instance_extensions.Size;
    create_info.ppEnabledExtensionNames = instance_extensions.Data;
    err = vkCreateInstance(&create_info, g_Allocator, &g_Instance);
    check_vk_result(err);
#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
    volkLoadInstance(g_Instance);
#endif

    // Setup the debug report callback
#ifdef APP_USE_VULKAN_DEBUG_REPORT
    auto f_vkCreateDebugReportCallbackEXT =
        (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(
            g_Instance, "vkCreateDebugReportCallbackEXT");
    IM_ASSERT(f_vkCreateDebugReportCallbackEXT != nullptr);
    VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
    debug_report_ci.sType =
        VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
    debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT |
                            VK_DEBUG_REPORT_WARNING_BIT_EXT |
                            VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
    debug_report_ci.pfnCallback = debug_report;
    debug_report_ci.pUserData = nullptr;
    err = f_vkCreateDebugReportCallbackEXT(g_Instance, &debug_report_ci,
                                           g_Allocator, &g_DebugReport);
    VK_CHECK_RESULT(err);
#endif
  }

  // Select Physical Device (GPU)
  g_PhysicalDevice = ImGui_ImplVulkanH_SelectPhysicalDevice(g_Instance);
  IM_ASSERT(g_PhysicalDevice != VK_NULL_HANDLE);

  // Select graphics queue family
  g_QueueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(g_PhysicalDevice);
  IM_ASSERT(g_QueueFamily != (uint32_t)-1);

  // Create Logical Device (with 1 queue)
  {
    ImVector<const char *> device_extensions;
    device_extensions.push_back("VK_KHR_swapchain");

    // Enumerate physical device extension
    uint32_t properties_count;
    ImVector<VkExtensionProperties> properties;
    vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr,
                                         &properties_count, nullptr);
    properties.resize(properties_count);
    vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr,
                                         &properties_count, properties.Data);
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
    if (IsExtensionAvailable(properties,
                             VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
      device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

    const float queue_priority[] = {1.0f};
    VkDeviceQueueCreateInfo queue_info[1] = {};
    queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info[0].queueFamilyIndex = g_QueueFamily;
    queue_info[0].queueCount = 1;
    queue_info[0].pQueuePriorities = queue_priority;
    VkDeviceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount =
        sizeof(queue_info) / sizeof(queue_info[0]);
    create_info.pQueueCreateInfos = queue_info;
    create_info.enabledExtensionCount = (uint32_t)device_extensions.Size;
    create_info.ppEnabledExtensionNames = device_extensions.Data;
    VK_CHECK_RESULT(
        vkCreateDevice(g_PhysicalDevice, &create_info, g_Allocator, &g_Device));
    vkGetDeviceQueue(g_Device, g_QueueFamily, 0, &g_Queue);
  }

  // Create Descriptor Pool
  // If you wish to load e.g. additional textures you may need to alter pools
  // sizes and maxSets.
  {
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
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
    pool_info.maxSets = 0;
    for (VkDescriptorPoolSize &pool_size : pool_sizes)
      pool_info.maxSets += pool_size.descriptorCount;
    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;
    VK_CHECK_RESULT(vkCreateDescriptorPool(g_Device, &pool_info, g_Allocator,
                                           &g_DescriptorPool));
  }
}

void Viewport3D::create_vk_surface() {
  VkResult err;
  if (SDL_Vulkan_CreateSurface(mainWindow, g_Instance, g_Allocator, &surface) ==
      0) {
    printf("Failed to create Vulkan surface.\n");
    return;
  } else {
    LogPointer("Vulkan surface created.", surface);
  }

  // if (!SDL_Vulkan_CreateSurface(mainWindow, g_Instance, g_Allocator,
  // &surface))
  // {
  //     printf("Failed to create Vulkan surface: %s\n", SDL_GetError());
  //     surface = VK_NULL_HANDLE;
  //     return;
  // }

  // Create Framebuffers
  int w, h;
  SDL_GetWindowSize(mainWindow, &w, &h);
  wd = &g_MainWindowData;
}

void Viewport3D::SetupVulkanWindow(ImGui_ImplVulkanH_Window *wd,
                                   VkSurfaceKHR surface, int width,
                                   int height) {
  viewportWidth = width;
  viewportHeight = height;
  wd->Surface = surface;

  // Check for WSI support
  VkBool32 res;
  vkGetPhysicalDeviceSurfaceSupportKHR(g_PhysicalDevice, g_QueueFamily,
                                       wd->Surface, &res);
  if (res != VK_TRUE) {
    fprintf(stderr, "Error no WSI support on physical device 0\n");
    exit(-1);
  }

  // Select Surface Format
  const VkFormat requestSurfaceImageFormat[] = {
      VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
      VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM};
  const VkColorSpaceKHR requestSurfaceColorSpace =
      VK_COLORSPACE_SRGB_NONLINEAR_KHR;
  wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
      g_PhysicalDevice, wd->Surface, requestSurfaceImageFormat,
      (size_t)IM_ARRAYSIZE(requestSurfaceImageFormat),
      requestSurfaceColorSpace);

  // Select Present Mode
#ifdef APP_USE_UNLIMITED_FRAME_RATE
  VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_MAILBOX_KHR,
                                      VK_PRESENT_MODE_IMMEDIATE_KHR,
                                      VK_PRESENT_MODE_FIFO_KHR};
#else
  VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_FIFO_KHR};
#endif
  wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
      g_PhysicalDevice, wd->Surface, &present_modes[0],
      IM_ARRAYSIZE(present_modes));
  // printf("[vulkan] Selected PresentMode = %d\n", wd->PresentMode);

  // Create SwapChain, RenderPass, Framebuffer, etc.
  IM_ASSERT(g_MinImageCount >= 2);
  ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device,
                                         wd, g_QueueFamily, g_Allocator, width,
                                         height, g_MinImageCount);
  SDL_SetWindowPosition(mainWindow, SDL_WINDOWPOS_CENTERED,
                        SDL_WINDOWPOS_CENTERED);
  SDL_ShowWindow(mainWindow);
}

#pragma region Setup ImGui
void Viewport3D::SetupImgui() {

  if (!gtk_init_check(0, nullptr)) {
    DEBUG_LOGF("Failed to initialize GTK", LogLevel::CRASH);
  }

  try {
    IMGUI_CHECKVERSION();
    DEBUG_LOG("IM Gui Version %s", IMGUI_VERSION);
    CreateContext();
    ImGuiIO &io = GetIO();
    (void)io;
    io.ConfigFlags |=
        ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |=
        ImGuiConfigFlags_NavEnableGamepad;            // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable Multi-Viewport
                                                        // / Platform Windows

    string fontPath = "assets/fonts/MiSans-Medium.ttf";
    fontSize = 24.0f;
    io.Fonts->AddFontFromFileTTF(fontPath.c_str(), fontSize);
    currentIo = getImGuiIO(io);
    // currentIo = io;

    ImGui_ImplSDL3_InitForVulkan(mainWindow);

    // On Wayland, SDL doesn't support the functions needed for multi-viewport
    // (SDL_GetGlobalMouseState, SDL_CaptureMouse). We check if the platform
    // supports viewports and disable it if not to prevent crashes. NOTE: To
    // enable multi-viewport on Wayland, run with: SDL_VIDEODRIVER=x11
    // ./ViewPort3D Rendering textures/triangles works fine without
    // ViewportsEnable - it only affects ImGui windows being able to be dragged
    // outside the main window.
    if ((io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports) == 0 &&
        (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) {
      DEBUG_LOG("Platform doesn't support multi-viewport (likely Wayland). "
                "Disabling ViewportsEnable.");
      DEBUG_LOG("TIP: Run with SDL_VIDEODRIVER=x11 ./ViewPort3D to enable "
                "multi-viewport on Wayland.");
      io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    }
    LogPointer("Surface", wd->Surface);
    // init_info.ApiVersion = VK_API_VERSION_1_3;              // Pass in your
    // value of VkApplicationInfo::apiVersion, otherwise will default to header
    // version.
    init_info.Instance = g_Instance;
    init_info.PhysicalDevice = g_PhysicalDevice;
    init_info.Device = g_Device;
    init_info.QueueFamily = g_QueueFamily;
    init_info.Queue = g_Queue;
    init_info.PipelineCache = g_PipelineCache;
    init_info.DescriptorPool = g_DescriptorPool;
    init_info.RenderPass = wd->RenderPass;
    init_info.Subpass = 0;
    init_info.MinImageCount = g_MinImageCount;
    init_info.ImageCount = wd->ImageCount;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = g_Allocator;
    init_info.CheckVkResultFn = check_vk_result;

    // init_info.Platform_CreateVkSurface = &SDL_Vulkan_CreateSurface;
    // init_info.
    LogPointer("Init Info: ", init_info.CheckVkResultFn);
    LogPointer("Surface ", surface);
    ImGui_ImplVulkan_Init(&init_info);
    ImGui_ImplVulkan_CreateFontsTexture();
  } catch (const exception &e) {
    DEBUG_LOGF("Failed to initialize GTK: %s", LogLevel::CRASH, e.what());
    MSGBOX_ERRORF(nullptr, "Failed to initialize GTK %s", e.what());
  }
}
#pragma endregion

int Viewport3D::ratePhysicalDevice(VkPhysicalDevice device) {

  vkGetPhysicalDeviceProperties(device, &deviceProps);
  vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

  // Print detailed device info for debugging
  DEBUG_LOG("\nEvaluating device: %s", deviceProps.deviceName);
  DEBUG_LOG("Device Type: %i", deviceProps.deviceType);
  DEBUG_LOG("Device ID: %i", deviceProps.deviceID);
  DEBUG_LOG("Vendor ID: %i", deviceProps.vendorID);
  DEBUG_LOG("Driver Version: %i", deviceProps.driverVersion);

  string fullInfo = "Device Name: " + string(deviceProps.deviceName) + "\n" +
                    "Device Type: " + to_string(deviceProps.deviceType) + "\n" +
                    "Device ID: " + to_string(deviceProps.deviceID) + "\n" +
                    "Vendor ID: " + to_string(deviceProps.vendorID) + "\n" +
                    "Driver Version: " + to_string(deviceProps.driverVersion);
  MSGBOX_INFOF(nullptr, "Full spec %s", fullInfo.c_str());
  tools::checkAllFeatures(deviceFeatures);

  int score = 0;

  // Check for compute capabilities
  // VkPhysicalDeviceMemoryProperties memoryProperties;
  vkGetPhysicalDeviceMemoryProperties(device, &memoryProperties);

  // Check queue families for compute support
  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
  vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount,
                                           queueFamilies.data());

  bool hasComputeSupport = false;
  for (const auto &queueFamily : queueFamilies) {
    if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) {
      hasComputeSupport = true;
      score += 500; // Bonus for compute support
      Log("Device has compute support: +500 points", LogLevel::INFO);
      // cout << "Device has compute support" << endl;
      break;
    }
  }

  // Score based on device type
  switch (deviceProps.deviceType) {
  case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
    score += 1000;
    Log("Discrete GPU: +1000 points", LogLevel::INFO);
    // cout << "Discrete GPU: +1000 points" << endl;
    break;
  case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
    score += 100;
    Log("Integrated GPU: +100 points", LogLevel::INFO);
    // cout << "Integrated GPU: +100 points" << endl;
    break;
  case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
    score += 50;
    Log("Virtual GPU: +50 points", LogLevel::INFO);
    // cout << "Virtual GPU: +50 points" << endl;
    break;
  default:
    cout << "Other GPU type: +0 points" << endl;
    break;
  }

  // Score based on memory size
  VkDeviceSize maxHeapSize = 0;
  for (uint32_t i = 0; i < memoryProperties.memoryHeapCount; i++) {
    if (memoryProperties.memoryHeaps[i].flags &
        VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
      maxHeapSize = max(maxHeapSize, memoryProperties.memoryHeaps[i].size);
    }
  }

  // Convert to GB and add to score
  score += static_cast<int>(maxHeapSize / (1024 * 1024 * 1024));
  Log("Memory size score: +" + to_string(maxHeapSize / (1024 * 1024 * 1024)) +
      " points");

  // Check for specific features
  if (deviceFeatures.geometryShader) {
    score += 100;
    Log("Has geometry shader: +100 points", LogLevel::INFO);
    // cout << "Has geometry shader: +100 points" << endl;
  }
  if (deviceFeatures.tessellationShader) {
    score += 100;
    Log("Has tessellation: +100 points", LogLevel::INFO);
    // cout << "Has tessellation: +100 points" << endl;
  }

  Log("Final score: %i", score);
  // cout << "Final score: " << score << endl;
  cout << "------------------------" << endl;

  return score;
}

void Viewport3D::pickPhysicalDevice() {
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(g_Instance, &deviceCount, nullptr);

  if (deviceCount == 0) {
    throw runtime_error("Failed to find GPUs with Vulkan support!");
  }

  vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(g_Instance, &deviceCount, devices.data());

  // Rate devices and pick the best one
  multimap<int, VkPhysicalDevice> candidates;

  for (const auto &device : devices) {
    int score = ratePhysicalDevice(device);
    candidates.insert(make_pair(score, device));
    // printDeviceProperties(device);
  }

  // Check if the best candidate is suitable
  if (candidates.rbegin()->first > 0) {
    // physicalDevice = candidates.rbegin()->second;
    VkPhysicalDeviceProperties deviceProps;
    vkGetPhysicalDeviceProperties(g_PhysicalDevice, &deviceProps);
    Log("Selected GPU: " + string(deviceProps.deviceName), LogLevel::INFO);
    // cout << "Selected GPU: " << deviceProps.deviceName << endl;
  } else {
    throw runtime_error("Failed to find a suitable GPU!");
  }
  VkPhysicalDeviceProperties deviceProps;
  vkGetPhysicalDeviceProperties(g_PhysicalDevice, &deviceProps);
  // cout << "Using GPU: " << deviceProps.deviceName << endl;
}

void Viewport3D::Events() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL3_ProcessEvent(&event);
    if (event.type == SDL_EVENT_QUIT)
      isRunning = true;
    else if (event.type == SDL_EVENT_KEY_DOWN) {
      if (event.key.key == SDLK_ESCAPE) {
        isRunning = true;
      }
    }
    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
        event.window.windowID == SDL_GetWindowID(mainWindow))
      isRunning = true;
  }
}

#pragma region Core update
void Viewport3D::Update(ImGuiIO &io) {
  while (!isRunning) {
    try {
      Events();
      if (SDL_GetWindowFlags(mainWindow) & SDL_WINDOW_MINIMIZED) {
        SDL_Delay(10);
        continue;
      }

      // Resize swap chain?
      int fb_width, fb_height;
      SDL_GetWindowSize(mainWindow, &fb_width, &fb_height);

      if (fb_width > 0 && fb_height > 0 &&
          (g_SwapChainRebuild || g_MainWindowData.Width != fb_width ||
           g_MainWindowData.Height != fb_height)) {
        ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);

        ImGui_ImplVulkanH_CreateOrResizeWindow(
            g_Instance, g_PhysicalDevice, g_Device, &g_MainWindowData,
            g_QueueFamily, g_Allocator, fb_width, fb_height, g_MinImageCount);
        g_MainWindowData.FrameIndex = 0;
        g_SwapChainRebuild = false;

        VK_CHECK_RESULT(vkDeviceWaitIdle(g_Device));
      }

      // Start the Dear ImGui frame
      ImGui_ImplVulkan_NewFrame();
      ImGui_ImplSDL3_NewFrame();
      NewFrame();

      ImGuiWindowFlags window_flags =
          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
          ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

      ImGuiViewport *viewport = GetMainViewport();
      SetNextWindowPos(viewport->Pos);
      SetNextWindowSize(viewport->Size);
      SetNextWindowViewport(viewport->ID);
      PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
      PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

      Begin("DockSpace", nullptr, window_flags);
      PopStyleVar(2);
      ImGuiID dockspace_id = GetID("DockSpace");

      DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

      RenderOffscreen(viewportWidth, viewportHeight);
      DrawImage();
      videoPlayerUI();
      helperRender();

      End();
      EndFrame();
      Render();

      ImDrawData *main_draw_data = GetDrawData();
      const bool main_is_minimized = (main_draw_data->DisplaySize.x <= 0.0f ||
                                      main_draw_data->DisplaySize.y <= 0.0f ||
                                      wd->Width == 0 || wd->Height == 0);
      if (!main_is_minimized) {
        // HandleRenderViewport(viewportWidth, viewportHeight);
        FrameRender(wd, main_draw_data);
        FramePresent(wd);
      }

      // Update and Render additional Platform Windows
      if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        UpdatePlatformWindows();
        RenderPlatformWindowsDefault();
      }
      vkDeviceWaitIdle(g_Device);

    } catch (const exception &e) {
      Log("Critical error in Update loop: " + string(e.what()),
          LogLevel::CRASH);

      // Try to recover
      // vkDeviceWaitIdle(g_Device);
      g_SwapChainRebuild = true;

      // Give the GPU a moment to recover
      SDL_Delay(100);
      continue;
    }
  }
}
#pragma endregion

void Viewport3D::FramePresent(ImGui_ImplVulkanH_Window *wd) {
  if (g_SwapChainRebuild)
    return;
  VkSemaphore render_complete_semaphore =
      wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
  VkPresentInfoKHR info = {};
  info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  info.waitSemaphoreCount = 1;
  info.pWaitSemaphores = &render_complete_semaphore;
  info.swapchainCount = 1;
  info.pSwapchains = &wd->Swapchain;
  info.pImageIndices = &wd->FrameIndex;
  VkResult err = vkQueuePresentKHR(g_Queue, &info);
  if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
    g_SwapChainRebuild = true;
  if (err == VK_ERROR_OUT_OF_DATE_KHR)
    return;
  if (err != VK_SUBOPTIMAL_KHR)
    VK_CHECK_RESULT(err);
  wd->SemaphoreIndex =
      (wd->SemaphoreIndex + 1) %
      wd->SemaphoreCount; // Now we can use the next set of semaphores
}

void Viewport3D::CleanupVulkan() {
  ImGui_ImplVulkanH_DestroyWindow(g_Instance, g_Device, &g_MainWindowData,
                                  g_Allocator);
  vkDestroyDescriptorPool(g_Device, g_DescriptorPool, g_Allocator);

#ifdef APP_USE_VULKAN_DEBUG_REPORT
  // Remove the debug report callback
  auto f_vkDestroyDebugReportCallbackEXT =
      (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(
          g_Instance, "vkDestroyDebugReportCallbackEXT");
  f_vkDestroyDebugReportCallbackEXT(g_Instance, g_DebugReport, g_Allocator);
#endif // APP_USE_VULKAN_DEBUG_REPORT

  vkDestroyDevice(g_Device, g_Allocator);
  vkDestroyInstance(g_Instance, g_Allocator);
  // vkDestroyBuffer(g_Device, vertexBuffer, nullptr);
  // vkFreeMemory(g_Device, vertexBufferMemory, nullptr);
  // vkDestroyBuffer(g_Device, uniformBuffer, nullptr);
  // vkFreeMemory(g_Device, uniformBufferMemory, nullptr);
  // vkDestroyDescriptorSetLayout(g_Device, uniformDescriptorSetLayout,
  // nullptr);
}

vector<char> Viewport3D::readFile(const string &filename) {
  ifstream file(filename, ios::ate | ios::binary);
  if (!file.is_open())
    throw runtime_error("Failed to open file: " + filename);
  size_t fileSize = (size_t)file.tellg();
  vector<char> buffer(fileSize);
  // cout << "Buffer data: " << (buffer.data(), fileSize) << endl;
  file.seekg(0);
  file.read(buffer.data(), fileSize);
  file.close();
  return buffer;
}

VkShaderModule Viewport3D::createShaderModule(const vector<char> &code) {
  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

  VkShaderModule shaderModule;
  if (vkCreateShaderModule(g_Device, &createInfo, nullptr, &shaderModule) !=
      VK_SUCCESS) {
    throw runtime_error("Failed to create shader module!");
  }

  // delete[] code.data();
  return shaderModule;
}

void Viewport3D::CreateOffscreenResources(int width, int height) {
  viewportWidth = width;
  viewportHeight = height;
  // Buat VkImage untuk warna
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  imageInfo.extent.width = viewportWidth;
  imageInfo.extent.height = viewportHeight;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  if (vkCreateImage(g_Device, &imageInfo, nullptr, &offscreenImage) !=
      VK_SUCCESS) {
    throw runtime_error("Failed to create offscreen image!");
  }

  VkMemoryRequirements memReq;
  vkGetImageMemoryRequirements(g_Device, offscreenImage, &memReq);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReq.size;
  allocInfo.memoryTypeIndex =
      0; // cari index memory yg sesuai (host visible/device local)

  vkAllocateMemory(g_Device, &allocInfo, nullptr, &offscreenImageMemory);
  vkBindImageMemory(g_Device, offscreenImage, offscreenImageMemory, 0);

  // Buat ImageView
  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = offscreenImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;
  vkCreateImageView(g_Device, &viewInfo, nullptr, &offscreenImageView);

  // Initial transition to SHADER_READ_ONLY_OPTIMAL to avoid UNDEFINED layout
  // when used as a texture
  VkCommandBuffer transitionCmd = BeginSingleTimeCommands();
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = offscreenImage;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  vkCmdPipelineBarrier(transitionCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);
  EndSingleTimeCommands(transitionCmd);

  // Create sampler
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;

  if (vkCreateSampler(g_Device, &samplerInfo, nullptr, &offscreenSampler) !=
      VK_SUCCESS) {
    throw runtime_error("failed to create texture sampler!");
  }
}

void Viewport3D::CreateOffscreenCommandResources() {
  // Command pool (transient / reset-able)
  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = g_QueueFamily; // atau graphicsQueueFamily
  if (vkCreateCommandPool(g_Device, &poolInfo, nullptr,
                          &offscreenCommandPool) != VK_SUCCESS)
    throw runtime_error("failed to create offscreen command pool");
  else
    Log("Offscreen command pool created", LogLevel::SUCCESS);
  // Semaphore to signal offscreen completion
  VkSemaphoreCreateInfo semInfo{};
  semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  if (vkCreateSemaphore(g_Device, &semInfo, nullptr,
                        &offscreenSignalSemaphore) != VK_SUCCESS)
    throw runtime_error("failed to create offscreen semaphore");
  else
    Log("Offscreen semaphore created", LogLevel::SUCCESS);
}

// Step 1 for pipeline
void Viewport3D::createRenderPass() {
  if (renderPass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(g_Device, renderPass, nullptr);
    renderPass = VK_NULL_HANDLE;
  }
  // Descriptors for the attachments used by this renderpass
  array<VkAttachmentDescription, 2> attachments{};

  // Color attachment
  attachments[0].format = VK_FORMAT_B8G8R8A8_UNORM; // Use the color format
                                                    // selected by the swapchain
  attachments[0].samples =
      VK_SAMPLE_COUNT_1_BIT; // We don't use multi sampling in this example
  attachments[0].loadOp =
      VK_ATTACHMENT_LOAD_OP_CLEAR; // Clear this attachment at the start of the
                                   // render pass
  attachments[0].storeOp =
      VK_ATTACHMENT_STORE_OP_STORE; // Keep its contents after the render pass
                                    // is finished (for displaying it)
  attachments[0].stencilLoadOp =
      VK_ATTACHMENT_LOAD_OP_DONT_CARE; // We don't use stencil, so don't care
                                       // for load
  attachments[0].stencilStoreOp =
      VK_ATTACHMENT_STORE_OP_DONT_CARE; // Same for store
  attachments[0].initialLayout =
      VK_IMAGE_LAYOUT_UNDEFINED; // Layout at render pass start. Initial doesn't
                                 // matter, so we use undefined
  attachments[0].finalLayout =
      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // Layout to which the attachment is
                                       // transitioned when the render pass is
                                       // finished As we want to present the
                                       // color buffer to the swapchain, we
                                       // transition to PRESENT_KHR
  // Depth attachment
  attachments[1].format =
      depthFormat; // A proper depth format is selected in the example base
  attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[1].loadOp =
      VK_ATTACHMENT_LOAD_OP_CLEAR; // Clear depth at start of first subpass
  attachments[1].storeOp =
      VK_ATTACHMENT_STORE_OP_DONT_CARE; // We don't need depth after render pass
                                        // has finished (DONT_CARE may result in
                                        // better performance)
  attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // No stencil
  attachments[1].stencilStoreOp =
      VK_ATTACHMENT_STORE_OP_DONT_CARE; // No Stencil
  attachments[1].initialLayout =
      VK_IMAGE_LAYOUT_UNDEFINED; // Layout at render pass start. Initial doesn't
                                 // matter, so we use undefined
  attachments[1].finalLayout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; // Transition to
                                                        // depth/stencil
                                                        // attachment

  // Setup attachment references
  VkAttachmentReference colorReference{};
  colorReference.attachment = 0; // Attachment 0 is color
  colorReference.layout =
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // Attachment layout used as
                                                // color during the subpass

  VkAttachmentReference depthReference{};
  depthReference.attachment = 1; // Attachment 1 is color
  depthReference.layout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; // Attachment used as
                                                        // depth/stencil used
                                                        // during the subpass

  // Setup a single subpass reference
  VkSubpassDescription subpassDescription{};
  subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpassDescription.colorAttachmentCount =
      1; // Subpass uses one color attachment
  subpassDescription.pColorAttachments =
      &colorReference; // Reference to the color attachment in slot 0
  subpassDescription.pDepthStencilAttachment =
      &depthReference; // Reference to the depth attachment in slot 1
  subpassDescription.inputAttachmentCount =
      0; // Input attachments can be used to sample from contents of a previous
         // subpass
  subpassDescription.pInputAttachments =
      nullptr; // (Input attachments not used by this example)
  subpassDescription.preserveAttachmentCount =
      0; // Preserved attachments can be used to loop (and preserve) attachments
         // through subpasses
  subpassDescription.pPreserveAttachments =
      nullptr; // (Preserve attachments not used by this example)
  subpassDescription.pResolveAttachments =
      nullptr; // Resolve attachments are resolved at the end of a sub pass and
               // can be used for e.g. multi sampling

  // Setup subpass dependencies
  // These will add the implicit attachment layout transitions specified by the
  // attachment descriptions The actual usage layout is preserved through the
  // layout specified in the attachment reference Each subpass dependency will
  // introduce a memory and execution dependency between the source and dest
  // subpass described by srcStageMask, dstStageMask, srcAccessMask,
  // dstAccessMask (and dependencyFlags is set) Note: VK_SUBPASS_EXTERNAL is a
  // special constant that refers to all commands executed outside of the actual
  // renderpass)
  array<VkSubpassDependency, 2> dependencies{};

  // Does the transition from final to initial layout for the depth an color
  // attachments Depth attachment
  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dependencies[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
  dependencies[0].dependencyFlags = 0;
  // Color attachment
  dependencies[1].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[1].dstSubpass = 0;
  dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[1].srcAccessMask = 0;
  dependencies[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
  dependencies[1].dependencyFlags = 0;

  // Create the actual renderpass
  VkRenderPassCreateInfo renderPassCI{};
  renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassCI.attachmentCount = static_cast<uint32_t>(
      attachments.size()); // Number of attachments used by this render pass
  renderPassCI.pAttachments =
      attachments
          .data(); // Descriptions of the attachments used by the render pass
  renderPassCI.subpassCount = 1; // We only use one subpass in this example
  renderPassCI.pSubpasses = &subpassDescription; // Description of that subpass
  renderPassCI.dependencyCount = static_cast<uint32_t>(
      dependencies.size()); // Number of subpass dependencies
  renderPassCI.pDependencies =
      dependencies.data(); // Subpass dependencies used by the render pass
  VK_CHECK_RESULT(
      vkCreateRenderPass(g_Device, &renderPassCI, nullptr, &renderPass));
  LogPointer("Device: ", g_Device, LogLevel::SUCCESS);
  LogPointer("Created render pass: ", renderPass, LogLevel::SUCCESS);
}

VkFormat Viewport3D::findDepthFormat() {
  std::vector<VkFormat> candidates = {VK_FORMAT_D32_SFLOAT,
                                      VK_FORMAT_D32_SFLOAT_S8_UINT,
                                      VK_FORMAT_D24_UNORM_S8_UINT};

  for (VkFormat format : candidates) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(g_PhysicalDevice, format, &props);

    if (props.optimalTilingFeatures &
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      return format;
    }
  }

  throw std::runtime_error("Failed to find supported depth format!");
}

void Viewport3D::CreateDepthStencil(uint32_t width, uint32_t height) {
  // Validate depth format
  // VkFormatProperties formatProperties;
  // vkGetPhysicalDeviceFormatProperties(g_PhysicalDevice, depthFormat,
  // &formatProperties); if (!(formatProperties.optimalTilingFeatures &
  // VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
  //     throw runtime_error("Selected depth format is not supported as a
  //     depth-stencil attachment!");
  // }

  // Create an optimal image used as the depth stencil attachment
  depthFormat = findDepthFormat();
  if (depthFormat == VK_FORMAT_UNDEFINED) {
    throw std::runtime_error("Depth format is UNDEFINED!");
  }
  VkImageCreateInfo imageCI{};
  imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCI.imageType = VK_IMAGE_TYPE_2D;
  imageCI.format = depthFormat;
  // Use example's height and width
  imageCI.extent = {width, height, 1};
  imageCI.mipLevels = 1;
  imageCI.arrayLayers = 1;
  imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
  imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VK_CHECK_RESULT(
      vkCreateImage(g_Device, &imageCI, nullptr, &depthStencil.image));

  // VkPhysicalDeviceMemoryProperties memoryProperties;
  vkGetPhysicalDeviceMemoryProperties(g_PhysicalDevice, &memoryProperties);
  // Allocate memory for the image (device local) and bind it to our image
  VkMemoryAllocateInfo memAlloc{};
  memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  VkMemoryRequirements memReqs;
  vkGetImageMemoryRequirements(g_Device, depthStencil.image, &memReqs);
  memAlloc.allocationSize = memReqs.size;
  memAlloc.memoryTypeIndex = FindMemoryType(
      memReqs, memoryProperties, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VK_CHECK_RESULT(
      vkAllocateMemory(g_Device, &memAlloc, nullptr, &depthStencil.memory));
  VK_CHECK_RESULT(
      vkBindImageMemory(g_Device, depthStencil.image, depthStencil.memory, 0));

  // Create a view for the depth stencil image
  // Images aren't directly accessed in Vulkan, but rather through views
  // described by a subresource range This allows for multiple views of one
  // image with differing ranges (e.g. for different layers)
  VkImageViewCreateInfo depthStencilViewCI{};
  depthStencilViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  depthStencilViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
  depthStencilViewCI.format = depthFormat;
  depthStencilViewCI.subresourceRange = {};
  depthStencilViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  // Stencil aspect should only be set on depth + stencil formats
  // (VK_FORMAT_D16_UNORM_S8_UINT..VK_FORMAT_D32_SFLOAT_S8_UINT)
  bool hasStencil = depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                    depthFormat == VK_FORMAT_D24_UNORM_S8_UINT;

  if (hasStencil) {
    depthStencilViewCI.subresourceRange.aspectMask |=
        VK_IMAGE_ASPECT_STENCIL_BIT;
  }
  depthStencilViewCI.subresourceRange.baseMipLevel = 0;
  depthStencilViewCI.subresourceRange.levelCount = 1;
  depthStencilViewCI.subresourceRange.baseArrayLayer = 0;
  depthStencilViewCI.subresourceRange.layerCount = 1;
  depthStencilViewCI.image = depthStencil.image;
  VK_CHECK_RESULT(vkCreateImageView(g_Device, &depthStencilViewCI, nullptr,
                                    &depthStencil.imageView));
}

void Viewport3D::CreateFrameBuffer(uint32_t width, uint32_t height) {
  MSGBOX_INFOF(NULL, "CreateFrameBuffer %d x %d", width, height);

  // Destroy existing framebuffers before recreating them
  for (auto framebuffer : framebuffers) {
    if (framebuffer != VK_NULL_HANDLE) {
      vkDestroyFramebuffer(g_Device, framebuffer, nullptr);
    }
  }
  framebuffers.clear();

  // Create a frame buffer for every image in the swapchain
  framebuffers.resize(images.size());
  Log("Framebuffer count: " + to_string(framebuffers.size()),
      LogLevel::WARNING);
  for (size_t i = 0; i < framebuffers.size(); i++) {
    DEBUG_LOG("Creating frame buffer %d", static_cast<int>(i));
    array<VkImageView, 2> attachments{};
    // Color attachment is the view of the swapchain image
    attachments[0] = imageViews[i];
    // Depth/Stencil attachment is the same for all frame buffers due to how
    // depth works with current GPUs
    attachments[1] = depthStencil.imageView;

    VkFramebufferCreateInfo frameBufferCI{};
    frameBufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    // All frame buffers use the same renderpass setup
    frameBufferCI.renderPass = renderPass;
    frameBufferCI.attachmentCount = static_cast<uint32_t>(attachments.size());
    frameBufferCI.pAttachments = attachments.data();
    frameBufferCI.width = width;
    frameBufferCI.height = height;
    frameBufferCI.layers = 1;
    cout << "Before creating Framebuffer: " << framebuffers[i] << endl;
    LogPointer("Before creating Framebuffer: ", framebuffers[i]);
    // Log("Creating framebuffer: " +
    // to_string(reinterpret_cast<uintptr_t>(framebuffers[i])),
    // LogLevel::SUCCESS); Create the framebuffer
    VK_CHECK_RESULT(vkCreateFramebuffer(g_Device, &frameBufferCI, nullptr,
                                        &framebuffers[i]));
    if (framebuffers[i] != VK_NULL_HANDLE) {
      LogPointer("Framebuffer: ", framebuffers[i], LogLevel::SUCCESS);
      // Log("Created framebuffer: " +
      // to_string(reinterpret_cast<uintptr_t>(framebuffers[i])),
      // LogLevel::SUCCESS);
    }
  }
}

#pragma region Setup Graphic pipelines
void Viewport3D::createGraphicsPipeline(const string &vertShaderPath,
                                        const string &fragShaderPath) {
  vector<char> vertShaderCode = readFile(vertShaderPath);
  vector<char> fragShaderCode = readFile(fragShaderPath);
  // Log("Vertshader size: " +
  // convertUintVariabletoString(vertShaderCode.capacity())); Log("fragshader
  // size: " + convertUintVariabletoString(fragShaderCode.capacity())); cout <<
  // "Vertshader code: " << vertShaderCode.capacity() << endl; cout <<
  // "fragshader code: " << fragShaderCode.capacity() << endl;
  VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
  VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);
  // Log("Vertshader module: " + convertUintVariabletoString(vertShaderModule));
  // Log("fragshader module: " + convertUintVariabletoString(fragShaderModule));
  // cout << "Vertshader module: " << vertShaderModule << endl;
  // cout << "fragshader module: " << fragShaderModule << endl;
  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = vertShaderModule;
  vertShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = fragShaderModule;
  fragShaderStageInfo.pName = "main";

  // example for implement an image to texture but 3D
  array<VkPipelineShaderStageCreateInfo, 2> quadShaders;

  // Quad Shaders
  quadShaders[0] =
      imageHandler.loadShader("assets/shaders/vulkan/templatetexture.vert.spv",
                              VK_SHADER_STAGE_VERTEX_BIT);
  quadShaders[1] =
      imageHandler.loadShader("assets/shaders/vulkan/templatetexture.frag.spv",
                              VK_SHADER_STAGE_FRAGMENT_BIT);

  // end example

  vector<VkPipelineShaderStageCreateInfo> shaderStages(2);
  shaderStages[0] = vertShaderStageInfo;
  shaderStages[1] = fragShaderStageInfo;
  // shaderStages[0] = quadShaders[0];
  // shaderStages[1] = quadShaders[1];

  int sizeShaderStages = shaderStages.size();
  // Update vertex input state
  vector<VkVertexInputBindingDescription> bindingDescription(1);
  bindingDescription[0].binding = 0;
  bindingDescription[0].stride =
      sizeof(Vertex); // 3 for pos + 3 for color + 3 for normal
  bindingDescription[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  // bindingDescription[0].binding = 0;
  // bindingDescription[0].stride = sizeof(TextureBase::Vertex);
  // bindingDescription[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  vector<VkVertexInputAttributeDescription> attributeDescriptions(2);

  // Position
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(Vertex, position);

  // Color
  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(Vertex, color);

  // Position texture
  // attributeDescriptions[0].binding = 0;
  // attributeDescriptions[0].location = 0;
  // attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  // attributeDescriptions[0].offset = offsetof(TextureBase::Vertex, pos);

  // // UV texture
  // attributeDescriptions[1].binding = 0;
  // attributeDescriptions[1].location = 1;
  // attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
  // attributeDescriptions[1].offset = offsetof(TextureBase::Vertex, uv);

  // // Normal UV
  // attributeDescriptions[2].binding = 0;
  // attributeDescriptions[2].location = 2;
  // attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
  // attributeDescriptions[2].offset = offsetof(TextureBase::Vertex, normal);

  Log("Input binding description size: %d", LogLevel::SUCCESS,
      bindingDescription.size());
  Log("Input attribute description: %d", LogLevel::SUCCESS,
      attributeDescriptions.size());

  VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
  depthStencilStateCI.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencilStateCI.depthTestEnable = VK_TRUE;
  depthStencilStateCI.depthWriteEnable = VK_TRUE;
  depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  depthStencilStateCI.depthBoundsTestEnable = VK_FALSE;
  depthStencilStateCI.back.failOp = VK_STENCIL_OP_KEEP;
  depthStencilStateCI.back.passOp = VK_STENCIL_OP_KEEP;
  depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;
  depthStencilStateCI.stencilTestEnable = VK_FALSE;
  depthStencilStateCI.front = depthStencilStateCI.back;

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount =
      static_cast<uint32_t>(bindingDescription.size());
  vertexInputInfo.pVertexBindingDescriptions = bindingDescription.data();
  vertexInputInfo.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(attributeDescriptions.size());
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssembly =
      vkhandler::initializers::pipelineInputAssemblyStateCreateInfo(
          VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = (float)g_MainWindowData.Width;
  viewport.height = (float)g_MainWindowData.Height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = {(uint32_t)g_MainWindowData.Width,
                    (uint32_t)g_MainWindowData.Height};

  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.pViewports = &viewport;
  viewportState.scissorCount = 1;
  viewportState.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo rasterizer =
      vkhandler::initializers::pipelineRasterizationStateCreateInfo(
          VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE,
          VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);

  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.pSampleMask = nullptr;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState blendAttachmentState{};
  blendAttachmentState.colorWriteMask = 0xf;
  blendAttachmentState.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &blendAttachmentState;

  // Update pipeline layout to include uniform buffer
  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.pNext = nullptr;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &uniformDescriptorSetLayout;

  vector<VkDynamicState> dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT,
                                                VK_DYNAMIC_STATE_SCISSOR};

  VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
  dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicStateInfo.pDynamicStates = dynamicStateEnables.data();
  dynamicStateInfo.dynamicStateCount =
      static_cast<uint32_t>(dynamicStateEnables.size());
  dynamicStateInfo.flags = 0;

  cout << pipelineLayoutInfo.pSetLayouts << "\n" << pipelineLayout << endl;

  if (vkCreatePipelineLayout(g_Device, &pipelineLayoutInfo, nullptr,
                             &pipelineLayout) != VK_SUCCESS) {
    throw runtime_error("Failed to create pipeline layout!");
  }

  VkGraphicsPipelineCreateInfo pipelineInfo = createPipeLineInfo(
      shaderStages.data(), vertexInputInfo, inputAssembly, viewportState,
      rasterizer, multisampling, colorBlending, depthStencilStateCI,
      dynamicStateInfo, sizeShaderStages);

  Log("Renderpass: " + to_string(reinterpret_cast<uintptr_t>(renderPass)));
  cout << "Pipeline info - Stages count: " << pipelineInfo.stageCount
       << ", Layout: " << reinterpret_cast<void *>(pipelineInfo.layout)
       << "\nRender Pass: " << pipelineInfo.renderPass << endl;
  Log("Pipeline info: " +
      to_string(reinterpret_cast<uintptr_t>(pipelineInfo.pStages)));
  cout << "Renderpass: " << renderPass << endl;
  cout << "Pipeline info: " << pipelineInfo.pStages << endl;

  VkPipelineCacheCreateInfo pipelineCacheCreateInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
  vkCreatePipelineCache(g_Device, &pipelineCacheCreateInfo, nullptr,
                        &g_PipelineCache);
  VK_CHECK_RESULT(vkCreateGraphicsPipelines(
      g_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline));
  // if (vkCreateGraphicsPipelines(g_Device, g_PipelineCache, 1, &pipelineInfo,
  // nullptr, &graphicsPipeline) != VK_SUCCESS) {
  //     throw runtime_error("Failed to create graphics pipeline!");
  // }
  // else {
  //     Log("Graphic Pipeline info: " +
  //     to_string(reinterpret_cast<uintptr_t>(graphicsPipeline)));
  //     // cout << "Graphic Pipeline: " << graphicsPipeline << endl;
  // }
  vkDestroyShaderModule(g_Device, fragShaderModule, nullptr);
  vkDestroyShaderModule(g_Device, vertShaderModule, nullptr);
}
#pragma endregion

void Viewport3D::createSwapChain(uint32_t &width, uint32_t &height, bool vsync,
                                 bool fullscreen) {
  // Store the current swap chain handle so we can use it later on to ease up
  // recreation
  VkSwapchainKHR oldSwapchain = swapChain;
  cout << "Old swapchain: " << oldSwapchain << endl;

  // Get physical device surface properties and formats
  VkSurfaceCapabilitiesKHR surfCaps;
  VK_CHECK_RESULT(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
      g_PhysicalDevice, surface, &surfCaps));

  VkExtent2D swapchainExtent = {};
  // If width (and height) equals the special value 0xFFFFFFFF, the size of the
  // surface will be set by the swapchain
  if (surfCaps.currentExtent.width == (uint32_t)-1) {
    // If the surface size is undefined, the size is set to the size of the
    // images requested
    swapchainExtent.width = width;
    swapchainExtent.height = height;
  } else {
    // If the surface size is defined, the swap chain size must match
    swapchainExtent = surfCaps.currentExtent;
    width = surfCaps.currentExtent.width;
    height = surfCaps.currentExtent.height;
  }

  // Select a present mode for the swapchain
  uint32_t presentModeCount;
  VK_CHECK_RESULT(vkGetPhysicalDeviceSurfacePresentModesKHR(
      g_PhysicalDevice, surface, &presentModeCount, NULL));
  assert(presentModeCount > 0);

  vector<VkPresentModeKHR> presentModes(presentModeCount);
  VK_CHECK_RESULT(vkGetPhysicalDeviceSurfacePresentModesKHR(
      g_PhysicalDevice, surface, &presentModeCount, presentModes.data()));

  // The VK_PRESENT_MODE_FIFO_KHR mode must always be present as per spec
  // This mode waits for the vertical blank ("v-sync")
  VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;

  // If v-sync is not requested, try to find a mailbox mode
  // It's the lowest latency non-tearing present mode available
  if (!vsync) {
    for (size_t i = 0; i < presentModeCount; i++) {
      if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
        swapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
        break;
      }
      if (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
        swapchainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
      }
    }
  }

  // Determine the number of images
  uint32_t desiredNumberOfSwapchainImages = surfCaps.minImageCount + 1;
  if ((surfCaps.maxImageCount > 0) &&
      (desiredNumberOfSwapchainImages > surfCaps.maxImageCount)) {
    desiredNumberOfSwapchainImages = surfCaps.maxImageCount;
  }

  // Find the transformation of the surface
  VkSurfaceTransformFlagsKHR preTransform;
  if (surfCaps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
    // We prefer a non-rotated transform
    preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  } else {
    preTransform = surfCaps.currentTransform;
  }

  // Find a supported composite alpha format (not all devices support alpha
  // opaque)
  VkCompositeAlphaFlagBitsKHR compositeAlpha =
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  // Simply select the first composite alpha format available
  vector<VkCompositeAlphaFlagBitsKHR> compositeAlphaFlags = {
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
  };
  for (auto &compositeAlphaFlag : compositeAlphaFlags) {
    if (surfCaps.supportedCompositeAlpha & compositeAlphaFlag) {
      compositeAlpha = compositeAlphaFlag;
      break;
    };
  }

  VkSwapchainCreateInfoKHR swapchainCI = {};
  swapchainCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchainCI.surface = surface;
  swapchainCI.minImageCount = desiredNumberOfSwapchainImages;

  uint32_t formatCount = 0;
  VK_CHECK_RESULT(vkGetPhysicalDeviceSurfaceFormatsKHR(
      g_PhysicalDevice, surface, &formatCount, nullptr));
  std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
  VK_CHECK_RESULT(vkGetPhysicalDeviceSurfaceFormatsKHR(
      g_PhysicalDevice, surface, &formatCount, surfaceFormats.data()));

  // Pilih format yang didukung
  VkSurfaceFormatKHR selectedFormat = surfaceFormats[0]; // fallback
  for (const auto &format : surfaceFormats) {
    if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
        format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      Log("Format selected");
      LogPointer("Selected format ", format.format);
      selectedFormat = format;
      break;
    }
  }

  colorFormat = selectedFormat.format;
  colorSpace = selectedFormat.colorSpace;
  swapchainCI.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
  swapchainCI.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
  swapchainCI.imageExtent = {swapchainExtent.width, swapchainExtent.height};
  // Set base usage
  swapchainCI.imageUsage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  // swapchainCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  // Conditional add
  // if (surfCaps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
  //     swapchainCI.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  // }
  // if (surfCaps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
  //     swapchainCI.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  // }
  // if (surfCaps.supportedUsageFlags & VK_IMAGE_USAGE_SAMPLED_BIT) {
  //     swapchainCI.imageUsage |= VK_IMAGE_USAGE_SAMPLED_BIT;
  // }

  swapchainCI.preTransform = (VkSurfaceTransformFlagBitsKHR)preTransform;
  swapchainCI.imageArrayLayers = 1;
  swapchainCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swapchainCI.queueFamilyIndexCount = 0;
  swapchainCI.presentMode = swapchainPresentMode;
  // Setting oldSwapChain to the saved handle of the previous swapchain aids in
  // resource reuse and makes sure that we can still present already acquired
  // images
  swapchainCI.oldSwapchain = oldSwapchain;
  // Setting clipped to VK_TRUE allows the implementation to discard rendering
  // outside of the surface area
  swapchainCI.clipped = VK_TRUE;
  swapchainCI.compositeAlpha = compositeAlpha;

  // Enable transfer source on swap chain images if supported
  if (surfCaps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
    swapchainCI.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  }

  // Enable transfer destination on swap chain images if supported
  if (surfCaps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
    swapchainCI.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }

  LogPointer("Global Device: ", g_Device);
  LogPointer("Swapchain Stype: ", swapchainCI.sType);
  LogPointer("Swapchain Format: ", swapchainCI.imageFormat);
  LogPointer("Swapchain space: ", swapchainCI.imageColorSpace);
  VK_CHECK_RESULT(
      vkCreateSwapchainKHR(g_Device, &swapchainCI, nullptr, &swapChain));
  // LogPointer("Swapchain Result: ", result);
  cout << "Created swap chain with " << desiredNumberOfSwapchainImages
       << " images\n"
          "swap chain details: "
       << swapChain << endl;
  // If an existing swap chain is re-created, destroy the old swap chain and the
  // ressources owned by the application (image views, images are owned by the
  // swap chain)
  if (oldSwapchain != VK_NULL_HANDLE) {
    for (auto i = 0; i < images.size(); i++) {
      vkDestroyImageView(g_Device, imageViews[i], nullptr);
    }
    vkDestroySwapchainKHR(g_Device, oldSwapchain, nullptr);
  }
  DEBUG_LOG("Image count: %i", imageCount);
  VK_CHECK_RESULT(
      vkGetSwapchainImagesKHR(g_Device, swapChain, &imageCount, nullptr));
  DEBUG_LOG("Image count: %i", imageCount);

  // Get the swap chain images
  images.resize(imageCount);
  VK_CHECK_RESULT(
      vkGetSwapchainImagesKHR(g_Device, swapChain, &imageCount, images.data()));
  DEBUG_LOG("Image data: %p", images.data());

  // Get the swap chain buffers containing the image and imageview
  imageViews.resize(imageCount);
  for (auto i = 0; i < images.size(); i++) {
    VkImageViewCreateInfo colorAttachmentView = {};
    colorAttachmentView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    colorAttachmentView.pNext = NULL;
    colorAttachmentView.format = colorFormat;
    colorAttachmentView.components = {
        VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B,
        VK_COMPONENT_SWIZZLE_A};
    colorAttachmentView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorAttachmentView.subresourceRange.baseMipLevel = 0;
    colorAttachmentView.subresourceRange.levelCount = 1;
    colorAttachmentView.subresourceRange.baseArrayLayer = 0;
    colorAttachmentView.subresourceRange.layerCount = 1;
    colorAttachmentView.viewType = VK_IMAGE_VIEW_TYPE_2D;
    colorAttachmentView.flags = 0;
    colorAttachmentView.image = images[i];
    cout << "Image: " << images[i] << endl;
    VK_CHECK_RESULT(vkCreateImageView(g_Device, &colorAttachmentView, nullptr,
                                      &imageViews[i]));
  }
}

void Viewport3D::CreateOffscreenPipeline() {
  Log("Create Offscreen pipeline !!!");
  // RenderPass minimal
  VkAttachmentDescription colorAttachment = createColorAttachment();

  Log("Color attachment format: " + to_string(colorAttachment.format));
  // cout << "Color attachment format: " << colorAttachment.format << endl;
  VkAttachmentReference colorRef{};
  colorRef.attachment = 0;
  colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorRef;

  VkRenderPassCreateInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = 1;
  renderPassInfo.pAttachments = &colorAttachment;
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;

  if (offscreenRenderPass == VK_NULL_HANDLE) {
    VK_CHECK_RESULT(vkCreateRenderPass(g_Device, &renderPassInfo, nullptr,
                                       &offscreenRenderPass));
  }

  // Framebuffer
  if (offscreenFramebuffer != VK_NULL_HANDLE) {
    vkDestroyFramebuffer(g_Device, offscreenFramebuffer, nullptr);
    offscreenFramebuffer = VK_NULL_HANDLE;
  }

  VkImageView attachments[] = {offscreenImageView};
  VkFramebufferCreateInfo fbInfo = createFrameBuffer(
      offscreenRenderPass, attachments, viewportWidth, viewportHeight);

  Log("Framebuffer width: " + to_string(fbInfo.width) +
      ", height: " + to_string(fbInfo.height));
  VK_CHECK_RESULT(
      vkCreateFramebuffer(g_Device, &fbInfo, nullptr, &offscreenFramebuffer));

  // TODO: load SPIR-V shaders & buat graphicsPipeline (binding vertex, input
  // layout, dsb)
}

void Viewport3D::createSynchronizationPrimitives() {
  // Fences are used to check draw command buffer completion on the host
  for (uint32_t i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
    VkFenceCreateInfo fenceCI{};
    fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    // Create the fences in signaled state (so we don't wait on first render of
    // each command buffer)
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    // Fence used to ensure that command buffer has completed exection before
    // using it again
    VK_CHECK_RESULT(vkCreateFence(g_Device, &fenceCI, nullptr, &waitFences[i]));
  }
  // Semaphores are used for correct command ordering within a queue
  // Used to ensure that image presentation is complete before starting to
  // submit again
  presentCompleteSemaphores.resize(MAX_CONCURRENT_FRAMES);
  for (auto &semaphore : presentCompleteSemaphores) {
    VkSemaphoreCreateInfo semaphoreCI{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK_CHECK_RESULT(
        vkCreateSemaphore(g_Device, &semaphoreCI, nullptr, &semaphore));
  }
  // Render completion
  // Semaphore used to ensure that all commands submitted have been finished
  // before submitting the image to the queue
  cout << "Count Image size: " << images.size() << endl;
  renderCompleteSemaphores.resize(images.size());
  for (auto &semaphore : renderCompleteSemaphores) {
    VkSemaphoreCreateInfo semaphoreCI{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK_CHECK_RESULT(
        vkCreateSemaphore(g_Device, &semaphoreCI, nullptr, &semaphore));
  }
}

vector<glm::mat4> Viewport3D::glm4Deserealize(const glm::mat4 &targetMat4) {
  vector<glm::mat4> currentGlm;
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      cout << targetMat4[i][j] << " ";
      currentGlm.push_back(targetMat4[i][j]);
      // cout << "Debuging: " << currentGlm.size();
    }
    cout << "\n";
  }
  cout << endl;
  return currentGlm;
}

#pragma region Handle Render Viewport

#pragma endregion
#pragma region Render Texture
void Viewport3D::HandleRenderTexture() {
  uint32_t imageIndex;

  try {
    // vkWaitForFences(g_Device, 1,
    // &textureHandler.waitFences[textureHandler.currentFrame], VK_TRUE,
    // UINT64_MAX); VK_CHECK_RESULT(vkResetFences(g_Device, 1,
    // &textureHandler.waitFences[textureHandler.currentFrame]));

    // VK_CHECK_RESULT(vkAcquireNextImageKHR(g_Device, swapChain, UINT64_MAX,
    // textureHandler.presentCompleteSemaphores[textureHandler.currentFrame],
    // VK_NULL_HANDLE, &imageIndex));

    // Update uniform buffers with current camera/view settings
    textureHandler.updateUniformBuffers(textureHandler.currentFrame);

    // Build the command buffer for texture rendering
    textureHandler.buildCommandBuffer();

    // Submit the command buffer
    // VkCommandBuffer cmdBuffer =
    // textureHandler.drawCmdBuffers[textureHandler.currentFrame];

    // VkPipelineStageFlags waitStageMask =
    // VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    // VkSubmitInfo submitInfo{};
    // submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    // submitInfo.commandBufferCount = 1;
    // submitInfo.pWaitDstStageMask = &waitStageMask;
    // submitInfo.pCommandBuffers = &cmdBuffer;

    // submitInfo.pWaitSemaphores =
    // &textureHandler.presentCompleteSemaphores[textureHandler.currentFrame];
    // submitInfo.waitSemaphoreCount = 1;
    // submitInfo.pSignalSemaphores =
    // &textureHandler.renderCompleteSemaphores[0];
    // submitInfo.signalSemaphoreCount = 1;

    // VkResult result = vkQueueSubmit(g_Queue, 1, &submitInfo,
    // textureHandler.waitFences[textureHandler.currentFrame]); if (result !=
    // VK_SUCCESS){
    //     Log("Failed to submit command buffer!", LogLevel::CRASH);
    //     return;
    // }

    // VkPresentInfoKHR presentInfo{};
    // presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    // presentInfo.waitSemaphoreCount = 1;
    // presentInfo.pWaitSemaphores =
    // &textureHandler.renderCompleteSemaphores[0]; presentInfo.swapchainCount =
    // 1; presentInfo.pSwapchains = &swapChain; presentInfo.pImageIndices =
    // &imageIndex; result = vkQueuePresentKHR(g_Queue, &presentInfo);

    // VK_CHECK_RESULT(vkQueueWaitIdle(g_Queue));
    // Create a fence for this submission if needed
    // VkFenceCreateInfo fenceInfo{};
    // fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    // VkFence renderFence;
    // vkCreateFence(g_Device, &fenceInfo, nullptr, &renderFence);

    // // Submit and wait
    // VK_CHECK_RESULT(vkQueueSubmit(g_Queue, 1, &submitInfo, renderFence));
    // VK_CHECK_RESULT(vkWaitForFences(g_Device, 1, &renderFence, VK_TRUE,
    // UINT64_MAX));

    // // Cleanup fence
    // vkDestroyFence(g_Device, renderFence, nullptr);

    // Advance frame for next render
    currentFrame = (currentFrame + 1) % MAX_CONCURRENT_FRAMES;
    // DEBUG_LOGF("Rendered texture to frame %u", LogLevel::INFO, currentFrame);
  } catch (const exception &e) {
    DEBUG_LOGF("HandleRenderTexture error: %s", LogLevel::CRASH, e.what());
  }
}
#pragma endregion

void Viewport3D::RenderOffscreen(uint32_t width, uint32_t height) {
  if (width == 0 || height == 0)
    return;
  // allocate command buffer (single-use)
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = offscreenCommandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer cmd = VK_NULL_HANDLE;
  vkAllocateCommandBuffers(g_Device, &allocInfo, &cmd);

  // Begin
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &beginInfo);

  // Transition image layout if needed is handled by render pass initialLayout =
  // UNDEFINED.
  VkClearValue clearValue{};
  clearValue.color = {{0.2f, 0.2f, 0.2f, 1.0f}};

  if (offscreenRenderPass == VK_NULL_HANDLE ||
      offscreenFramebuffer == VK_NULL_HANDLE) {
    DEBUG_LOGF(
        "Offscreen resources not initialized: RenderPass=%p, Framebuffer=%p",
        LogLevel::WARNING, (void *)offscreenRenderPass,
        (void *)offscreenFramebuffer);
    vkEndCommandBuffer(cmd);
    vkFreeCommandBuffers(g_Device, offscreenCommandPool, 1, &cmd);
    return;
  }

  VkRenderPassBeginInfo rpInfo = createRenderPassInfo(
      offscreenRenderPass, offscreenFramebuffer, width, height, clearValue);

  DEBUG_LOG("Starting offscreen render pass: RP=%p, FB=%p",
            (void *)offscreenRenderPass, (void *)offscreenFramebuffer);

  vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

  // Set dynamic states
  VkViewport viewport{};
  viewport.height = (float)height;
  viewport.width = (float)width;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(cmd, 0, 1, &viewport);

  VkRect2D scissor{};
  scissor.extent.width = width;
  scissor.extent.height = height;
  scissor.offset.x = 0;
  scissor.offset.y = 0;
  vkCmdSetScissor(cmd, 0, 1, &scissor);

  // Update uniform buffer data
  ShaderData shaderData{};
  shaderData.modelMatrix = glm::mat4(1.0f);
  shaderData.viewMatrix = camera.matrices.view;
  shaderData.projectionMatrix = camera.matrices.perspective;
  shaderData.projectionMatrix[1][1] *= -1; // Vulkan flip Y

  if (uniformBuffers[currentFrame].mapped != nullptr) {
    memcpy(uniformBuffers[currentFrame].mapped, &shaderData,
           sizeof(ShaderData));
  }

  // Bind pipeline and descriptor sets
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                          0, 1, &uniformBuffers[currentFrame].descriptorSet, 0,
                          nullptr);

  // Bind vertex and index buffers
  VkDeviceSize offsets[1]{0};
  vkCmdBindVertexBuffers(cmd, 0, 1, &vertices.buffer, offsets);
  vkCmdBindIndexBuffer(cmd, indices.buffer, 0, VK_INDEX_TYPE_UINT32);

  // Draw
  vkCmdDrawIndexed(cmd, indices.count, 1, 0, 0, 0);

  vkCmdEndRenderPass(cmd);

  // End
  vkEndCommandBuffer(cmd);

  // Submit offscreen command buffer
  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;
  submit.signalSemaphoreCount = 0;
  submit.pSignalSemaphores = nullptr;

  VkResult res = vkQueueSubmit(g_Queue, 1, &submit, VK_NULL_HANDLE);
  VK_CHECK_RESULT(res);

  // Wait for the queue to finish executing the command buffer before freeing
  // it. This is required because vkQueueSubmit is asynchronous, and freeing it
  // whilst it is in the pending state will cause a validation error
  // VUID-vkFreeCommandBuffers-pCommandBuffers-00047.
  vkQueueWaitIdle(g_Queue);

  vkFreeCommandBuffers(g_Device, offscreenCommandPool, 1, &cmd);
}

void Viewport3D::recreateOffscreenResources(uint32_t width, uint32_t height) {
  vkDeviceWaitIdle(g_Device);

  // Cleanup old resources (offscreenFramebuffer is handled in
  // CreateOffscreenPipeline)
  if (offscreenImageView != VK_NULL_HANDLE) {
    vkDestroyImageView(g_Device, offscreenImageView, nullptr);
  }
  if (offscreenImage != VK_NULL_HANDLE) {
    vkDestroyImage(g_Device, offscreenImage, nullptr);
  }
  if (offscreenImageMemory != VK_NULL_HANDLE) {
    vkFreeMemory(g_Device, offscreenImageMemory, nullptr);
  }

  // Recreate offscreen resources
  CreateOffscreenResources(width, height);
  CreateDepthStencil(width, height);
  createRenderPass(); // Recreate render pass to ensure depth format
                      // compatibility
  CreateFrameBuffer(width, height);
  CreateOffscreenPipeline();

  // Synchronize ImGui window resources with new dimensions
  if (wd != nullptr) {
    ImGui_ImplVulkanH_CreateOrResizeWindow(
        g_Instance, g_PhysicalDevice, g_Device, wd, g_QueueFamily, g_Allocator,
        width, height, g_MinImageCount);
  }

  // Synchronize textureHandler reference handles
  textureHandler.currentRenderPass = renderPass;
  textureHandler.currentFrameBuffers = framebuffers;
  textureHandler.width = width;
  textureHandler.height = height;

  // Recreate texture pipelines to ensure compatibility with the new RenderPass
  textureHandler.preparePipelines();
}

void Viewport3D::renderVideoFrame() {
  // Dalam render loop ImGui
  if (imageHandler.getVideoDescriptorSet() != VK_NULL_HANDLE) {
    float aspectRatio = static_cast<float>(imageHandler.width) /
                        static_cast<float>(imageHandler.height);

    // Hitung dimensi tampilan
    ImVec2 contentSize = GetContentRegionAvail();
    float displayWidth = contentSize.x;
    float displayHeight = displayWidth / aspectRatio;

    if (displayHeight > contentSize.y) {
      displayHeight = contentSize.y;
      displayWidth = displayHeight * aspectRatio;
    }

    // Pusatkan video di ruang yang tersedia
    float posX = (contentSize.x - displayWidth) * 0.5f;
    float posY = (contentSize.y - displayHeight) * 0.5f;

    SetCursorPos(ImVec2(posX, posY));

    float w = displayWidth, h = displayHeight;
    Image((ImTextureID)imageHandler.getVideoDescriptorSet(), ImVec2(w, h));
  } else {
    Text("No texture available");
  }
}

void Viewport3D::FrameRender(ImGui_ImplVulkanH_Window *wd,
                             ImDrawData *draw_data) {
  // Early exit if no draw data or zero area
  if (!draw_data || draw_data->CmdListsCount == 0 || wd->Width == 0 ||
      wd->Height == 0)
    return;

  try {
    VkResult err;

    // Get semaphores for current frame
    VkSemaphore image_acquired_semaphore =
        wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore render_complete_semaphore =
        wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;

    // Acquire next swapchain image
    uint32_t frame_idx = 0;
    err = vkAcquireNextImageKHR(g_Device, wd->Swapchain, UINT64_MAX,
                                image_acquired_semaphore, VK_NULL_HANDLE,
                                &frame_idx);

    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
      g_SwapChainRebuild = true;
      return;
    }
    VK_CHECK_RESULT(err);

    // Get frame data
    wd->FrameIndex = frame_idx;
    ImGui_ImplVulkanH_Frame *fd = &wd->Frames[frame_idx];

    // Wait for previous frame to complete
    err = vkWaitForFences(g_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
    VK_CHECK_RESULT(err);

    err = vkResetFences(g_Device, 1, &fd->Fence);
    VK_CHECK_RESULT(err);

    // Reset and begin command buffer
    err = vkResetCommandPool(g_Device, fd->CommandPool, 0);
    VK_CHECK_RESULT(err);

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    err = vkBeginCommandBuffer(fd->CommandBuffer, &begin_info);
    VK_CHECK_RESULT(err);

    // Begin render pass
    VkRenderPassBeginInfo rp_info = {};
    rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_info.renderPass = wd->RenderPass;
    rp_info.framebuffer = fd->Framebuffer;
    rp_info.renderArea.extent.width = wd->Width;
    rp_info.renderArea.extent.height = wd->Height;
    rp_info.clearValueCount = 1;
    rp_info.pClearValues = &wd->ClearValue;

    vkCmdBeginRenderPass(fd->CommandBuffer, &rp_info,
                         VK_SUBPASS_CONTENTS_INLINE);

    // Record ImGui Draw Data and Commands
    ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

    // End render pass
    vkCmdEndRenderPass(fd->CommandBuffer);
    err = vkEndCommandBuffer(fd->CommandBuffer);
    VK_CHECK_RESULT(err);

    // Submit command buffer
    VkPipelineStageFlags wait_stage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &image_acquired_semaphore;
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &fd->CommandBuffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &render_complete_semaphore;

    err = vkQueueSubmit(g_Queue, 1, &submit_info, fd->Fence);
    VK_CHECK_RESULT(err);

  } catch (const exception &e) {
    // Handle device lost error
    if (g_Device != VK_NULL_HANDLE) {
      vkDeviceWaitIdle(g_Device);
      g_SwapChainRebuild = true;
      Log("Device lost error: " + string(e.what()), LogLevel::CRASH);
    }
  }
}

uint32_t
Viewport3D::FindMemoryType(VkMemoryRequirements memRequirements,
                           VkPhysicalDeviceMemoryProperties memProperties,
                           VkMemoryPropertyFlags properties) {
  uint32_t memTypeIndex = UINT32_MAX;
  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    bool typeSupported = (memRequirements.memoryTypeBits & (1 << i));
    bool propertiesSupported =
        (memProperties.memoryTypes[i].propertyFlags & properties) == properties;

    if (typeSupported && propertiesSupported) {
      memTypeIndex = i;
      break;
    }
  }

  if (memTypeIndex == UINT32_MAX) {
    throw runtime_error("failed to find suitable memory type!");
  } else {
    Log("find suitable memory type: " + to_string(memTypeIndex),
        LogLevel::SUCCESS);
  }

  return memTypeIndex;
}

VkBuffer Viewport3D::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                  VkMemoryPropertyFlags properties,
                                  VkBuffer &buffer,
                                  VkDeviceMemory &bufferMemory) {
  // Create buffer
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(g_Device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
    throw runtime_error("failed to create buffer!");
  } else {
    Log("Buffer created successfully. Size: " + to_string(size) + " bytes",
        LogLevel::SUCCESS);
  }

  // Get memory requirements
  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(g_Device, buffer, &memRequirements);

  // Get memory properties BEFORE the loop
  // VkPhysicalDeviceMemoryProperties memoryProperties;
  // vkGetPhysicalDeviceMemoryProperties(g_PhysicalDevice, &memoryProperties);

  // Find suitable memory type
  uint32_t memTypeIndex =
      FindMemoryType(memRequirements, memoryProperties, properties);

  // Allocate memory
  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = memTypeIndex;

  if (vkAllocateMemory(g_Device, &allocInfo, nullptr, &bufferMemory) !=
      VK_SUCCESS) {
    throw runtime_error("failed to allocate buffer memory!");
  } else {
    Log("Allocate memory successfully. Size: " +
            to_string(allocInfo.allocationSize) + " bytes",
        LogLevel::SUCCESS);
  }

  // Bind buffer with allocated memory
  if (vkBindBufferMemory(g_Device, buffer, bufferMemory, 0) != VK_SUCCESS) {
    throw runtime_error("failed to bind buffer memory!");
  }

  return buffer;
}

void Viewport3D::createUniformBuffers() {
  VkDeviceSize bufferSize = sizeof(ShaderData);

  VkMemoryRequirements memReqs;

  // Vertex shader uniform buffer block
  VkBufferCreateInfo bufferInfo{};
  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.pNext = nullptr;
  allocInfo.allocationSize = 0;
  allocInfo.memoryTypeIndex = 0;

  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = sizeof(ShaderData);
  // This buffer will be used as a uniform buffer
  bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

  // VkPhysicalDeviceMemoryProperties memoryProperties;
  // vkGetPhysicalDeviceMemoryProperties(g_PhysicalDevice, &memoryProperties);

  for (uint32_t i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
    VK_CHECK_RESULT(vkCreateBuffer(g_Device, &bufferInfo, nullptr,
                                   &uniformBuffers[i].buffer));
    // Get memory requirements including size, alignment and memory type
    vkGetBufferMemoryRequirements(g_Device, uniformBuffers[i].buffer, &memReqs);
    allocInfo.allocationSize = memReqs.size;
    // Get the memory type index that supports host visible memory access
    // Most implementations offer multiple memory types and selecting the
    // correct one to allocate memory from is crucial We also want the buffer to
    // be host coherent so we don't have to flush (or sync after every update.
    // Note: This may affect performance so you might not want to do this in a
    // real world application that updates buffers on a regular base
    allocInfo.memoryTypeIndex =
        FindMemoryType(memReqs, memoryProperties,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    // Allocate memory for the uniform buffer
    VK_CHECK_RESULT(vkAllocateMemory(g_Device, &allocInfo, nullptr,
                                     &(uniformBuffers[i].memory)));
    // Bind memory to buffer
    VK_CHECK_RESULT(vkBindBufferMemory(g_Device, uniformBuffers[i].buffer,
                                       uniformBuffers[i].memory, 0));
    // We map the buffer once, so we can update it without having to map it
    // again
    VK_CHECK_RESULT(vkMapMemory(g_Device, uniformBuffers[i].memory, 0,
                                sizeof(ShaderData), 0,
                                (void **)&uniformBuffers[i].mapped));
  }

  CreateBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               uniformBuffer, uniformBufferMemory);
}

void Viewport3D::createCommandBuffers() {
  // All command buffers are allocated from a command pool
  VkCommandPoolCreateInfo commandPoolCI{};
  commandPoolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  commandPoolCI.queueFamilyIndex = g_QueueFamily;
  commandPoolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  VK_CHECK_RESULT(
      vkCreateCommandPool(g_Device, &commandPoolCI, nullptr, &commandPool));

  // Allocate one command buffer per max. concurrent frame from above pool
  VkCommandBufferAllocateInfo cmdBufAllocateInfo = commandBufferAllocateInfo(
      commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, MAX_CONCURRENT_FRAMES);
  VK_CHECK_RESULT(vkAllocateCommandBuffers(g_Device, &cmdBufAllocateInfo,
                                           commandBuffers.data()));
}

void Viewport3D::createUniformDescriptorSetLayout() {
  VkDescriptorSetLayoutBinding uboLayoutBinding{};
  // uboLayoutBinding.binding = 0;
  uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  uboLayoutBinding.descriptorCount = 1;
  uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  uboLayoutBinding.pImmutableSamplers = nullptr;

  VkDescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.pNext = nullptr;
  layoutInfo.bindingCount = 1;
  layoutInfo.pBindings = &uboLayoutBinding;

  if (vkCreateDescriptorSetLayout(g_Device, &layoutInfo, nullptr,
                                  &uniformDescriptorSetLayout) != VK_SUCCESS) {
    throw runtime_error("failed to create descriptor set layout!");
  }
}

void Viewport3D::createUniformDescriptorSets() {
  for (uint32_t i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = g_DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &uniformDescriptorSetLayout;

    VK_CHECK_RESULT(vkAllocateDescriptorSets(g_Device, &allocInfo,
                                             &uniformBuffers[i].descriptorSet));

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBuffers[i].buffer;
    // bufferInfo.offset = 0;
    bufferInfo.range = sizeof(ShaderData);

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = uniformBuffers[i].descriptorSet;
    descriptorWrite.dstBinding = 0;
    // descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(g_Device, 1, &descriptorWrite, 0, nullptr);
  }
}

// Add this method to create and fill vertex buffer
void Viewport3D::createVertexBuffer() {
  // Setup vertices
  vector<Vertex> vertexBuffer{{{1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
                              {{-1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
                              {{0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};
  uint32_t vertexBufferSize =
      static_cast<uint32_t>(vertexBuffer.size()) * sizeof(Vertex);

  // Create vertex buffer
  CreateBuffer(
      vertexBufferSize,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      vertices.buffer, // Store in class member
      vertices.memory  // Store in class member
  );

  // Create staging buffer for vertices
  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;
  CreateBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               stagingBuffer, stagingBufferMemory);

  // Copy vertex data to staging buffer
  void *data;
  vkMapMemory(g_Device, stagingBufferMemory, 0, vertexBufferSize, 0, &data);
  memcpy(data, vertexBuffer.data(), vertexBufferSize);
  vkUnmapMemory(g_Device, stagingBufferMemory);

  // Setup indices
  vector<uint32_t> indexBuffer{0, 1, 2};
  indices.count = static_cast<uint32_t>(indexBuffer.size());
  uint32_t indexBufferSize = indices.count * sizeof(uint32_t);

  // Create index buffer
  CreateBuffer(
      indexBufferSize,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      indices.buffer, // Store in class member
      indices.memory  // Store in class member
  );

  // Create staging buffer for indices
  VkBuffer indexStagingBuffer;
  VkDeviceMemory indexStagingBufferMemory;
  CreateBuffer(indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               indexStagingBuffer, indexStagingBufferMemory);

  // Copy index data to staging buffer
  vkMapMemory(g_Device, indexStagingBufferMemory, 0, indexBufferSize, 0, &data);
  memcpy(data, indexBuffer.data(), indexBufferSize);
  vkUnmapMemory(g_Device, indexStagingBufferMemory);

  // Transfer buffers
  VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

  // Copy vertices
  VkBufferCopy vertexCopy{};
  vertexCopy.size = vertexBufferSize;
  vkCmdCopyBuffer(commandBuffer, stagingBuffer, vertices.buffer, 1,
                  &vertexCopy);
  cout << "Vertice: " << vertices.buffer << endl;

  // Copy indices
  VkBufferCopy indexCopy{};
  indexCopy.size = indexBufferSize;
  vkCmdCopyBuffer(commandBuffer, indexStagingBuffer, indices.buffer, 1,
                  &indexCopy);
  Log("Indices count from createVertexBuffer: " + to_string(indices.count),
      LogLevel::WARNING);
  EndSingleTimeCommands(commandBuffer);

  // Cleanup staging buffers
  vkDestroyBuffer(g_Device, stagingBuffer, nullptr);
  vkFreeMemory(g_Device, stagingBufferMemory, nullptr);
  vkDestroyBuffer(g_Device, indexStagingBuffer, nullptr);
  vkFreeMemory(g_Device, indexStagingBufferMemory, nullptr);

  // Store vertex count
  vertexCount = static_cast<uint32_t>(vertexBuffer.size());
}

int main(int argc, char *argv[]) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
    cout << "Error: SDL_Init(): " << SDL_GetError() << endl;
    return -1;
  }

  float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  SDL_WindowFlags windowFlags =
      SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  SDL_Window *mainWindow =
      SDL_CreateWindow("Viewport 3D", 1280, 720, windowFlags);
  if (mainWindow == nullptr) {
    cout << "Error: SDL_CreateWindow(): " << SDL_GetError() << endl;
    return -1;
  }

  ImVector<const char *> extensions;
  {
    unsigned int sdl_extensions_count = 0;
    const char *const *sdl_extensions =
        SDL_Vulkan_GetInstanceExtensions(&sdl_extensions_count);
    for (unsigned int n = 0; n < sdl_extensions_count; n++) {
      DEBUG_LOG("SDL Extension %i: %s", n, sdl_extensions[n]);
      extensions.push_back(sdl_extensions[n]);
    }
  }

  try {
    Viewport3D viewport;
    viewport.textureHandler.convertToKtx(
        "assets/images/backgrounds/shiroko_bluearchive.jpg");
    NOTIF_INFO("Texture converted to KTX", "ILMEEE ENGINE");
    // Setup a default look-at camera
    viewport.camera.type = Camera::CameraType::lookat;
    viewport.camera.setPosition(glm::vec3(0.0f, 0.0f, -2.5f));
    viewport.camera.setRotation(glm::vec3(0.0f));
    viewport.camera.setPerspective(60.0f, (float)1280 / (float)720, 1.0f,
                                   256.0f);

    Log("Initializing Vulkan...");
    viewport.initVulkan(extensions, mainWindow);

    viewport.CreateDepthStencil(1280, 720);

    Log("Creating render pass...");
    viewport.createRenderPass();

    uint32_t swapWidth = 1280, swapHeight = 720;
    viewport.CreateOffscreenCommandResources();
    viewport.createVertexBuffer();
    viewport.createUniformBuffers();
    viewport.createUniformDescriptorSetLayout();
    viewport.helperInitImage();
    viewport.CreateOffscreenResources(1280, 720);

    viewport.textureHandler.setupRenderPassTexture();
    viewport.textureHandler.loadTexture(
        "assets/images/backgrounds/shiroko_bluearchive.ktx");
    viewport.textureHandler.generateQuad();
    viewport.textureHandler.prepareUniformBuffers();
    viewport.textureHandler.setupDescriptors();
    viewport.textureHandler.createCommandBuffers();
    viewport.textureHandler.createSynchronization();

    viewport.CreateOffscreenPipeline();

    Log("Creating uniform descriptor sets...");
    viewport.createUniformDescriptorSets();

    Log("Creating graphics pipeline...");
    try {
      viewport.createGraphicsPipeline(
          "assets/shaders/vulkan/triangle.vert.spv",
          "assets/shaders/vulkan/triangle.frag.spv");
      viewport.textureHandler.preparePipelines();
      MSGBOX_INFOF(nullptr, "PipeLine info: %p", viewport.graphicsPipeline);
    } catch (const exception e) {
      Log("PipeLine error: %s", LogLevel::CRASH, e.what());
      throw e;
    } catch (...) {
      Log("PipeLine error: %s", LogLevel::CRASH, "Unknown error");
      throw;
    }

    Log("Starting update loop...");
    viewport.Update(viewport.currentIo);

    // Cleanup
    viewport.CleanupVulkan();
  } catch (const exception &e) {
    Log("Exception caught: " + string(e.what()), LogLevel::CRASH);
    return -1;
  }

  SDL_DestroyWindow(mainWindow);
  SDL_Quit();
  return 0;
}

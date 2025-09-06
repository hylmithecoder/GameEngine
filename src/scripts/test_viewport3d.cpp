#define SDL_MAIN_HANDLED
#include <Debugger.hpp>
#include <test_viewport3d.hpp>

bool Viewport3D::IsExtensionAvailable(const ImVector<VkExtensionProperties>& properties, const char* extension)
{
    for (const VkExtensionProperties& p : properties)
        if (strcmp(p.extensionName, extension) == 0)
            return true;
    return false;
}

void Viewport3D::SetupVulkan(ImVector<const char*> instance_extensions, SDL_Window* currentWindow)
{
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
        err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.Data);
        check_vk_result(err);

        // Enable required extensions
        if (IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
            instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
        {
            instance_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
#endif

        // Enabling validation layers
#ifdef APP_USE_VULKAN_DEBUG_REPORT
        const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
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
        auto f_vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(g_Instance, "vkCreateDebugReportCallbackEXT");
        IM_ASSERT(f_vkCreateDebugReportCallbackEXT != nullptr);
        VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
        debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
        debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
        debug_report_ci.pfnCallback = debug_report;
        debug_report_ci.pUserData = nullptr;
        err = f_vkCreateDebugReportCallbackEXT(g_Instance, &debug_report_ci, g_Allocator, &g_DebugReport);
        check_vk_result(err);
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
        ImVector<const char*> device_extensions;
        device_extensions.push_back("VK_KHR_swapchain");

        // Enumerate physical device extension
        uint32_t properties_count;
        ImVector<VkExtensionProperties> properties;
        vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &properties_count, nullptr);
        properties.resize(properties_count);
        vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &properties_count, properties.Data);
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
        if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
            device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

        const float queue_priority[] = { 1.0f };
        VkDeviceQueueCreateInfo queue_info[1] = {};
        queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info[0].queueFamilyIndex = g_QueueFamily;
        queue_info[0].queueCount = 1;
        queue_info[0].pQueuePriorities = queue_priority;
        VkDeviceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
        create_info.pQueueCreateInfos = queue_info;
        create_info.enabledExtensionCount = (uint32_t)device_extensions.Size;
        create_info.ppEnabledExtensionNames = device_extensions.Data;
        check_vk_result(vkCreateDevice(g_PhysicalDevice, &create_info, g_Allocator, &g_Device));
        vkGetDeviceQueue(g_Device, g_QueueFamily, 0, &g_Queue);
    }

    // Create Descriptor Pool
    // If you wish to load e.g. additional textures you may need to alter pools sizes and maxSets.
    {
        VkDescriptorPoolSize pool_sizes[] =
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000;
        for (VkDescriptorPoolSize& pool_size : pool_sizes)
            pool_info.maxSets += pool_size.descriptorCount;
        pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        check_vk_result(vkCreateDescriptorPool(g_Device, &pool_info, g_Allocator, &g_DescriptorPool));
    }
}

void Viewport3D::create_vk_surface()
{
    VkResult err;
    if (SDL_Vulkan_CreateSurface(mainWindow, g_Instance, g_Allocator, &surface) == 0)
    {
        printf("Failed to create Vulkan surface.\n");
        return;
    }

    // Create Framebuffers
    int w, h;
    SDL_GetWindowSize(mainWindow, &w, &h);
    wd = &g_MainWindowData;
}

void Viewport3D::SetupVulkanWindow(ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int width, int height)
{
    wd->Surface = surface;

    // Check for WSI support
    VkBool32 res;
    vkGetPhysicalDeviceSurfaceSupportKHR(g_PhysicalDevice, g_QueueFamily, wd->Surface, &res);
    if (res != VK_TRUE)
    {
        fprintf(stderr, "Error no WSI support on physical device 0\n");
        exit(-1);
    }

    // Select Surface Format
    const VkFormat requestSurfaceImageFormat[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM };
    const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(g_PhysicalDevice, wd->Surface, requestSurfaceImageFormat, (size_t)IM_ARRAYSIZE(requestSurfaceImageFormat), requestSurfaceColorSpace);

    // Select Present Mode
#ifdef APP_USE_UNLIMITED_FRAME_RATE
    VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR };
#else
    VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_FIFO_KHR };
#endif
    wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(g_PhysicalDevice, wd->Surface, &present_modes[0], IM_ARRAYSIZE(present_modes));
    //printf("[vulkan] Selected PresentMode = %d\n", wd->PresentMode);

    // Create SwapChain, RenderPass, Framebuffer, etc.
    IM_ASSERT(g_MinImageCount >= 2);
    ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, wd, g_QueueFamily, g_Allocator, width, height, g_MinImageCount);
    SDL_SetWindowPosition(mainWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(mainWindow);
}

void Viewport3D::SetupImgui(){
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
    
    string fontPath = "assets/fonts/MiSans-Medium.ttf";
    io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 24.0f);
    currentIo = io;
    ImGui_ImplSDL3_InitForVulkan(mainWindow);
    // init_info.ApiVersion = VK_API_VERSION_1_3;              // Pass in your value of VkApplicationInfo::apiVersion, otherwise will default to header version.
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
    ImGui_ImplVulkan_Init(&init_info);
    ImGui_ImplVulkan_CreateFontsTexture();
}

int Viewport3D::ratePhysicalDevice(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties deviceProps;
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceProperties(device, &deviceProps);
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    // Print detailed device info for debugging
    Debug::Logger::Log("\nEvaluating device: " + string(deviceProps.deviceName), Debug::LogLevel::INFO);
    Debug::Logger::Log("Device Type: " + to_string(deviceProps.deviceType), Debug::LogLevel::INFO);
    Debug::Logger::Log("Device ID: " + to_string(deviceProps.deviceID), Debug::LogLevel::INFO);
    Debug::Logger::Log("Vendor ID: " + to_string(deviceProps.vendorID), Debug::LogLevel::INFO);
    Debug::Logger::Log("Driver Version: " + to_string(deviceProps.driverVersion), Debug::LogLevel::INFO);

    int score = 0;

    // Check for compute capabilities
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(device, &memProps);

    // Check queue families for compute support
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    bool hasComputeSupport = false;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) {
            hasComputeSupport = true;
            score += 500; // Bonus for compute support
            Debug::Logger::Log("Device has compute support: +500 points", Debug::LogLevel::INFO);
            // cout << "Device has compute support" << endl;
            break;
        }
    }

    // Score based on device type
    switch(deviceProps.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            score += 1000;
            Debug::Logger::Log("Discrete GPU: +1000 points", Debug::LogLevel::INFO);
            // cout << "Discrete GPU: +1000 points" << endl;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            score += 100;
            Debug::Logger::Log("Integrated GPU: +100 points", Debug::LogLevel::INFO);
            // cout << "Integrated GPU: +100 points" << endl;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            score += 50;
            Debug::Logger::Log("Virtual GPU: +50 points", Debug::LogLevel::INFO);
            // cout << "Virtual GPU: +50 points" << endl;
            break;
        default:
            cout << "Other GPU type: +0 points" << endl;
            break;
    }

    // Score based on memory size
    VkDeviceSize maxHeapSize = 0;
    for(uint32_t i = 0; i < memProps.memoryHeapCount; i++) {
        if(memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            maxHeapSize = max(maxHeapSize, memProps.memoryHeaps[i].size);
        }
    }
    
    // Convert to GB and add to score
    score += static_cast<int>(maxHeapSize / (1024 * 1024 * 1024));
    Debug::Logger::Log("Memory size score: +" + to_string(maxHeapSize / (1024 * 1024 * 1024)) + " points", Debug::LogLevel::INFO);
    // cout << "Memory size score: +" << (maxHeapSize / (1024 * 1024 * 1024)) << " points" << endl;

    // Check for specific features
    if (deviceFeatures.geometryShader) {
        score += 100;
        Debug::Logger::Log("Has geometry shader: +100 points", Debug::LogLevel::INFO);
        // cout << "Has geometry shader: +100 points" << endl;
    }
    if (deviceFeatures.tessellationShader) {
        score += 100;
        Debug::Logger::Log("Has tessellation: +100 points", Debug::LogLevel::INFO);
        // cout << "Has tessellation: +100 points" << endl;
    }

    Debug::Logger::Log("Final score: "+ to_string(score), Debug::LogLevel::INFO);
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
    
    for (const auto& device : devices) {
        int score = ratePhysicalDevice(device);
        candidates.insert(make_pair(score, device));
        // printDeviceProperties(device);
    }

    // Check if the best candidate is suitable
    if (candidates.rbegin()->first > 0) {
        // physicalDevice = candidates.rbegin()->second;
        VkPhysicalDeviceProperties deviceProps;
        vkGetPhysicalDeviceProperties(g_PhysicalDevice, &deviceProps);
        Debug::Logger::Log("Selected GPU: " + string(deviceProps.deviceName), Debug::LogLevel::INFO);
        // cout << "Selected GPU: " << deviceProps.deviceName << endl;
    } else {
        throw runtime_error("Failed to find a suitable GPU!");
    }
    VkPhysicalDeviceProperties deviceProps;
    vkGetPhysicalDeviceProperties(g_PhysicalDevice, &deviceProps);
    // cout << "Using GPU: " << deviceProps.deviceName << endl;
}

void Viewport3D::CreateOffscreenResources(int width, int height) {
    viewportWidth = width;
    viewportHeight = height;
    // Buat VkImage untuk warna
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    if (vkCreateImage(g_Device, &imageInfo, nullptr, &offscreenImage) != VK_SUCCESS) {
        throw runtime_error("Failed to create offscreen image!");
    }

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(g_Device, offscreenImage, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = 0; // cari index memory yg sesuai (host visible/device local)

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

    if (vkCreateSampler(g_Device, &samplerInfo, nullptr, &offscreenSampler) != VK_SUCCESS) {
        throw runtime_error("failed to create texture sampler!");
    }
}

void Viewport3D::CreateOffscreenCommandResources() {
    // Command pool (transient / reset-able)
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = g_QueueFamily; // atau graphicsQueueFamily
    if (vkCreateCommandPool(g_Device, &poolInfo, nullptr, &offscreenCommandPool) != VK_SUCCESS)
        throw runtime_error("failed to create offscreen command pool");
    else
        Logger::Log("Offscreen command pool created", LogLevel::SUCCESS);
    // Semaphore to signal offscreen completion
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(g_Device, &semInfo, nullptr, &offscreenSignalSemaphore) != VK_SUCCESS)
        throw runtime_error("failed to create offscreen semaphore");
    else 
        Logger::Log("Offscreen semaphore created", LogLevel::SUCCESS);
}

void Viewport3D::Events(){
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                isRunning = true;
            else if (event.type == SDL_EVENT_KEY_DOWN){
                if (event.key.key == SDLK_ESCAPE){
                    isRunning = true;
                }
             }
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(mainWindow))
                isRunning = true;
        }
}

void Viewport3D::Update(){
    bool show_demo_window = false;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    // bool done = false;
    while (!isRunning)
    {
        Events();
        // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your SDL_AppIterate() function]
        if (SDL_GetWindowFlags(mainWindow) & SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(10);
            continue;
        }

        // Resize swap chain?
        int fb_width, fb_height;
        SDL_GetWindowSize(mainWindow, &fb_width, &fb_height);

        if (fb_width > 0 && fb_height > 0 &&
            (g_SwapChainRebuild || g_MainWindowData.Width != fb_width || g_MainWindowData.Height != fb_height))
        {
            ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);

            ImGui_ImplVulkanH_CreateOrResizeWindow(
                g_Instance, g_PhysicalDevice, g_Device,
                &g_MainWindowData, g_QueueFamily, g_Allocator,
                fb_width, fb_height, g_MinImageCount
            );
            g_MainWindowData.FrameIndex = 0;
            g_SwapChainRebuild = false;

            // >>> Tambahan penting: rebuild pipeline/objects ImGui sesuai render pass baru
            check_vk_result(vkDeviceWaitIdle(g_Device));
        }

        // Start the Dear ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    
        ImGui::Begin("DockSpace", nullptr, window_flags);
        ImGui::PopStyleVar(2);
        ImGuiID dockspace_id = ImGui::GetID("DockSpace");

        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);
        {
            static float f = 0.0f;
            static int counter = 0;

            ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

            ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
            ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
            ImGui::Checkbox("Another Window", &show_another_window);

            ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
            ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

            if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
                counter++;
            ImGui::SameLine();
            ImGui::Text("counter = %d", counter);

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / currentIo.Framerate, currentIo.Framerate);
            ImGui::End();

            // DrawImguiViewport();
        }

        if (show_another_window)
        {
            ImGui::Begin("Another Window", &show_another_window);
            if (ImGui::Button("Close Me"))
                show_another_window = false;
            ImGui::End();
        }

        RenderOffscreen(viewportWidth, viewportHeight);
        DrawViewport3D();
        DrawImage();
        updateUniformBuffer();

        ImGui::End();
        ImGui::EndFrame();
        // Rendering
        ImGui::Render();
        ImDrawData* main_draw_data = ImGui::GetDrawData();
        const bool main_is_minimized = (main_draw_data->DisplaySize.x <= 0.0f || main_draw_data->DisplaySize.y <= 0.0f);
        wd->ClearValue.color.float32[0] = clear_color.x * clear_color.w;
        wd->ClearValue.color.float32[1] = clear_color.y * clear_color.w;
        wd->ClearValue.color.float32[2] = clear_color.z * clear_color.w;
        wd->ClearValue.color.float32[3] = clear_color.w;
        if (!main_is_minimized)
            FrameRender(wd, main_draw_data);
            // RenderOffscreen();

        // Update and Render additional Platform Windows
        // if (currentIo.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        // {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        // }

        // Present Main Platform Window
        if (!main_is_minimized)
            FramePresent(wd);

    }
}

void Viewport3D::FramePresent(ImGui_ImplVulkanH_Window* wd)
{
    if (g_SwapChainRebuild)
        return;
    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
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
        check_vk_result(err);
    wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount; // Now we can use the next set of semaphores
}

void Viewport3D::CleanupVulkan()
{
    ImGui_ImplVulkanH_DestroyWindow(g_Instance, g_Device, &g_MainWindowData, g_Allocator);
    vkDestroyDescriptorPool(g_Device, g_DescriptorPool, g_Allocator);

#ifdef APP_USE_VULKAN_DEBUG_REPORT
    // Remove the debug report callback
    auto f_vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(g_Instance, "vkDestroyDebugReportCallbackEXT");
    f_vkDestroyDebugReportCallbackEXT(g_Instance, g_DebugReport, g_Allocator);
#endif // APP_USE_VULKAN_DEBUG_REPORT

    vkDestroyDevice(g_Device, g_Allocator);
    vkDestroyInstance(g_Instance, g_Allocator);
    // vkDestroyBuffer(g_Device, uniformBuffer, nullptr);
    // vkFreeMemory(g_Device, uniformBufferMemory, nullptr);
    // vkDestroyDescriptorSetLayout(g_Device, uniformDescriptorSetLayout, nullptr);

}

vector<char> Viewport3D::readFile(const string& filename) {
    ifstream file(filename, ios::ate | ios::binary);
    if (!file.is_open()) throw runtime_error("Failed to open file: " + filename);
    size_t fileSize = (size_t)file.tellg();
    vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

VkShaderModule Viewport3D::createShaderModule(const vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(g_Device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw runtime_error("Failed to create shader module!");
    }
    return shaderModule;
}

// Step 1 for pipeline
void Viewport3D::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = g_MainWindowData.SurfaceFormat.format;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
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

    if (vkCreateRenderPass(g_Device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw runtime_error("Failed to create render pass!");
    }
}

// Parent Step2 for pipeline
void Viewport3D::createGraphicsPipeline(const string& vertShaderPath, const string& fragShaderPath) {
    auto vertShaderCode = readFile(vertShaderPath);
    auto fragShaderCode = readFile(fragShaderPath);
    Debug::Logger::Log("Vertshader size: " + convertUintVariabletoString(vertShaderCode.capacity()));
    Debug::Logger::Log("fragshader size: " + convertUintVariabletoString(fragShaderCode.capacity()));
    // cout << "Vertshader code: " << vertShaderCode.capacity() << endl;
    // cout << "fragshader code: " << fragShaderCode.capacity() << endl;
    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);
    Debug::Logger::Log("Vertshader module: " + convertUintVariabletoString(vertShaderModule));
    Debug::Logger::Log("fragshader module: " + convertUintVariabletoString(fragShaderModule));
    // cout << "Vertshader module: " << vertShaderModule << endl;
    // cout << "fragshader module: " << fragShaderModule << endl;
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    // Update vertex input state
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(float) * 9; // 3 for pos + 3 for color + 3 for normal
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
    
    // Position
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = 0;

    // Color
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = sizeof(float) * 3;

    // Normal
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[2].offset = sizeof(float) * 6;

    // Vertex input state (sementara kosong, nanti diisi dari model .obj)
    // VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    // vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;


    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)g_MainWindowData.Width;
    viewport.height = (float)g_MainWindowData.Height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = { (uint32_t)g_MainWindowData.Width, (uint32_t)g_MainWindowData.Height };

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    // pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    // Update pipeline layout to include uniform buffer
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &uniformDescriptorSetLayout;

    cout << pipelineLayoutInfo.pSetLayouts << "\n" << pipelineLayout << endl;
    if (vkCreatePipelineLayout(g_Device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw runtime_error("Failed to create pipeline layout!");
    }


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
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    Debug::Logger::Log("Renderpass: " + to_string(reinterpret_cast<uintptr_t>(renderPass)));
    Debug::Logger::Log("Pipeline info: " + to_string(reinterpret_cast<uintptr_t>(pipelineInfo.pStages)));
    // cout << "Renderpass: " << renderPass << endl;
    // cout << "Pipeline info: " << pipelineInfo.pStages << endl;

    if (vkCreateGraphicsPipelines(g_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
        throw runtime_error("Failed to create graphics pipeline!");
    }
    else {
        Debug::Logger::Log("Graphic Pipeline info: " + to_string(reinterpret_cast<uintptr_t>(graphicsPipeline)));
        // cout << "Graphic Pipeline: " << graphicsPipeline << endl;
    }
    vkDestroyShaderModule(g_Device, fragShaderModule, nullptr);
    vkDestroyShaderModule(g_Device, vertShaderModule, nullptr);
}

void Viewport3D::CreateOffscreenPipeline() {
    // RenderPass minimal
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    Debug::Logger::Log("Color attachment format: " + to_string(colorAttachment.format));
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

    vkCreateRenderPass(g_Device, &renderPassInfo, nullptr, &offscreenRenderPass);

    // Framebuffer
    VkImageView attachments[] = { offscreenImageView };
    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = offscreenRenderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = attachments;
    fbInfo.width = viewportWidth;
    fbInfo.height = viewportHeight;
    fbInfo.layers = 1;

    Debug::Logger::Log("Framebuffer width: " + to_string(fbInfo.width) + ", height: " + to_string(fbInfo.height));
    // Debug::Logger::Log("Framebuffer render pass: " + to_string(fbInfo.renderPass));
    // cout << "Framebuffer width: " << fbInfo.width << ", height: " << fbInfo.height << endl;
    // cout << "Framebuffer render pass: " << offscreenRenderPass << endl;
    vkCreateFramebuffer(g_Device, &fbInfo, nullptr, &offscreenFramebuffer);

    // TODO: load SPIR-V shaders & buat graphicsPipeline (binding vertex, input layout, dsb)
}

// void Viewport3D::RenderOffscreen() {
//     VkCommandBufferBeginInfo beginInfo{};
//     VkSemaphore image_acquired_semaphore  = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
//     VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
//     VkResult err = vkAcquireNextImageKHR(g_Device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);
//     beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
//     err = vkBeginCommandBuffer(offscreenCmdBuffer, &beginInfo);
//     check_vk_result(err);

//     VkClearValue clearValue = { {0.2f, 0.2f, 0.2f, 1.0f} };

//     VkRenderPassBeginInfo rpInfo{};
//     rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
//     rpInfo.renderPass = offscreenRenderPass;
//     rpInfo.framebuffer = offscreenFramebuffer;
//     rpInfo.renderArea.extent.width = 640;
//     rpInfo.renderArea.extent.height = 480;
//     rpInfo.clearValueCount = 1;
//     rpInfo.pClearValues = &clearValue;

//     vkCmdBeginRenderPass(offscreenCmdBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
//     vkCmdBindPipeline(offscreenCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

//     vkCmdDraw(offscreenCmdBuffer, 3, 1, 0, 0); // segitiga test

//     vkCmdEndRenderPass(offscreenCmdBuffer);
//     vkEndCommandBuffer(offscreenCmdBuffer);
// }

void Viewport3D::RenderOffscreen(uint32_t width, uint32_t height) {
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

    // Transition image layout if needed is handled by render pass initialLayout = UNDEFINED.
    VkClearValue clearValue{};
    clearValue.color = {{0.2f, 0.2f, 0.2f, 1.0f}};

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = offscreenRenderPass;
    rpInfo.framebuffer = offscreenFramebuffer;
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = { width, height };
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Bind pipeline & draw (segitiga)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);

    // End
    vkEndCommandBuffer(cmd);

    // Submit offscreen command buffer, signal offscreenSignalSemaphore when done
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &offscreenSignalSemaphore;

    VkResult res = vkQueueSubmit(g_Queue, 1, &submit, VK_NULL_HANDLE);
    check_vk_result(res);

    vkFreeCommandBuffers(g_Device, offscreenCommandPool, 1, &cmd);
}

// Draw viewport
void Viewport3D::DrawViewport3D() {
    static VkDescriptorSet viewportTexture = VK_NULL_HANDLE;
    
    // Initialize viewport texture descriptor set
    if (viewportTexture == VK_NULL_HANDLE) {
        // Create descriptor set for viewport texture
        viewportTexture = ImGui_ImplVulkan_AddTexture(
            offscreenSampler,
            offscreenImageView,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
    }

    ImGui::Begin("3D Viewport");
    ImGui::Text("This is a 3D viewport using Vulkan and ImGui.");
    ImGui::Image((ImTextureID)viewportTexture, ImVec2(640, 480));
    showCurrentCameraPosition();
    ImGui::End();
}

// Draw Image
void Viewport3D::DrawImage()
{
    static VkDescriptorSet imguiTexture = VK_NULL_HANDLE;
    if (imguiTexture == VK_NULL_HANDLE){
        imguiTexture = LoadImage("assets/images/backgrounds/Hanako_Swimsuit.png");
    }
    ImGui::Begin("Image Test");
    ImGui::Image((ImTextureID)imguiTexture, ImVec2(640, 340));
    ImGui::End();
}

VkDescriptorSet Viewport3D::LoadImage(const char* filename)
{
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
    if (vkCreateImage(g_Device, &imageInfo, nullptr, &textureImage) != VK_SUCCESS)
        throw runtime_error("Failed to create image!");
        
    Debug::Logger::Log("Texture image: "+to_string(reinterpret_cast<uintptr_t>(textureImage)));

    // Alokasi memory
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(g_Device, textureImage, &memRequirements);
    cout << "Mem requirements size: " << memRequirements.size << endl;
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;

    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(g_PhysicalDevice, &memProperties);
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
    if (vkAllocateMemory(g_Device, &allocInfo, nullptr, &textureMemory) != VK_SUCCESS)
        throw runtime_error("Failed to allocate image memory!");
    cout << "Allocinfo: " << allocInfo.allocationSize << endl;

    vkBindImageMemory(g_Device, textureImage, textureMemory, 0);

    // Copy pixel data langsung ke image
    VkImageSubresource subresource{};
    subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(g_Device, textureImage, &subresource, &layout);

    void* data;
    vkMapMemory(g_Device, textureMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(g_Device, textureMemory);

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
    if (vkCreateImageView(g_Device, &viewInfo, nullptr, &textureImageView) != VK_SUCCESS)
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
    if (vkCreateSampler(g_Device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS)
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

void Viewport3D::FrameRender(ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data)
{
    if (!draw_data || draw_data->CmdListsCount == 0)
        return; // Tidak ada yang dirender

    VkSemaphore image_acquired_semaphore  = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;

    uint32_t frame_idx = 0;
    VkResult err = vkAcquireNextImageKHR(g_Device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &frame_idx);
    if (err == VK_ERROR_OUT_OF_DATE_KHR) { g_SwapChainRebuild = true; return; }
    if (err == VK_SUBOPTIMAL_KHR) { g_SwapChainRebuild = true; }
    check_vk_result(err);

    wd->FrameIndex = frame_idx;
    ImGui_ImplVulkanH_Frame* fd = &wd->Frames[frame_idx];

    // Sinkronisasi
    check_vk_result(vkWaitForFences(g_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX));
    check_vk_result(vkResetFences(g_Device, 1, &fd->Fence));

    // Reset command buffer
    check_vk_result(vkResetCommandPool(g_Device, fd->CommandPool, 0));

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check_vk_result(vkBeginCommandBuffer(fd->CommandBuffer, &begin_info));

    // Mulai render pass
    VkRenderPassBeginInfo rp_info = {};
    rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_info.renderPass = wd->RenderPass;
    rp_info.framebuffer = fd->Framebuffer;
    rp_info.renderArea.extent.width = wd->Width;
    rp_info.renderArea.extent.height = wd->Height;
    rp_info.clearValueCount = 1;
    rp_info.pClearValues = &wd->ClearValue;

    vkCmdBeginRenderPass(fd->CommandBuffer, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

    // cout << "Draw Data: " << draw_data->CmdListsCount << endl;
    // cout << "FD Command Buffer: " << fd->CommandBuffer << endl;

    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->TextureId) {
                VkDescriptorSet ds = (VkDescriptorSet)pcmd->TextureId;
                offscreenDescriptorSet = ds;
                // printf("TextureId=%p\n", (void*)ds);
            }
        }
    }

    // Render ImGui
    ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

    vkCmdEndRenderPass(fd->CommandBuffer);
    check_vk_result(vkEndCommandBuffer(fd->CommandBuffer));

    // Submit
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &image_acquired_semaphore;
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &fd->CommandBuffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &render_complete_semaphore;

    check_vk_result(vkQueueSubmit(g_Queue, 1, &submit_info, fd->Fence));
}

VkBuffer Viewport3D::CreateBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer& buffer,
    VkDeviceMemory& bufferMemory
) {
    // Create buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(g_Device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer!");
    }

    // Get memory requirements
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(g_Device, buffer, &memRequirements);

    // Allocate memory
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    uint32_t memTypeIndex = UINT32_MAX;
    VkPhysicalDeviceMemoryProperties memProperties;
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

    if (vkAllocateMemory(g_Device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate buffer memory!");
    }

    // Bind buffer with allocated memory
    vkBindBufferMemory(g_Device, buffer, bufferMemory, 0);
    return buffer;
}

void Viewport3D::createUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    CreateBuffer(
        bufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        uniformBuffer,
        uniformBufferMemory
    );
}

void Viewport3D::createUniformDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    if (vkCreateDescriptorSetLayout(g_Device, &layoutInfo, nullptr, &uniformDescriptorSetLayout) != VK_SUCCESS) {
        throw runtime_error("failed to create descriptor set layout!");
    }
}

void Viewport3D::createUniformDescriptorSets() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = g_DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &uniformDescriptorSetLayout;

    if (vkAllocateDescriptorSets(g_Device, &allocInfo, &uniformDescriptorSet) != VK_SUCCESS) {
        throw runtime_error("failed to allocate descriptor sets!");
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformBufferObject);

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = uniformDescriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(g_Device, 1, &descriptorWrite, 0, nullptr);
}

void Viewport3D::updateUniformBuffer() {
    static auto startTime = chrono::high_resolution_clock::now();
    // Logger::Log("Start time: "+ to_string(startTime.time_since_epoch().count()), LogLevel::WARNING);
    auto currentTime = chrono::high_resolution_clock::now();
    float time = chrono::duration<float, chrono::seconds::period>
        (currentTime - startTime).count();

    UniformBufferObject ubo{};
    // Rotate model
    ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), 
        glm::vec3(0.0f, 0.0f, 1.0f));
    
    // View matrix from camera
    ubo.view = camera->GetViewMatrix();
    
    // Projection matrix
    ubo.proj = glm::perspective(glm::radians(45.0f), 
        viewportWidth / (float) viewportHeight, 0.1f, 100.0f);
    
    // Vulkan NDC uses different coordinate system
    ubo.proj[1][1] *= -1;

    // Copy to uniform buffer
    void* data;
    vkMapMemory(g_Device, uniformBufferMemory, 0, sizeof(ubo), 0, &data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(g_Device, uniformBufferMemory);
}

int main(int argc, char* argv[]){
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO))
    {
        cout << "Error: SDL_Init(): " << SDL_GetError() << endl;
        return -1;
    }

    float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    SDL_WindowFlags windowFlags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* mainWindow =  SDL_CreateWindow("Viewport 3D", 1280, 720, windowFlags);
    if (mainWindow == nullptr)
    {
        cout << "Error: SDL_CreateWindow(): "<<  SDL_GetError() << endl;
        return -1;
    }

    ImVector<const char*> extensions;
    {
        unsigned int sdl_extensions_count = 0;
        const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extensions_count);
        for (unsigned int n = 0; n < sdl_extensions_count; n++)
            extensions.push_back(sdl_extensions[n]);
    }

    Viewport3D viewport;
    viewport.initVulkan(extensions, mainWindow);
    Logger::Log("Create Shaders & Pipeline");
    viewport.CreateOffscreenCommandResources();
    viewport.CreateOffscreenResources(1280, 720);
    viewport.CreateOffscreenPipeline();
    viewport.createRenderPass();

    viewport.createUniformDescriptorSetLayout();
    viewport.createGraphicsPipeline("assets/shaders/vulkan/shader.vert.spv", "assets/shaders/vulkan/shader.frag.spv");
    
    viewport.createUniformBuffers();
    viewport.createUniformDescriptorSets();
    // viewport.camera->GetViewMatrix();
    viewport.Update();
    viewport.CleanupVulkan();
    // viewport.initVulkan(mainWindow);

    SDL_DestroyWindow(viewport.mainWindow);
    SDL_Quit();
    return 0;
}
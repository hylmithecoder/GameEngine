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

        const char* validation_layer = "VK_LAYER_KHRONOS_validation";
        uint32_t layer_count;
        vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
        std::vector<VkLayerProperties> available_layers(layer_count);
        vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());
        
        bool validation_found = false;
        for (const auto& layer : available_layers) {
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
    fontSize = 24.0f;
    io.Fonts->AddFontFromFileTTF(fontPath.c_str(), fontSize);
    currentIo = getImGuiIO(io);
    // currentIo = io;
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

void Viewport3D::Update(ImGuiIO& io){
    bool show_demo_window = false;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    // bool done = false;
    while (!isRunning)
    {
        try {
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

            if (fb_width > 0 && fb_height > 0 && (g_SwapChainRebuild || g_MainWindowData.Width != fb_width || g_MainWindowData.Height != fb_height))
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

                ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
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
            try {
                HandleRenderViewport(viewportWidth, viewportHeight);
            } catch (const exception& e){
                Logger::Log("Error in HandleRenderViewport: " + string(e.what()), LogLevel::CRASH);
            }
            DrawViewport3D();
            DrawImage();
            videoPlayerUI();
            // updateUniformBuffer();

            ImGui::End();
            ImGui::EndFrame();
            // Rendering
            ImGui::Render();

            ImDrawData* main_draw_data = ImGui::GetDrawData();
            const bool main_is_minimized = (main_draw_data->DisplaySize.x <= 0.0f || main_draw_data->DisplaySize.y <= 0.0f);
            // wd->ClearValue.color.float32[0] = clear_color.x * clear_color.w;
            // wd->ClearValue.color.float32[1] = clear_color.y * clear_color.w;
            // wd->ClearValue.color.float32[2] = clear_color.z * clear_color.w;
            // wd->ClearValue.color.float32[3] = clear_color.w;
            if (!main_is_minimized)
                FrameRender(wd, main_draw_data);
                FramePresent(wd);
                // RenderOffscreen();

            // Update and Render additional Platform Windows
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }
            vkDeviceWaitIdle(g_Device);

        }
        catch (const std::exception& e) {
            Logger::Log("Critical error in Update loop: " + std::string(e.what()), LogLevel::CRASH);
            
            // Try to recover
            // vkDeviceWaitIdle(g_Device);
            g_SwapChainRebuild = true;
            
            // Give the GPU a moment to recover
            SDL_Delay(100);
            continue;
        }
       
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
    // vkDestroyBuffer(g_Device, vertexBuffer, nullptr);
    // vkFreeMemory(g_Device, vertexBufferMemory, nullptr);
    // vkDestroyBuffer(g_Device, uniformBuffer, nullptr);
    // vkFreeMemory(g_Device, uniformBufferMemory, nullptr);
    // vkDestroyDescriptorSetLayout(g_Device, uniformDescriptorSetLayout, nullptr);

}

vector<char> Viewport3D::readFile(const string& filename) {
    ifstream file(filename, ios::ate | ios::binary);
    if (!file.is_open()) throw runtime_error("Failed to open file: " + filename);
    size_t fileSize = (size_t)file.tellg();
    vector<char> buffer(fileSize);
    // cout << "Buffer data: " << (buffer.data(), fileSize) << endl;
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

void Viewport3D::setupDepthStencil(uint32_t width, uint32_t height)
	{
		// Create an optimal image used as the depth stencil attachment
		VkImageCreateInfo imageCI{};
		imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCI.imageType = VK_IMAGE_TYPE_2D;
		imageCI.format = depthFormat;
		// Use example's height and width
		imageCI.extent = { width, height, 1 };
		imageCI.mipLevels = 1;
		imageCI.arrayLayers = 1;
		imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		check_vk_result(vkCreateImage(g_Device, &imageCI, nullptr, &depthStencil.image));

        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(g_PhysicalDevice, &memProperties);
		// Allocate memory for the image (device local) and bind it to our image
		VkMemoryAllocateInfo memAlloc{};
		memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(g_Device, depthStencil.image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = FindMemoryType(memReqs, memProperties, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		check_vk_result(vkAllocateMemory(g_Device, &memAlloc, nullptr, &depthStencil.memory));
		check_vk_result(vkBindImageMemory(g_Device, depthStencil.image, depthStencil.memory, 0));

		// Create a view for the depth stencil image
		// Images aren't directly accessed in Vulkan, but rather through views described by a subresource range
		// This allows for multiple views of one image with differing ranges (e.g. for different layers)
		VkImageViewCreateInfo depthStencilViewCI{};
		depthStencilViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		depthStencilViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
		depthStencilViewCI.format = depthFormat;
		depthStencilViewCI.subresourceRange = {};
		depthStencilViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		// Stencil aspect should only be set on depth + stencil formats (VK_FORMAT_D16_UNORM_S8_UINT..VK_FORMAT_D32_SFLOAT_S8_UINT)
		if (depthFormat >= VK_FORMAT_D16_UNORM_S8_UINT) {
			depthStencilViewCI.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		depthStencilViewCI.subresourceRange.baseMipLevel = 0;
		depthStencilViewCI.subresourceRange.levelCount = 1;
		depthStencilViewCI.subresourceRange.baseArrayLayer = 0;
		depthStencilViewCI.subresourceRange.layerCount = 1;
		depthStencilViewCI.image = depthStencil.image;
		check_vk_result(vkCreateImageView(g_Device, &depthStencilViewCI, nullptr, &depthStencil.imageView));
	}

void Viewport3D::setupFrameBuffer(uint32_t width, uint32_t height)
	{
		// Create a frame buffer for every image in the swapchain
		framebuffers.resize(images.size());
		for (size_t i = 0; i < framebuffers.size(); i++)
		{
			std::array<VkImageView, 2> attachments{};
			// Color attachment is the view of the swapchain image
			attachments[0] = imageViews[i];
			// Depth/Stencil attachment is the same for all frame buffers due to how depth works with current GPUs
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
			// Create the framebuffer
			check_vk_result(vkCreateFramebuffer(g_Device, &frameBufferCI, nullptr, &framebuffers[i]));
		}
	}

// Parent Step2 for pipeline
void Viewport3D::createGraphicsPipeline(const string& vertShaderPath, const string& fragShaderPath) {
    vector<char> vertShaderCode = readFile(vertShaderPath);
    vector<char> fragShaderCode = readFile(fragShaderPath);
    // Debug::Logger::Log("Vertshader size: " + convertUintVariabletoString(vertShaderCode.capacity()));
    // Debug::Logger::Log("fragshader size: " + convertUintVariabletoString(fragShaderCode.capacity()));
    // cout << "Vertshader code: " << vertShaderCode.capacity() << endl;
    // cout << "fragshader code: " << fragShaderCode.capacity() << endl;
    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);
    // Debug::Logger::Log("Vertshader module: " + convertUintVariabletoString(vertShaderModule));
    // Debug::Logger::Log("fragshader module: " + convertUintVariabletoString(fragShaderModule));
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

    array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
    
    // Position
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, pos);

    // Color
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, color);

    // Normal
    // attributeDescriptions[2].binding = 0;
    // attributeDescriptions[2].location = 2;
    // attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    // attributeDescriptions[2].offset = sizeof(float) * 6;

    // Vertex input state (sementara kosong, nanti diisi dari model .obj)
    // VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    // vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;


    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
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
	pipelineLayoutInfo.pNext = nullptr;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &uniformDescriptorSetLayout;

    cout << pipelineLayoutInfo.pSetLayouts << "\n" << pipelineLayout << endl;
    if (vkCreatePipelineLayout(g_Device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw runtime_error("Failed to create pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo = createPipeLineInfo(shaderStages, vertexInputInfo, inputAssembly, viewportState, rasterizer, multisampling, colorBlending);

    Debug::Logger::Log("Renderpass: " + to_string(reinterpret_cast<uintptr_t>(renderPass)));
    // cout << "Pipeline info - Stages count: " << pipelineInfo.stageCount 
    //      << ", Layout: " << reinterpret_cast<void*>(pipelineInfo.layout) 
    //      << "\nRender Pass: " << pipelineInfo.renderPass << endl;
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
    VkAttachmentDescription colorAttachment = createColorAttachment();
    
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
    VkFramebufferCreateInfo fbInfo = createFrameBuffer(offscreenRenderPass, attachments, viewportWidth, viewportHeight);

    Debug::Logger::Log("Framebuffer width: " + to_string(fbInfo.width) + ", height: " + to_string(fbInfo.height));
    // Debug::Logger::Log("Framebuffer render pass: " + to_string(fbInfo.renderPass));
    // cout << "Framebuffer width: " << fbInfo.width << ", height: " << fbInfo.height << endl;
    // cout << "Framebuffer render pass: " << offscreenRenderPass << endl;
    vkCreateFramebuffer(g_Device, &fbInfo, nullptr, &offscreenFramebuffer);

    // TODO: load SPIR-V shaders & buat graphicsPipeline (binding vertex, input layout, dsb)
}

void Viewport3D::createSynchronizationPrimitives()
	{
		// Fences are used to check draw command buffer completion on the host
		for (uint32_t i = 0; i < MAX_CONCURRENT_FRAMES; i++) {		
			VkFenceCreateInfo fenceCI{};
			fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			// Create the fences in signaled state (so we don't wait on first render of each command buffer)
			fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			// Fence used to ensure that command buffer has completed exection before using it again
			check_vk_result(vkCreateFence(g_Device, &fenceCI, nullptr, &waitFences[i]));
		}
		// Semaphores are used for correct command ordering within a queue
		// Used to ensure that image presentation is complete before starting to submit again
		presentCompleteSemaphores.resize(MAX_CONCURRENT_FRAMES);
		for (auto& semaphore : presentCompleteSemaphores) {
			VkSemaphoreCreateInfo semaphoreCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
			check_vk_result(vkCreateSemaphore(g_Device, &semaphoreCI, nullptr, &semaphore));
		}
		// Render completion
		// Semaphore used to ensure that all commands submitted have been finished before submitting the image to the queue
		renderCompleteSemaphores.resize(g_SwapChainRebuild);
		for (auto& semaphore : renderCompleteSemaphores) {
			VkSemaphoreCreateInfo semaphoreCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
			check_vk_result(vkCreateSemaphore(g_Device, &semaphoreCI, nullptr, &semaphore));
		}
}

void Viewport3D::glm4Deserealize(const glm::mat4& targetMat4){
		for(int i = 0; i < 4; i++) {
			for(int j = 0; j < 4; j++) {
				std::cout << targetMat4[i][j] << " ";
			}
			std::cout << "\n";
		}
		std::cout << std::endl;
	}

void Viewport3D::HandleRenderViewport(uint32_t width, uint32_t height) {
    // Validasi resources sebelum render
    if (graphicsPipeline == VK_NULL_HANDLE) {
        cerr << "Error: Graphics pipeline not created!" << endl;
        return;
    }
    
    if (renderPass == VK_NULL_HANDLE) {
        cerr << "Error: Render pass not created!" << endl;
        return;
    }
    
    if (vertexBuffer == VK_NULL_HANDLE) {
        cerr << "Error: Vertex buffer not created!" << endl;
        return;
    }
    
    // if (uniformDescriptorSet.empty()) {
    //     Logger::Log("Error: Descriptor sets not created!", LogLevel::CRASH);
    //     return;
    // }
    
    if (offscreenCmdBuffer == VK_NULL_HANDLE) {
        // Create command buffer if not exists
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = offscreenCommandPool;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(g_Device, &allocInfo, &offscreenCmdBuffer) != VK_SUCCESS) {
            Logger::Log("Failed to allocate command buffer!", LogLevel::CRASH);
            return;
        }
    }

    if (uniformBuffers[currentFrame].buffer == VK_NULL_HANDLE) {
        Logger::Log("Error: Uniform buffer not created!", LogLevel::CRASH);
        return;
    }

    uint32_t imageIndex;

    if (framebuffers.empty()) {
        Logger::Log("Error: Framebuffers not created!", LogLevel::CRASH);
        return;
    }
    
    try {
        vkWaitForFences(g_Device, 1, &waitFences[currentFrame], VK_TRUE, UINT64_MAX);
        check_vk_result(vkResetFences(g_Device, 1, &waitFences[currentFrame]));

        // Update uniform buffer dengan camera matrices
        ShaderData shaderData{};
        shaderData.modelMatrix = glm::mat4(1.0f); // Identity matrix
        shaderData.viewMatrix = camera.matrices.view;
        shaderData.projectionMatrix = camera.matrices.perspective;
		cout << "Projection Matrix:\n";
		glm4Deserealize(shaderData.projectionMatrix);
		cout << "View Matrix:\n";
		glm4Deserealize(shaderData.viewMatrix);
		cout << "Model Matrix:\n";
		glm4Deserealize(shaderData.modelMatrix);
        
        // Flip Y axis untuk Vulkan
        // shaderData.projectionMatrix[1][1] *= -1;
        
        // Copy UBO ke uniform buffer
        // void* data;
        // vkMapMemory(g_Device, uniformBufferMemory, 0, sizeof(ShaderData), 0, &data);
        cout << "Will check method memcpy..." << endl;
        cout << "Mapped Uniform Buffer: " << uniformBuffers[currentFrame].mapped << endl;
        // cout << "Shader data: " << reinterpret_cast<uintptr_t>(shaderData) << endl;
        memcpy(uniformBuffers[currentFrame].mapped, &shaderData, sizeof(ShaderData));
        // vkUnmapMemory(g_Device, uniformBufferMemory);

		vkResetCommandBuffer(commandBuffers[currentFrame], 0);
        
		// VkCommandBufferBeginInfo cmdBufInfo{};
		// cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        
        // Begin command buffer
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        
        VkClearValue clearValues[2]{};
        clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 1.0f } };
        clearValues[1].depthStencil = { 1.0f, 0 };

        // if (vkBeginCommandBuffer(offscreenCmdBuffer, &beginInfo) != VK_SUCCESS) {
        //     Logger::Log("Failed to begin recording command buffer!", LogLevel::CRASH);
        //     return;
        // }
        
        // Begin render pass
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.pNext = nullptr;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = offscreenFramebuffer;
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = {width, height};    
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = clearValues;
        renderPassInfo.framebuffer = framebuffers[imageIndex];
        
		const VkCommandBuffer commandBuffer = commandBuffers[currentFrame];
		check_vk_result(vkBeginCommandBuffer(commandBuffer, &beginInfo));

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        
		VkViewport viewport{};
		viewport.height = (float)height;
		viewport.width = (float)width;
		viewport.minDepth = (float)0.0f;
		viewport.maxDepth = (float)1.0f;
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        
        VkRect2D scissor{};
		scissor.extent.width = width;
		scissor.extent.height = height;
		scissor.offset.x = 0;
		scissor.offset.y = 0;
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        // Bind descriptor set
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                            pipelineLayout, 0, 1, &uniformBuffers[currentFrame].descriptorSet, 0, nullptr);

        // Bind pipeline
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

        // Bind vertex buffer
        VkBuffer vertexBuffers[] = {vertexBuffer};
        VkDeviceSize offsets[1]{0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertices.buffer, offsets);
        
        
		// Bind triangle index buffer
		vkCmdBindIndexBuffer(commandBuffer, indices.buffer, 0, VK_INDEX_TYPE_UINT32);

		// Draw indexed triangle
		vkCmdDrawIndexed(commandBuffer, indices.count, 1, 0, 0, 0);

		vkCmdEndRenderPass(commandBuffer);

        // Draw (assuming you have vertex count)
        // vkCmdDraw(offscreenCmdBuffer, vertexCount, 1, 0, 0);
        
        // vkCmdEndRenderPass(offscreenCmdBuffer);
        
        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            Logger::Log("Failed to record command buffer!", LogLevel::CRASH);
        }

		VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        // Submit command buffer
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
		submitInfo.pWaitDstStageMask = &waitStageMask;      // Pointer to the list of pipeline stages that the semaphore waits will occur at
        submitInfo.pCommandBuffers = &commandBuffer;

        // Semaphore to wait upon before the submitted command buffer starts executing
		submitInfo.pWaitSemaphores = &presentCompleteSemaphores[currentFrame];
		submitInfo.waitSemaphoreCount = 1;
		// Semaphore to be signaled when command buffers have completed
		submitInfo.pSignalSemaphores = &renderCompleteSemaphores[imageIndex];
		submitInfo.signalSemaphoreCount = 1;

        VkResult result = vkQueueSubmit(g_Queue, 1, &submitInfo, waitFences[currentFrame]);
        if (result != VK_SUCCESS) {
            Logger::Log("Failed to submit command buffer!", LogLevel::CRASH);
            return;
        }

        VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &renderCompleteSemaphores[imageIndex];
		presentInfo.swapchainCount = 1;
		// presentInfo.pSwapchains = &swapChain.swapChain;
		presentInfo.pImageIndices = &imageIndex;
		result = vkQueuePresentKHR(g_Queue, &presentInfo);

        // Wait for rendering to complete
        check_vk_result(vkQueueWaitIdle(g_Queue));

        currentFrame = (currentFrame + 1) % MAX_CONCURRENT_FRAMES;
    } catch (const exception& e){
        cerr << "HandleRenderViewport Error: " << e.what() << endl;
    }
	
}

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

    VkRenderPassBeginInfo rpInfo = createRenderPassInfo(offscreenRenderPass, offscreenFramebuffer, width, height, clearValue);

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Bind pipeline & draw (segitiga)
    // vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    // vkCmdDraw(cmd, 3, 1, 0, 0);

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

void Viewport3D::videoPlayerUI(){
    static char videoPath[5000] = "";
    static bool loopVideo = true;
    static bool paused = false;
    // enum RenderMode currentMode;
    static bool isOnlyRender = imageHandler.isOnlyRenderImage;
    static bool isOnlyAd = imageHandler.isOnlyAudio;

    ImGui::Begin("Video Player");
    ImGui::InputText("Video File", videoPath, IM_ARRAYSIZE(videoPath));
    ImGui::SameLine();
    if (ImGui::Button("Open")) {
        if (strlen(videoPath) > 0) {
            imageHandler.OpenFileVideo(videoPath);
            paused = false;
        }
    }

    ImGui::Checkbox("Loop Video", &loopVideo);
    ImGui::SameLine();
    ImGui::Checkbox("Only Render Image", &isOnlyRender);
    ImGui::SameLine();
    ImGui::Checkbox("Only Audio", &isOnlyAd);

    if (imageHandler.isPlaying) {  
        if (!paused){
            if (isOnlyRender == true && isOnlyAd == false) {
                // updateVideoFrame();
                Debug::Logger::Log("Only Render Image", Debug::LogLevel::WARNING);
                imageHandler.updateVideoFrame();
            }
            else if (isOnlyRender == false && isOnlyAd == true) {
                // Debug::Logger::Log("Only Render audio", Debug::LogLevel::WARNING);
                imageHandler.updateAudio();
            }
            else {
                if (imageHandler.fps <= 24) {
                    imageHandler.updateBothVideoAndAudio24fps();
                } else {
                    imageHandler.updateBothVideoAndAudio();
                }
            }
        }
            renderVideoFrame();

        ImGui::Separator();
        if (ImGui::Button(paused ? "Play" : "Pause")) {
                paused = !paused;
            }
            ImGui::SameLine();
            if (ImGui::Button("Stop")) {
                av_seek_frame(imageHandler.formatContext, imageHandler.videoStream, 0, AVSEEK_FLAG_BACKWARD);
                imageHandler.currentTime = 0;
                paused = true;
            }

            ImGui::Text("Time: %.2f / %.2f", imageHandler.currentTime, imageHandler.duration);

            // Informasi video
            ImGui::Text("Resolution: %dx%d | FPS: %.2f", 
                        imageHandler.width, imageHandler.height, imageHandler.fps);
            if (!isOnlyRender | isOnlyAd) {
                ImGui::Separator();
                ImGui::Text("Frequency: %d | Channels: %d", 
                        imageHandler.audioCodecContext->sample_rate, imageHandler.audioCodecContext->ch_layout.nb_channels);
            }
    } else {
        ImGui::Text("No video loaded or playing.");
    }

    ImGui::End();
}

void Viewport3D::renderVideoFrame(){
    // Dalam render loop ImGui
    if (imageHandler.getVideoDescriptorSet() != VK_NULL_HANDLE) {
        float aspectRatio = static_cast<float>(imageHandler.width) / static_cast<float>(imageHandler.height);
        
        // Hitung dimensi tampilan
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        float displayWidth = contentSize.x;
        float displayHeight = displayWidth / aspectRatio;
        
        if (displayHeight > contentSize.y) {
            displayHeight = contentSize.y;
            displayWidth = displayHeight * aspectRatio;
        }
        
        // Debug: Print display dimensions
        // cout << "Display dimensions: " << displayWidth << "x" << displayHeight << endl;
        
        // Pusatkan video di ruang yang tersedia  
        float posX = (contentSize.x - displayWidth) * 0.5f;
        float posY = (contentSize.y - displayHeight) * 0.5f;
        
        ImGui::SetCursorPos(ImVec2(posX, posY));
        
        // Coba render dengan berbagai cara untuk test
        
        // Method 1: Current method
        // ui::Image(reinterpret_cast<ImTextureID>(reinterpret_cast<void*>(static_cast<intptr_t>(videoPlayer->glTextureID))),
        //          ImVec2(displayWidth, displayHeight));
        
        float w = displayWidth, h = displayHeight;
        ImGui::Image((ImTextureID)imageHandler.getVideoDescriptorSet(), 
                    ImVec2(w, h));
    } else {
        ImGui::Text("No texture available");
    }
}

// Draw Image
void Viewport3D::DrawImage()
{
    static VkDescriptorSet imguiTexture = VK_NULL_HANDLE;
    if (imguiTexture == VK_NULL_HANDLE){
        imguiTexture = imageHandler.LoadImage("assets/images/backgrounds/shiroko_bluearchive.jpg");
    }
    ImGui::Begin("Image Test");
    ImGui::Image((ImTextureID)imguiTexture, ImVec2(640, 360));
    ImGui::End();
}

void Viewport3D::FrameRender(ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data)
{
    // Early exit if no draw data
    if (!draw_data || draw_data->CmdListsCount == 0)
        return;

    try {
        VkResult err;
        
        // Get semaphores for current frame
        VkSemaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
        VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;

        // Acquire next swapchain image
        uint32_t frame_idx = 0;
        err = vkAcquireNextImageKHR(g_Device, wd->Swapchain, UINT64_MAX, 
            image_acquired_semaphore, VK_NULL_HANDLE, &frame_idx);
        
        if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
            g_SwapChainRebuild = true;
            return;
        }
        check_vk_result(err);

        // Get frame data
        wd->FrameIndex = frame_idx;
        ImGui_ImplVulkanH_Frame* fd = &wd->Frames[frame_idx];

        // Wait for previous frame to complete
        err = vkWaitForFences(g_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
        check_vk_result(err);
        
        err = vkResetFences(g_Device, 1, &fd->Fence);
        check_vk_result(err);

        // Reset and begin command buffer
        err = vkResetCommandPool(g_Device, fd->CommandPool, 0);
        check_vk_result(err);

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        
        err = vkBeginCommandBuffer(fd->CommandBuffer, &begin_info);
        check_vk_result(err);

        // Begin render pass
        VkRenderPassBeginInfo rp_info = {};
        rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp_info.renderPass = wd->RenderPass;
        rp_info.framebuffer = fd->Framebuffer;
        rp_info.renderArea.extent.width = wd->Width;
        rp_info.renderArea.extent.height = wd->Height;
        rp_info.clearValueCount = 1;
        rp_info.pClearValues = &wd->ClearValue;

        vkCmdBeginRenderPass(fd->CommandBuffer, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

        // Record ImGui Draw Data and Commands
        ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

        // End render pass
        vkCmdEndRenderPass(fd->CommandBuffer);
        err = vkEndCommandBuffer(fd->CommandBuffer);
        check_vk_result(err);

        // Submit command buffer
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

        err = vkQueueSubmit(g_Queue, 1, &submit_info, fd->Fence);
        check_vk_result(err);

    } catch (const std::exception& e) {
        // Handle device lost error
        if (g_Device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(g_Device);
            g_SwapChainRebuild = true;
            Logger::Log("Device lost error: " + std::string(e.what()), LogLevel::CRASH);
        }
    }
}

uint32_t Viewport3D::FindMemoryType(VkMemoryRequirements memRequirements, VkPhysicalDeviceMemoryProperties memProperties, VkMemoryPropertyFlags properties) {
    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        bool typeSupported = (memRequirements.memoryTypeBits & (1 << i));
        bool propertiesSupported = (memProperties.memoryTypes[i].propertyFlags & properties) == properties;
        
        if (typeSupported && propertiesSupported) {
            memTypeIndex = i;
            break;
        }
    }

    if (memTypeIndex == UINT32_MAX) {
        throw std::runtime_error("failed to find suitable memory type!");
    }
    else {
        Logger::Log("find suitable memory type: " + to_string(memTypeIndex), LogLevel::SUCCESS);
    }

    return memTypeIndex;
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
    else {
        Logger::Log("Buffer created successfully. Size: " + to_string(size) + " bytes", LogLevel::SUCCESS);
    }

    // Get memory requirements
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(g_Device, buffer, &memRequirements);

    // Get memory properties BEFORE the loop
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(g_PhysicalDevice, &memProperties);

    // Find suitable memory type
    uint32_t memTypeIndex = FindMemoryType(memRequirements, memProperties, properties);

    // Allocate memory
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memTypeIndex;

    if (vkAllocateMemory(g_Device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate buffer memory!");
    }
    else {
        Logger::Log("Allocate memory successfully. Size: " + to_string(allocInfo.allocationSize) + " bytes", LogLevel::SUCCESS);
    }

    // Bind buffer with allocated memory
    if (vkBindBufferMemory(g_Device, buffer, bufferMemory, 0) != VK_SUCCESS) {
        throw std::runtime_error("failed to bind buffer memory!");
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

    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(g_PhysicalDevice, &memProperties);
    
    for (uint32_t i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
			check_vk_result(vkCreateBuffer(g_Device, &bufferInfo, nullptr, &uniformBuffers[i].buffer));
			// Get memory requirements including size, alignment and memory type
			vkGetBufferMemoryRequirements(g_Device, uniformBuffers[i].buffer, &memReqs);
			allocInfo.allocationSize = memReqs.size;
			// Get the memory type index that supports host visible memory access
			// Most implementations offer multiple memory types and selecting the correct one to allocate memory from is crucial
			// We also want the buffer to be host coherent so we don't have to flush (or sync after every update.
			// Note: This may affect performance so you might not want to do this in a real world application that updates buffers on a regular base
			allocInfo.memoryTypeIndex = FindMemoryType(memReqs, memProperties, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			// Allocate memory for the uniform buffer
			check_vk_result(vkAllocateMemory(g_Device, &allocInfo, nullptr, &(uniformBuffers[i].memory)));
			// Bind memory to buffer
			check_vk_result(vkBindBufferMemory(g_Device, uniformBuffers[i].buffer, uniformBuffers[i].memory, 0));
			// We map the buffer once, so we can update it without having to map it again
			check_vk_result(vkMapMemory(g_Device, uniformBuffers[i].memory, 0, sizeof(ShaderData), 0, (void**)&uniformBuffers[i].mapped));
		}

    CreateBuffer(
        bufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        uniformBuffer,
        uniformBufferMemory
    );
}

void Viewport3D::createCommandBuffers()
	{
		// All command buffers are allocated from a command pool
		VkCommandPoolCreateInfo commandPoolCI{};
		commandPoolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		commandPoolCI.queueFamilyIndex = g_QueueFamily;
		commandPoolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		check_vk_result(vkCreateCommandPool(g_Device, &commandPoolCI, nullptr, &commandPool));

		// Allocate one command buffer per max. concurrent frame from above pool
		VkCommandBufferAllocateInfo cmdBufAllocateInfo = commandBufferAllocateInfo(commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, MAX_CONCURRENT_FRAMES);
		check_vk_result(vkAllocateCommandBuffers(g_Device, &cmdBufAllocateInfo, commandBuffers.data()));
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
	layoutInfo.pNext = nullptr;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    if (vkCreateDescriptorSetLayout(g_Device, &layoutInfo, nullptr, &uniformDescriptorSetLayout) != VK_SUCCESS) {
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

            check_vk_result(vkAllocateDescriptorSets(g_Device, &allocInfo, &uniformBuffers[i].descriptorSet));

            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = uniformBuffer;
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(UniformBufferObject);

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

// void Viewport3D::updateUniformBuffer() {
//     static auto startTime = chrono::high_resolution_clock::now();
//     // Logger::Log("Start time: "+ to_string(startTime.time_since_epoch().count()), LogLevel::WARNING);
//     auto currentTime = chrono::high_resolution_clock::now();
//     float time = chrono::duration<float, chrono::seconds::period>
//         (currentTime - startTime).count();

//     UniformBufferObject ubo{};
//     // Rotate model
//     ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), 
//         glm::vec3(0.0f, 0.0f, 1.0f));
    
//     // View matrix from camera
//     ubo.view = camera->GetViewMatrix();
    
//     // Projection matrix
//     ubo.proj = glm::perspective(glm::radians(45.0f), 
//         viewportWidth / (float) viewportHeight, 0.1f, 100.0f);
    
//     // Vulkan NDC uses different coordinate system
//     ubo.proj[1][1] *= -1;

//     // Copy to uniform buffer
//     void* data;
//     vkMapMemory(g_Device, uniformBufferMemory, 0, sizeof(ubo), 0, &data);
//     memcpy(data, &ubo, sizeof(ubo));
//     vkUnmapMemory(g_Device, uniformBufferMemory);
// }

// Add this method to create and fill vertex buffer
void Viewport3D::createVertexBuffer() {
    // Setup vertices
    std::vector<Vertex> vertexBuffer{
        { {  1.0f,  1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
        { { -1.0f,  1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
        { {  0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } }
    };
    uint32_t vertexBufferSize = static_cast<uint32_t>(vertexBuffer.size()) * sizeof(Vertex);

    // Create vertex buffer
    CreateBuffer(
        vertexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        vertices.buffer,  // Store in class member
        vertices.memory   // Store in class member
    );

    // Create staging buffer for vertices
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(
        vertexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingBufferMemory
    );

    // Copy vertex data to staging buffer
    void* data;
    vkMapMemory(g_Device, stagingBufferMemory, 0, vertexBufferSize, 0, &data);
    memcpy(data, vertexBuffer.data(), vertexBufferSize);
    vkUnmapMemory(g_Device, stagingBufferMemory);

    // Setup indices
    std::vector<uint32_t> indexBuffer{ 0, 1, 2 };
    indices.count = static_cast<uint32_t>(indexBuffer.size());
    uint32_t indexBufferSize = indices.count * sizeof(uint32_t);

    // Create index buffer
    CreateBuffer(
        indexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        indices.buffer,  // Store in class member
        indices.memory   // Store in class member
    );

    // Create staging buffer for indices
    VkBuffer indexStagingBuffer;
    VkDeviceMemory indexStagingBufferMemory;
    CreateBuffer(
        indexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        indexStagingBuffer,
        indexStagingBufferMemory
    );

    // Copy index data to staging buffer
    vkMapMemory(g_Device, indexStagingBufferMemory, 0, indexBufferSize, 0, &data);
    memcpy(data, indexBuffer.data(), indexBufferSize);
    vkUnmapMemory(g_Device, indexStagingBufferMemory);

    // Transfer buffers
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

    // Copy vertices
    VkBufferCopy vertexCopy{};
    vertexCopy.size = vertexBufferSize;
    vkCmdCopyBuffer(commandBuffer, stagingBuffer, vertices.buffer, 1, &vertexCopy);

    // Copy indices
    VkBufferCopy indexCopy{};
    indexCopy.size = indexBufferSize;
    vkCmdCopyBuffer(commandBuffer, indexStagingBuffer, indices.buffer, 1, &indexCopy);

    EndSingleTimeCommands(commandBuffer);

    // Cleanup staging buffers
    vkDestroyBuffer(g_Device, stagingBuffer, nullptr);
    vkFreeMemory(g_Device, stagingBufferMemory, nullptr);
    vkDestroyBuffer(g_Device, indexStagingBuffer, nullptr);
    vkFreeMemory(g_Device, indexStagingBufferMemory, nullptr);

    // Store vertex count
    vertexCount = static_cast<uint32_t>(vertexBuffer.size());
}

int main(int argc, char* argv[]){
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO))
    {
        cout << "Error: SDL_Init(): " << SDL_GetError() << endl;
        return -1;
    }

    float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    SDL_WindowFlags windowFlags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* mainWindow = SDL_CreateWindow("Viewport 3D", 1280, 720, windowFlags);
    if (mainWindow == nullptr)
    {
        cout << "Error: SDL_CreateWindow(): " << SDL_GetError() << endl;
        return -1;
    }

    ImVector<const char*> extensions;
    {
        unsigned int sdl_extensions_count = 0;
        const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extensions_count);
        for (unsigned int n = 0; n < sdl_extensions_count; n++)
            extensions.push_back(sdl_extensions[n]);
    }

    try {
        Viewport3D viewport;
        
        // Langkah 1: Initialize Vulkan
        Logger::Log("Initializing Vulkan...");
        viewport.initVulkan(extensions, mainWindow);
        
        // Langkah 2: Create render pass PERTAMA
        Logger::Log("Creating render pass...");
        viewport.createRenderPass();

        viewport.setupDepthStencil(1280, 720);
        viewport.setupFrameBuffer(1280, 720);

        viewport.createSynchronizationPrimitives();
        viewport.createCommandBuffers();
        
        // Langkah 3: Create offscreen resources
        Logger::Log("Creating offscreen resources...");
        viewport.CreateOffscreenCommandResources();
        
        // Langkah 6: Create buffers
        Logger::Log("Creating vertex buffer...");
        viewport.createVertexBuffer();

        Logger::Log("Creating uniform buffers...");
        viewport.createUniformBuffers();

        // Langkah 4: Create descriptor set layout
        Logger::Log("Creating uniform descriptor set layout...");
        viewport.createUniformDescriptorSetLayout();

        // init vulkan handler needed variabel
        viewport.helperInitImage();
        viewport.CreateOffscreenResources(1280, 720);

        viewport.CreateOffscreenPipeline();           
    
        Logger::Log("Creating uniform descriptor sets...");
        viewport.createUniformDescriptorSets();
        
        // Langkah 7: Initialize camera
        // Logger::Log("Initializing camera...");
        // if (viewport.camera == nullptr) {
        //     viewport.camera = new Camera(glm::vec3(0.0f, 0.0f, 3.0f));
        // }

        // Langkah 5: Create graphics pipeline
        Logger::Log("Creating graphics pipeline...");
        try {
            viewport.createGraphicsPipeline("assets/shaders/vulkan/triangle.vert.spv", "assets/shaders/vulkan/triangle.frag.spv");
        } catch (const exception e){
            Logger::Log("PipeLine error: " + string(e.what()), LogLevel::CRASH);
            throw e;
        }
        
        // Langkah 8: Update dan render
        Logger::Log("Starting update loop...");
        viewport.Update(viewport.currentIo);
        
        // Cleanup
        viewport.CleanupVulkan();
    }
    catch (const std::exception& e) {
        Logger::Log("Exception caught: " + string(e.what()), LogLevel::CRASH);
        return -1;
    }

    SDL_DestroyWindow(mainWindow);
    SDL_Quit();
    return 0;
}
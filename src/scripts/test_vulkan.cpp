#include "test_vulkan.hpp"
#include <iostream>
#include <vector>
#include <stdexcept>
#include <imgui.h>
#include <SDL.h>
#include <SDL_vulkan.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>
#include <SDL_ttf.h>
#include <windows.h>
#include <map>
#include <iomanip>

TestVulkan::TestVulkan(SDL_Window* window) : sdlWindow(window) {
    createInstance();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createRenderPass();
    // ratePhysicalDevice(physicalDevice);
    // printDeviceProperties(physicalDevice);
    // isDiscreteGPU(physicalDevice);
    createCommandPool();
    createDescriptorPool();
    setupImGui();
}

TestVulkan::~TestVulkan() {
    // Wait for the device to finish operations before destroying resources
    vkDeviceWaitIdle(device);

    // ImGui cleanup
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    // Destroy Vulkan resources (in reverse order of creation)
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
}

void TestVulkan::createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Test Vulkan";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "IlmeeEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;  // Updated to 1.1 for better compatibility

    // Get required extensions from SDL
    unsigned int extensionCount = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(sdlWindow, &extensionCount, nullptr)) {
        throw std::runtime_error("Failed to get SDL Vulkan extension count");
    }
    
    std::vector<const char*> extensions(extensionCount);
    if (!SDL_Vulkan_GetInstanceExtensions(sdlWindow, &extensionCount, extensions.data())) {
        throw std::runtime_error("Failed to get SDL Vulkan extensions");
    }

    // Add debug extensions if needed
    #ifdef _DEBUG
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    #endif

    // Setup validation layers for debug builds
    std::vector<const char*> validationLayers;
    #ifdef _DEBUG
    validationLayers.push_back("VK_LAYER_KHRONOS_validation");
    #endif

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance!");
    }
}

void TestVulkan::createSurface() {
    if (!SDL_Vulkan_CreateSurface(sdlWindow, instance, &surface)) {
        throw std::runtime_error("Failed to create Vulkan surface!");
    }
}

// void TestVulkan::pickPhysicalDevice() {
//     uint32_t deviceCount = 0;
//     vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    
//     if (deviceCount == 0) {
//         throw std::runtime_error("Failed to find GPUs with Vulkan support!");
//     }
    
//     std::vector<VkPhysicalDevice> devices(deviceCount);
//     vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    
//     // Just pick the first device for simplicity
//     physicalDevice = devices[0];
    
//     // Get queue family indices
//     uint32_t queueFamilyCount = 0;
//     vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
//     std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
//     vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());
    
//     // Find graphics queue family index
//     for (uint32_t i = 0; i < queueFamilyCount; i++) {
//         if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
//             graphicsQueueFamily = i;
//             break;
//         }
//     }
    
//     // Find presentation queue family index
//     for (uint32_t i = 0; i < queueFamilyCount; i++) {
//         VkBool32 presentSupport = false;
//         vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);
//         if (presentSupport) {
//             presentQueueFamily = i;
//             break;
//         }
//     }
    
//     if (graphicsQueueFamily < 0 || presentQueueFamily < 0) {
//         throw std::runtime_error("Failed to find suitable queue families!");
//     }
// }

void TestVulkan::createLogicalDevice() {
    // Create a set of unique queue families needed
    std::vector<uint32_t> uniqueQueueFamilies = { 
        static_cast<uint32_t>(graphicsQueueFamily), 
        static_cast<uint32_t>(presentQueueFamily)
    };
    if (graphicsQueueFamily == presentQueueFamily) {
        uniqueQueueFamilies.pop_back();  // Remove duplicate
    }
    
    float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }
    
    // Specify device features
    VkPhysicalDeviceFeatures deviceFeatures{};
    
    // Required device extensions
    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
    
    // Create the logical device
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    
    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create logical device!");
    }
    
    // Get queue handles
    vkGetDeviceQueue(device, graphicsQueueFamily, 0, &graphicsQueue);
    vkGetDeviceQueue(device, presentQueueFamily, 0, &presentQueue);
}

void TestVulkan::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = graphicsQueueFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool!");
    }
}

void TestVulkan::createDescriptorPool() {
    // Create descriptor pool for ImGui
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
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
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = std::size(poolSizes);
    poolInfo.pPoolSizes = poolSizes;
    
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool!");
    }
}

void TestVulkan::setupImGui() {
    // Setup ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    // Setup ImGui style
    ImGui::StyleColorsDark();
    
    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForVulkan(sdlWindow);

    // Retrieve swapchain images before using their count
    uint32_t swapchainImageCount = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, nullptr);
    std::vector<VkImage> swapchainImages(swapchainImageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, swapchainImages.data());
    
    // Initialize ImGui for Vulkan
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = instance;
    initInfo.PhysicalDevice = physicalDevice;
    initInfo.Device = device;
    initInfo.QueueFamily = graphicsQueueFamily;
    initInfo.Queue = graphicsQueue;
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = descriptorPool;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = swapchainImages.size();
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.RenderPass = renderPass;  // Add this line
    
    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        throw std::runtime_error("Failed to initialize ImGui Vulkan implementation");
    }

    // Upload fonts
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    ImGui_ImplVulkan_CreateFontsTexture();
    endSingleTimeCommands(commandBuffer);
    // ImGui_ImplVulkan_DestroyFontUploadObjects();    
}

void TestVulkan::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainImageFormat;
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

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }
}

void TestVulkan::createSwapChain() {
    // Get surface capabilities
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

    // Choose surface format
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());
    
    swapchainImageFormat = formats[0].format;
    VkColorSpaceKHR colorSpace = formats[0].colorSpace;

    // Create swapchain
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = capabilities.minImageCount + 1;
    createInfo.imageFormat = swapchainImageFormat;
    createInfo.imageColorSpace = colorSpace;
    createInfo.imageExtent = capabilities.currentExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    createInfo.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swap chain!");
    }
}

void TestVulkan::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    
    if (deviceCount == 0) {
        throw std::runtime_error("Failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    // Rate devices and pick the best one
    std::multimap<int, VkPhysicalDevice> candidates;
    
    for (const auto& device : devices) {
        int score = ratePhysicalDevice(device);
        candidates.insert(std::make_pair(score, device));
        printDeviceProperties(device);
    }

    // Check if the best candidate is suitable
    if (candidates.rbegin()->first > 0) {
        physicalDevice = candidates.rbegin()->second;
        VkPhysicalDeviceProperties deviceProps;
        vkGetPhysicalDeviceProperties(physicalDevice, &deviceProps);
        std::cout << "Selected GPU: " << deviceProps.deviceName << std::endl;
    } else {
        throw std::runtime_error("Failed to find a suitable GPU!");
    }
    std::cout << "Using GPU: " << physicalDevice << std::endl;
}

int TestVulkan::ratePhysicalDevice(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties deviceProps;
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceProperties(device, &deviceProps);
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    // Print detailed device info for debugging
    std::cout << "\nEvaluating device: " << deviceProps.deviceName << std::endl;
    std::cout << "Device ID: " << deviceProps.deviceID << std::endl;
    std::cout << "Vendor ID: " << deviceProps.vendorID << std::endl;
    std::cout << "Driver Version: " << deviceProps.driverVersion << std::endl;

    int score = 0;

    // Check for compute capabilities
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(device, &memProps);

    // Check queue families for compute support
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    bool hasComputeSupport = false;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) {
            hasComputeSupport = true;
            score += 500; // Bonus for compute support
            std::cout << "Device has compute support" << std::endl;
            break;
        }
    }

    // Score based on device type
    switch(deviceProps.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            score += 1000;
            std::cout << "Discrete GPU: +1000 points" << std::endl;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            score += 100;
            std::cout << "Integrated GPU: +100 points" << std::endl;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            score += 50;
            std::cout << "Virtual GPU: +50 points" << std::endl;
            break;
        default:
            std::cout << "Other GPU type: +0 points" << std::endl;
            break;
    }

    // Score based on memory size
    VkDeviceSize maxHeapSize = 0;
    for(uint32_t i = 0; i < memProps.memoryHeapCount; i++) {
        if(memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            maxHeapSize = std::max(maxHeapSize, memProps.memoryHeaps[i].size);
        }
    }
    
    // Convert to GB and add to score
    score += static_cast<int>(maxHeapSize / (1024 * 1024 * 1024));
    std::cout << "Memory size score: +" << (maxHeapSize / (1024 * 1024 * 1024)) << " points" << std::endl;

    // Check for specific features
    if (deviceFeatures.geometryShader) {
        score += 100;
        std::cout << "Has geometry shader: +100 points" << std::endl;
    }
    if (deviceFeatures.tessellationShader) {
        score += 100;
        std::cout << "Has tessellation: +100 points" << std::endl;
    }

    std::cout << "Final score: " << score << std::endl;
    std::cout << "------------------------" << std::endl;

    return score;
}

bool TestVulkan::isDiscreteGPU(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties deviceProps;
    vkGetPhysicalDeviceProperties(device, &deviceProps);
    return deviceProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
}

QueueFamilyIndices TestVulkan::findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }

        if (indices.isComplete()) {
            break;
        }

        i++;
    }

    return indices;
}

void TestVulkan::printDeviceProperties(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties deviceProps;
    VkPhysicalDeviceMemoryProperties memProps;
    VkPhysicalDeviceFeatures deviceFeatures;
    
    vkGetPhysicalDeviceProperties(device, &deviceProps);
    vkGetPhysicalDeviceMemoryProperties(device, &memProps);
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    std::cout << "\n=== Device Details ===" << std::endl;
    std::cout << "Device Name: " << deviceProps.deviceName << std::endl;
    std::cout << "Device ID: 0x" << std::hex << deviceProps.deviceID << std::dec << std::endl;
    std::cout << "Vendor ID: 0x" << std::hex << deviceProps.vendorID << std::dec << std::endl;
    
    std::cout << "Device Type: ";
    switch(deviceProps.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            std::cout << "Integrated GPU";
            break;
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            std::cout << "Discrete GPU";
            break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            std::cout << "Virtual GPU";
            break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            std::cout << "CPU";
            break;
        default:
            std::cout << "Other";
    }
    std::cout << std::endl;

    // Print memory information
    std::cout << "\nMemory Heaps:" << std::endl;
    for(uint32_t i = 0; i < memProps.memoryHeapCount; i++) {
        float sizeGB = static_cast<float>(memProps.memoryHeaps[i].size) / (1024.0f * 1024.0f * 1024.0f);
        std::cout << "  Heap " << i << ": " << std::fixed << std::setprecision(2) << sizeGB << " GB";
        if(memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            std::cout << " (Device Local)";
        std::cout << std::endl;
    }

    // Print queue families
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    std::cout << "\nQueue Families:" << std::endl;
    for(uint32_t i = 0; i < queueFamilyCount; i++) {
        std::cout << "  Family " << i << ":" << std::endl;
        std::cout << "    Queue Count: " << queueFamilies[i].queueCount << std::endl;
        std::cout << "    Capabilities:";
        if(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            std::cout << " Graphics";
        if(queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
            std::cout << " Compute";
        if(queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT)
            std::cout << " Transfer";
        std::cout << std::endl;
    }

    std::cout << "\nAPI Version: " 
              << VK_VERSION_MAJOR(deviceProps.apiVersion) << "."
              << VK_VERSION_MINOR(deviceProps.apiVersion) << "."
              << VK_VERSION_PATCH(deviceProps.apiVersion) << std::endl;
    std::cout << "Driver Version: "
              << VK_VERSION_MAJOR(deviceProps.driverVersion) << "."
              << VK_VERSION_MINOR(deviceProps.driverVersion) << "."
              << VK_VERSION_PATCH(deviceProps.driverVersion) << std::endl;
    std::cout << "========================" << std::endl;
}

VkCommandBuffer TestVulkan::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;
    
    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    
    return commandBuffer;
}

void TestVulkan::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

void TestVulkan::draw() {
    // For now, we just create an ImGui window without actual rendering
    ImGui::Begin("Vulkan Test");
    ImGui::Text("Vulkan Initialized Successfully!");
    ImGui::Text("This is a test UI without actual Vulkan rendering");
    ImGui::Text("Physical Device: Selected");
    ImGui::Text("Logical Device: Created");
    ImGui::Text("Graphics Queue Family: %d", graphicsQueueFamily);
    ImGui::Text("Present Queue Family: %d", presentQueueFamily);
    ImGui::End();
}

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Vulkan + SDL2", 
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
        800, 600, 
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        
    if (!window) {
        std::cerr << "Failed to create SDL window: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }

    TestVulkan* vulkanTest = nullptr;
    try {
        vulkanTest = new TestVulkan(window);
    } catch (const std::exception& e) {
        std::cerr << "Error initializing Vulkan: " << e.what() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    bool running = true;
    SDL_Event event;
    while (running) {
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                running = false;
            if (event.type == SDL_WINDOWEVENT && 
                event.window.event == SDL_WINDOWEVENT_CLOSE && 
                event.window.windowID == SDL_GetWindowID(window))
                running = false;
        }

        // Start ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Draw UI
        vulkanTest->draw();

        // Render
        ImGui::Render();
        
        // Note: In a complete implementation, you would render to the swapchain here
        // This is a simplified example without actual rendering
        
        // Delay to prevent CPU usage spike
        SDL_Delay(16);  // ~60 FPS
    }

    delete vulkanTest;
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    return main(__argc, __argv);
}
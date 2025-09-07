#pragma once
#include <Debugger.hpp>
#include <vulkan/vulkan.h>
#include <SDL_vulkan.h>
#include <SDL.h>
#include <string>
#include <iostream>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <stdlib.h>
#include <imgui_impl_sdl3.h>
#include <fstream>
using namespace std;
using namespace Debug;

class VulkanHandler {
    public: 
        VkDescriptorSet LoadImage(const char* filename);
        void setCurrentDeviceAndPhysic(VkDevice device, VkPhysicalDevice physicalDevice);

    private:
        VkDevice currentDevice;
        VkPhysicalDevice currentPhysicalDevice;

};
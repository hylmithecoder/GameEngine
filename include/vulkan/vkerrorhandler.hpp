#include <vulkanhandler.hpp>

namespace vkhandler 
{
    static void VK_CHECK_RESULT(VkResult err) {
            // Logger::Log("[vulkan] Info: VkResult = " + to_string(err), LogLevel::INFO);

        if (err == VK_SUCCESS){ 
            return;
        }

        // Logger::Log("[vulkan] Error: VkResult = " + to_string(err), LogLevel::CRASH);
        
        switch (err) {
            case VK_ERROR_DEVICE_LOST:
                Logger::Log("Device lost - attempting recovery...", LogLevel::WARNING);
                break;
            case VK_ERROR_OUT_OF_DATE_KHR:
            case VK_SUBOPTIMAL_KHR:
                // Logger::Log("Swap chain out of date or suboptimal - rebuilding...", LogLevel::WARNING);
                // g_SwapChainRebuild = true;
                break;
            default:
                if (err < 0) {
                    throw runtime_error("Unhandled Vulkan error");
                }
                break;
        }
    }
}
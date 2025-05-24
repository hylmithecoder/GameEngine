#pragma once
#include <GameEngine.h>

namespace GameEngine {

    class GAMEENGINE_API CoreDebugger {
    public:
        static void LogInfo(const std::string& message);
        static void LogWarning(const std::string& message);
        static void LogError(const std::string& message);
        // static void ShowNotification(const std::string& title, const std::string& message, const ImVec4& color);
        static void ShowError(const std::string& title, const std::string& message);
        static void ShowWarning(const std::string& title, const std::string& message);
    };
}
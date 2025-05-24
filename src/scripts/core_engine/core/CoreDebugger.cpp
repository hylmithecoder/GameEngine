// This is a core debugger implementation for a game engine.

#include <CoreDebugger.h>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace GameEngine {
        void CoreDebugger::LogInfo(const std::string& message) {
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            SetConsoleTextAttribute(hConsole, 9); // Blue
            std::cout << "[INFO] " << message << std::endl;
            SetConsoleTextAttribute(hConsole, 15); // Reset to default
        }

        void CoreDebugger::LogWarning(const std::string& message) {
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            SetConsoleTextAttribute(hConsole, 14); // Yellow
            std::cout << "[WARNING] " << message << std::endl;
            SetConsoleTextAttribute(hConsole, 15); // Reset to default
        }

        void CoreDebugger::LogError(const std::string& message) {
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            SetConsoleTextAttribute(hConsole, 12); // Red
            std::cerr << "[ERROR] " << message << std::endl;
            SetConsoleTextAttribute(hConsole, 15); // Reset to default
        }
}
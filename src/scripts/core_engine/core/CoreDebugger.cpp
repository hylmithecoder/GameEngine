// This is a core debugger implementation for a game engine.

#include <CoreDebugger.h>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace GameEngine {
        void CoreDebugger::LogInfo(const std::string& message) {
            #ifdef _WIN32
                    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
                    SetConsoleTextAttribute(hConsole, 9); // Blue
                    std::cout << "[INFO] ";
                    SetConsoleTextAttribute(hConsole, 15);
            #else
                    std::cout << "\033[1;34m[INFO]\033[0m ";
            #endif
                    std::cout << message << std::endl;
        }

        void CoreDebugger::LogWarning(const std::string& message) {
            #ifdef _WIN32
                    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
                    SetConsoleTextAttribute(hConsole, 14); // Yellow
                    std::cout << "[WARNING] ";
                    SetConsoleTextAttribute(hConsole, 15);
            #else
                    std::cout << "\033[1;33m[WARNING]\033[0m ";
            #endif
                    std::cout << message << std::endl;
        }

        void CoreDebugger::LogError(const std::string& message) {
            #ifdef _WIN32
                    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
                    SetConsoleTextAttribute(hConsole, 12); // Red
                    std::cerr << "[ERROR] ";
                    SetConsoleTextAttribute(hConsole, 15);
            #else
                    std::cerr << "\033[1;31m[ERROR]\033[0m ";
            #endif
                    std::cerr << message << std::endl;
        }

        void CoreDebugger::LogSuccess(const std::string& message) {
            #ifdef _WIN32
                    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
                    SetConsoleTextAttribute(hConsole, 10); // Green
                    std::cout << "[SUCCESS] ";
                    SetConsoleTextAttribute(hConsole, 15);
            #else
                    std::cout << "\033[1;32m[SUCCESS]\033[0m ";
            #endif
                    std::cout << message << std::endl;
        }
}
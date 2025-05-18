#pragma once
#include <iostream>
#include <string>
#include <windows.h>

namespace Debug {
    enum class LogLevel {
        INFO,
        WARNING,
        CRASH,
        SUCCESS
    };

    class Logger {
    public:
        static void Log(const std::string& message, LogLevel level = LogLevel::INFO) {
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            
            // Set color based on log level
            switch(level) {
                default:
                case LogLevel::INFO:
                    SetConsoleTextAttribute(hConsole, 9); // Blue
                    std::cout << "[INFO] ";
                    break;
                case LogLevel::WARNING:
                    SetConsoleTextAttribute(hConsole, 14); // Yellow
                    std::cout << "[WARNING] ";
                    break;
                case LogLevel::CRASH:
                    SetConsoleTextAttribute(hConsole, 12); // Red
                    std::cout << "[ERROR] ";
                    break;
                case LogLevel::SUCCESS:
                    SetConsoleTextAttribute(hConsole, 10); // Green
                    std::cout << "[SUCCESS] ";
                    break;
            }

            std::cout << message << std::endl;
            
            // Reset color
            SetConsoleTextAttribute(hConsole, 15);
        }
    };
}
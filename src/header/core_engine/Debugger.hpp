#pragma once
#include <iostream>
#include <string>

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
            // ANSI escape codes for colors
            const char* blue = "\033[34m";
            const char* yellow = "\033[33m";
            const char* red = "\033[31m";
            const char* green = "\033[32m";
            const char* reset = "\033[0m";
            
            // Set color based on log level
            switch(level) {
                default:
                case LogLevel::INFO:
                    std::cout << blue << "[INFO] ";
                    break;
                case LogLevel::WARNING:
                    std::cout << yellow << "[WARNING] ";
                    break;
                case LogLevel::CRASH:
                    std::cout << red << "[ERROR] ";
                    break;
                case LogLevel::SUCCESS:
                    std::cout << green << "[SUCCESS] ";
                    break;
            }

            std::cout << message << reset << std::endl;
        }
    };
}
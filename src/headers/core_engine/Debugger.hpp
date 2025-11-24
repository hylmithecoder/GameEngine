#pragma once
#include <iostream>
#include <string>
using namespace std;

namespace Debug {
    enum class LogLevel {
        INFO,
        WARNING,
        CRASH,
        SUCCESS
    };

    class Logger {
    public:
        static void Log(const string& message, LogLevel level = LogLevel::INFO) {
            // ANSI escape codes for colors
            const char* blue = "\033[1;34m";
            const char* yellow = "\033[1;33m";
            const char* red = "\033[1;31m";
            const char* green = "\033[1;32m";
            const char* reset = "\033[0m";
                        
            // Set color based on log level
            switch(level) {
                default:
                case LogLevel::INFO:
                    cout << blue << "[INFO] ";
                    break;
                case LogLevel::WARNING:
                    cout << yellow << "[WARNING] ";
                    break;
                case LogLevel::CRASH:
                    cout << red << "[ERROR] ";
                    break;
                case LogLevel::SUCCESS:
                    cout << green << "[SUCCESS] ";
                    break;
            }

            cout << message << reset << endl;
        }
    };
}
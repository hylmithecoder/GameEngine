#pragma once
#include <iostream>
#include <string>
#include <cstdio>
#include <cstdarg>
using namespace std;

namespace Debug {
    enum class LogLevel {
        INFO,
        WARNING,
        CRASH,
        SUCCESS,
        ERROR
    };

        // ANSI escape codes untuk colors
    static const char* getColorCode(LogLevel level) {
        switch(level) {
            case LogLevel::INFO:
                return "\033[1;34m";      // Blue
            case LogLevel::WARNING:
                return "\033[1;33m";     // Yellow
            case LogLevel::CRASH:
            case LogLevel::ERROR:
                return "\033[1;31m";     // Red
            case LogLevel::SUCCESS:
                return "\033[1;32m";     // Green
            default:
                return "\033[0m";        // Reset
        }
    }

    static const char* getLevelString(LogLevel level) {
        switch(level) {
            case LogLevel::INFO:
                return "[INFO]";
            case LogLevel::WARNING:
                return "[WARNING]";
            case LogLevel::CRASH:
            case LogLevel::ERROR:
                return "[ERROR]";
            case LogLevel::SUCCESS:
                return "[SUCCESS]";
            default:
                return "[LOG]";
        }
    }

    // Internal function untuk handle format string
    static void LogFormattedWithLocation(const char* format, LogLevel level, const char* file, int line, va_list args) {
        const char* colorCode = getColorCode(level);
        const char* levelStr = getLevelString(level);
        const char* reset = "\033[0m";

        // Print level dengan warna
        cout << colorCode << levelStr << " ";

        // Format dan print message dengan arguments
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), format, args);
        cout << buffer << " (" << file << ":" << line << ")" << reset << endl;
    }

    // Overload tanpa arguments
    static void LogWithLocation(const char* format, LogLevel level, const char* file, int line) {
        const char* colorCode = getColorCode(level);
        const char* levelStr = getLevelString(level);
        const char* reset = "\033[0m";

        cout << colorCode << levelStr << " " << format << " (" << file << ":" << line << ")" << reset << endl;
    }

    static void Log(const string& message, LogLevel level = LogLevel::INFO) {
        const char* colorCode = getColorCode(level);
        const char* levelStr = getLevelString(level);
        const char* reset = "\033[0m";

        cout << colorCode << levelStr << " ";
        cout << message << reset << endl;
    }

    // Variadic template version untuk mendukung format string dengan arguments
    template<typename... Args>
    static void Log(const char* format, LogLevel level, Args... args) {
        const char* colorCode = getColorCode(level);
        const char* levelStr = getLevelString(level);
        const char* reset = "\033[0m";

        cout << colorCode << levelStr << " ";

        char buffer[1024];
        snprintf(buffer, sizeof(buffer), format, args...);
        cout << buffer << reset << endl;
    }

    // Variadic template untuk format tanpa level (default INFO)
    template<typename... Args>
    static void Log(const char* format, Args... args) {
        Log(format, LogLevel::INFO, args...);
    }

    template<typename T>
    static void LogPointer(const string& name, T handle, LogLevel level = LogLevel::INFO) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), 
            "%s | Type: %s | Address: %p | Decimal: %lu", 
            name.c_str(),
            typeid(handle).name(),
            (void*)handle,
            (unsigned long)handle
        );
        Log(buffer, level);
    }
    
    // ============================================
    // MACRO DEFINITIONS dengan automatic file/line
    // ============================================

    // Macro untuk simple string logging
    #define DEBUG_LOG(format, ...) \
        do { \
            const char* colorCode = Debug::getColorCode((Debug::LogLevel::INFO)); \
            const char* levelStr = Debug::getLevelString((Debug::LogLevel::INFO)); \
            const char* reset = "\033[0m"; \
            cout << colorCode << levelStr << " "; \
            char buffer[1024]; \
            snprintf(buffer, sizeof(buffer), (format), ##__VA_ARGS__); \
            cout << buffer << " (" << __FILE__ << ":" << __LINE__ << ")" << reset << endl; \
        } while(0)

    // Macro untuk format string dengan arguments
    #define DEBUG_LOGF(format, level , ...) \
        do { \
            const char* colorCode = Debug::getColorCode((level)); \
            const char* levelStr = Debug::getLevelString((level)); \
            const char* reset = "\033[0m"; \
            cout << colorCode << levelStr << " "; \
            char buffer[1024]; \
            snprintf(buffer, sizeof(buffer), (format), ##__VA_ARGS__); \
            cout << buffer << " (" << __FILE__ << ":" << __LINE__ << ")" << reset << endl; \
        } while(0)

    // Macro untuk pointer logging
    #define DEBUG_LOG_POINTER(name, handle, level) \
        do { \
            char buffer[256]; \
            snprintf(buffer, sizeof(buffer), \
                "%s | Type: %s | Address: %p | Decimal: %lu", \
                (name), \
                typeid(handle).name(), \
                (void*)(handle), \
                (unsigned long)(handle) \
            ); \
            Debug::LogWithLocation(buffer, (level), __FILE__, __LINE__); \
        } while(0)

    // Macro untuk error checking (seperti VK_CHECK_RESULT)
    #define DEBUG_ASSERT(condition, message, level) \
        do { \
            if (!(condition)) { \
                const char* colorCode = Debug::getColorCode((level)); \
                const char* levelStr = Debug::getLevelString((level)); \
                const char* reset = "\033[0m"; \
                cout << colorCode << levelStr << " ASSERTION FAILED: " << (message) \
                     << " (" << __FILE__ << ":" << __LINE__ << ")" << reset << endl; \
                assert((condition)); \
            } \
        } while(0)

}
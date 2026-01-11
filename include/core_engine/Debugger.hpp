#pragma once
#include <iostream>
#include <string>
#include <cstdio>
#include <cstdarg>

#ifdef __linux__
#include <gtk/gtk.h>
#endif
using namespace std;

#define TITLE "Ilmeee Engine"
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
    #define DEBUG_LOG_POINTER(name, handle ,level) \
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

    #if defined(__WIN32__) || defined(__WIN64__)
    #include <windows.h>

    // ============================================
    // WINDOW MESSAGE BOX MACROS
    // ============================================

    static void ShowMsgBoxWithLocation(const wchar_t *title,
                                    const std::wstring &message,
                                    const char *file, int line, UINT type) {
        // Convert filename to wide char
        wchar_t wFile[1024];
        size_t converted = 0;
        mbstowcs_s(&converted, wFile, sizeof(wFile) / sizeof(wchar_t), file,
                    _TRUNCATE);

        // Convert line number
        wchar_t wLine[32];
        swprintf_s(wLine, sizeof(wLine) / sizeof(wchar_t), L"%d", line);

        // Combine
        std::wstring fullMessage = message + L"\n\n(" + wFile + L":" + wLine + L")";

        // Show
        MessageBoxW(NULL, fullMessage.c_str(), title, type);
    }

    // ============================================
    // WINDOW MESSAGE BOX MACROS
    // ============================================
    #define MSGBOX_TITLE L"Ilmeee Engine"
    // Info message box (blue icon)
    #define MSGBOX_INFO(message)                                                   \
    Debug::ShowMsgBoxWithLocation(MSGBOX_TITLE, (std::wstring)(message),         \
                                    __FILE__, __LINE__,                            \
                                    MB_OK | MB_ICONINFORMATION)

    // Success message box (green checkmark)
    #define MSGBOX_SUCCESS(message)                                                \
    Debug::ShowMsgBoxWithLocation(MSGBOX_TITLE, (std::wstring)(message),         \
                                    __FILE__, __LINE__,                            \
                                    MB_OK | MB_ICONINFORMATION)

    // Warning message box (yellow exclamation)
    #define MSGBOX_WARNING(message)                                                \
    Debug::ShowMsgBoxWithLocation(MSGBOX_TITLE, (std::wstring)(message),         \
                                    __FILE__, __LINE__, MB_OK | MB_ICONWARNING)

    // Error/Crash message box (red X)
    #define MSGBOX_ERROR(message)                                                  \
    Debug::ShowMsgBoxWithLocation(MSGBOX_TITLE, (std::wstring)(message),         \
                                    __FILE__, __LINE__, MB_OK | MB_ICONERROR)

    // Crash message box with abort option
    #define MSGBOX_CRASH(message)                                                  \
    do {                                                                         \
        Debug::ShowMsgBoxWithLocation(MSGBOX_TITLE, (std::wstring)(message),       \
                                    __FILE__, __LINE__,                          \
                                    MB_ABORTRETRYIGNORE | MB_ICONERROR);         \
        abort();                                                                   \
    } while (0)

    // Format message box with printf-style formatting
    #define MSGBOX_INFOF(title, format, ...)                                       \
    do {                                                                         \
        wchar_t buffer[1024];                                                      \
        swprintf_s(buffer, sizeof(buffer) / sizeof(wchar_t), (format),             \
                ##__VA_ARGS__);                                                 \
        MessageBoxW(NULL, buffer, (title), MB_OK | MB_ICONINFORMATION);            \
    } while (0)

    #define MSGBOX_WARNINGF(title, format, ...)                                    \
    do {                                                                         \
        wchar_t buffer[1024];                                                      \
        swprintf_s(buffer, sizeof(buffer) / sizeof(wchar_t), (format),             \
                ##__VA_ARGS__);                                                 \
        MessageBoxW(NULL, buffer, (title), MB_OK | MB_ICONWARNING);                \
    } while (0)

    #define MSGBOX_ERRORF(title, format, ...)                                      \
    do {                                                                         \
        wchar_t buffer[1024];                                                      \
        swprintf_s(buffer, sizeof(buffer) / sizeof(wchar_t), (format),             \
                ##__VA_ARGS__);                                                 \
        MessageBoxW(NULL, buffer, (title), MB_OK | MB_ICONERROR);                  \
    } while (0)

    // ANSI string version (converts to wide string automatically)
    #define MSGBOX_INFO_A(title, message)                                          \
    do {                                                                         \
        MessageBoxA(NULL, (message), (title), MB_OK | MB_ICONINFORMATION);         \
    } while (0)

    #define MSGBOX_SUCCESS_A(title, message)                                       \
    do {                                                                         \
        MessageBoxA(NULL, (message), (title), MB_OK | MB_ICONINFORMATION);         \
    } while (0)

    #define MSGBOX_WARNING_A(title, message)                                       \
    do {                                                                         \
        MessageBoxA(NULL, (message), (title), MB_OK | MB_ICONWARNING);             \
    } while (0)

    #define MSGBOX_ERROR_A(title, message)                                         \
    do {                                                                         \
        MessageBoxA(NULL, (message), (title), MB_OK | MB_ICONERROR);               \
    } while (0)

    #endif

    // If not windows or apple is used gtk
    #if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
    // Helper macros for stringification
    #define _MSGBOX_STRINGIFY(x) #x
    #define _MSGBOX_TOSTRING(x) _MSGBOX_STRINGIFY(x)

        // Show a GTK message box for debugging
    // Usage: Debug::ShowBox(GTK_WINDOW(parent_widget), "Your message");
    //        Debug::ShowBox(nullptr, "Message without parent");
    static void ShowBox(GtkWindow *parent, const char* message, GtkMessageType type = GTK_MESSAGE_INFO) {
        GtkWidget *dialog = gtk_message_dialog_new(
            parent,
            GTK_DIALOG_DESTROY_WITH_PARENT,
            type,
            GTK_BUTTONS_CLOSE,
            "%s",
            message
        );
        gtk_window_set_title(GTK_WINDOW(dialog), TITLE);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }

    // Overload for std::string
    static void ShowBox(GtkWindow *parent, const string& message, GtkMessageType type = GTK_MESSAGE_INFO) {
        ShowBox(parent, message.c_str(), type);
    }


    // Auto-close timeout callback for GTK dialogs
    static gboolean _msgbox_auto_close(gpointer data) {
        GtkWidget *dialog = GTK_WIDGET(data);
        gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);
        return FALSE; // Don't repeat
    }

    // Auto-close timeout in milliseconds (2 seconds)
    #define MSGBOX_AUTO_CLOSE_MS 5000

    #define MSGBOX_INFO(parent, message) \
        do { \
            GtkWidget *dialog; \
            char _msgbox_buf[2048]; \
            snprintf(_msgbox_buf, sizeof(_msgbox_buf), "%s\n\n[%s:%d]", (message), __FILE__, __LINE__); \
            dialog = gtk_message_dialog_new( \
                GTK_WINDOW(parent), \
                GTK_DIALOG_DESTROY_WITH_PARENT, \
                GTK_MESSAGE_INFO, \
                GTK_BUTTONS_CLOSE, \
                "%s", \
                _msgbox_buf \
            ); \
            gtk_window_set_title(GTK_WINDOW(dialog), TITLE); \
            g_timeout_add(MSGBOX_AUTO_CLOSE_MS, Debug::_msgbox_auto_close, dialog); \
            gtk_dialog_run(GTK_DIALOG(dialog)); \
            gtk_widget_destroy(dialog); \
        } while(0)

    #define MSGBOX_INFOF(parent, format, ...) \
        do { \
            GtkWidget *dialog; \
            char _msgbox_msg[1024]; \
            char _msgbox_buf[2048]; \
            snprintf(_msgbox_msg, sizeof(_msgbox_msg), format, ##__VA_ARGS__); \
            snprintf(_msgbox_buf, sizeof(_msgbox_buf), "%s\n\n[%s:%d]", _msgbox_msg, __FILE__, __LINE__); \
            dialog = gtk_message_dialog_new( \
                GTK_WINDOW(parent), \
                GTK_DIALOG_DESTROY_WITH_PARENT, \
                GTK_MESSAGE_INFO, \
                GTK_BUTTONS_CLOSE, \
                "%s", \
                _msgbox_buf \
            ); \
            gtk_window_set_title(GTK_WINDOW(dialog), TITLE); \
            g_timeout_add(MSGBOX_AUTO_CLOSE_MS, Debug::_msgbox_auto_close, dialog); \
            gtk_dialog_run(GTK_DIALOG(dialog)); \
            gtk_widget_destroy(dialog); \
        } while(0)
    
    #define MSGBOX_ERRORF(parent, format, ...) \
        do { \
            GtkWidget *dialog; \
            char _msgbox_msg[1024]; \
            char _msgbox_buf[2048]; \
            snprintf(_msgbox_msg, sizeof(_msgbox_msg), format, ##__VA_ARGS__); \
            snprintf(_msgbox_buf, sizeof(_msgbox_buf), "%s\n\n[%s:%d]", _msgbox_msg, __FILE__, __LINE__); \
            dialog = gtk_message_dialog_new( \
                GTK_WINDOW(parent), \
                GTK_DIALOG_DESTROY_WITH_PARENT, \
                GTK_MESSAGE_ERROR, \
                GTK_BUTTONS_CLOSE, \
                "%s", \
                _msgbox_buf \
            ); \
            gtk_window_set_title(GTK_WINDOW(dialog), TITLE); \
            g_timeout_add(MSGBOX_AUTO_CLOSE_MS, Debug::_msgbox_auto_close, dialog); \
            gtk_dialog_run(GTK_DIALOG(dialog)); \
            gtk_widget_destroy(dialog); \
        } while(0)
    
    #define MSGBOX_WARNINGF(parent, format, ...) \
        do { \
            GtkWidget *dialog; \
            char _msgbox_msg[1024]; \
            char _msgbox_buf[2048]; \
            snprintf(_msgbox_msg, sizeof(_msgbox_msg), format, ##__VA_ARGS__); \
            snprintf(_msgbox_buf, sizeof(_msgbox_buf), "%s\n\n[%s:%d]", _msgbox_msg, __FILE__, __LINE__); \
            dialog = gtk_message_dialog_new( \
                GTK_WINDOW(parent), \
                GTK_DIALOG_DESTROY_WITH_PARENT, \
                GTK_MESSAGE_WARNING, \
                GTK_BUTTONS_CLOSE, \
                "%s", \
                _msgbox_buf \
            ); \
            gtk_window_set_title(GTK_WINDOW(dialog), TITLE); \
            g_timeout_add(MSGBOX_AUTO_CLOSE_MS, Debug::_msgbox_auto_close, dialog); \
            gtk_dialog_run(GTK_DIALOG(dialog)); \
            gtk_widget_destroy(dialog); \
        } while(0)
    #endif
}
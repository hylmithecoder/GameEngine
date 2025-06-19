#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_ttf.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>
#include <csignal>
#include <vector>
#include <functional>
#include <unistd.h>
#include <sys/wait.h>
#include <Application.hpp>
#include "SceneRenderer2D.hpp"
#include <Debugger.hpp>

// Global state management
std::atomic<bool> g_shutdown_requested{false};

// Global application manager
std::unique_ptr<ApplicationManager> g_app;

// Signal handlers for proper cleanup
void SignalHandler(int signal) {
    const char* signal_name = "UNKNOWN";
    switch(signal) {
        case SIGINT:  signal_name = "SIGINT (Ctrl+C)"; break;
        case SIGTERM: signal_name = "SIGTERM"; break;
        case SIGABRT: signal_name = "SIGABRT"; break;
        case SIGQUIT: signal_name = "SIGQUIT"; break;
        case SIGHUP:  signal_name = "SIGHUP"; break;
    }
    
    Debug::Logger::Log("Received signal: " + std::string(signal_name) + " (" + std::to_string(signal) + ")");
    
    // Set shutdown flag
    g_shutdown_requested.store(true);
    
    if (g_app) {
        g_app->Shutdown();
    }
    
    // Give some time for cleanup
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    exit(signal);
}

// Setup signal handlers for Linux
void SetupSignalHandlers() {
    struct sigaction sa;
    sa.sa_handler = SignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; // Restart interrupted system calls
    
    // Handle common termination signals
    sigaction(SIGINT, &sa, nullptr);   // Ctrl+C
    sigaction(SIGTERM, &sa, nullptr);  // Termination request
    sigaction(SIGABRT, &sa, nullptr);  // Abort signal
    sigaction(SIGQUIT, &sa, nullptr);  // Quit signal (Ctrl+\)
    sigaction(SIGHUP, &sa, nullptr);   // Hang up signal
    
    // Ignore SIGPIPE to prevent crashes on broken pipes
    signal(SIGPIPE, SIG_IGN);
    
    Debug::Logger::Log("Signal handlers configured for Linux");
}

// Check if running in terminal
bool IsRunningInTerminal() {
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
}

// Print startup information
void PrintStartupInfo() {
    Debug::Logger::Log("=== Ilmee Editor Starting ===");
    Debug::Logger::Log("Platform: Linux");
    Debug::Logger::Log("Terminal: " + std::string(IsRunningInTerminal() ? "Yes" : "No"));
    
    // Get process ID
    Debug::Logger::Log("Process ID: " + std::to_string(getpid()));
    
    // Get user information
    char* user = getenv("USER");
    if (user) {
        Debug::Logger::Log("User: " + std::string(user));
    }
    
    // Get display information
    char* display = getenv("DISPLAY");
    if (display) {
        Debug::Logger::Log("Display: " + std::string(display));
    } else {
        Debug::Logger::Log("Display: Not set (may be running headless)");
    }
    
    // Check for Wayland
    char* wayland_display = getenv("WAYLAND_DISPLAY");
    if (wayland_display) {
        Debug::Logger::Log("Wayland Display: " + std::string(wayland_display));
    }
}

// Check system requirements
bool CheckSystemRequirements() {
    Debug::Logger::Log("Checking system requirements...");
    
    // Check if we have access to display
    if (!getenv("DISPLAY") && !getenv("WAYLAND_DISPLAY")) {
        Debug::Logger::Log("Warning: No display environment detected", Debug::LogLevel::WARNING);
        Debug::Logger::Log("Make sure you're running in a graphical environment or via SSH with X11 forwarding", Debug::LogLevel::WARNING);
    }
    
    // Check SDL version
    SDL_version compiled, linked;
    SDL_VERSION(&compiled);
    SDL_GetVersion(&linked);
    
    Debug::Logger::Log("SDL Version - Compiled: " + 
                      std::to_string(compiled.major) + "." + 
                      std::to_string(compiled.minor) + "." + 
                      std::to_string(compiled.patch));
    Debug::Logger::Log("SDL Version - Linked: " + 
                      std::to_string(linked.major) + "." + 
                      std::to_string(linked.minor) + "." + 
                      std::to_string(linked.patch));
    
    return true;
}

// Main application entry point
int main(int argc, char* argv[]) {
    // Print startup information
    PrintStartupInfo();
    
    // Set up signal handlers
    SetupSignalHandlers();
    
    // Check system requirements
    if (!CheckSystemRequirements()) {
        Debug::Logger::Log("System requirements check failed", Debug::LogLevel::CRASH);
        return 1;
    }
    
    try {        
        // Create application manager
        Debug::Logger::Log("Creating application manager...");
        g_app = std::make_unique<ApplicationManager>();
        
        // Launch engine
        Debug::Logger::Log("Launching engine...");
        if (!g_app->LaunchEngine()) {
            Debug::Logger::Log("Failed to launch engine", Debug::LogLevel::CRASH);
            return 1;
        }
        
        // Initialize application
        Debug::Logger::Log("Initializing application...");
        if (!g_app->Initialize()) {
            Debug::Logger::Log("Failed to initialize application", Debug::LogLevel::CRASH);
            return 1;
        }
        
        Debug::Logger::Log("Application initialized successfully");
        Debug::Logger::Log("Entering main loop...");
        
        // Run main loop
        g_app->Run();
        
        Debug::Logger::Log("Main loop exited");
        
        // Explicit shutdown before cleanup
        Debug::Logger::Log("Shutting down application...");
        g_app->Shutdown();
        g_app.reset();
        g_app = nullptr;
        
        Debug::Logger::Log("Application manager cleaned up");
        Debug::Logger::Log("=== Ilmee Editor Terminated Successfully ===");
        return 0;
        
    } catch (const std::exception& e) {
        Debug::Logger::Log("Unhandled exception: " + std::string(e.what()), Debug::LogLevel::CRASH);
        
        if (g_app) {
            try {
                g_app->Shutdown();
                g_app.reset();
                g_app = nullptr;
            } catch (...) {
                Debug::Logger::Log("Exception during emergency shutdown", Debug::LogLevel::CRASH);
            }
        }
        
        Debug::Logger::Log("=== Ilmee Editor Terminated with Error ===");
        return 1;
        
    } catch (...) {
        Debug::Logger::Log("Unknown exception caught", Debug::LogLevel::CRASH);
        
        if (g_app) {
            try {
                g_app->Shutdown();
                g_app.reset();
                g_app = nullptr;
            } catch (...) {
                // Ignore exceptions during emergency shutdown
            }
        }
        
        Debug::Logger::Log("=== Ilmee Editor Terminated with Unknown Error ===");
        return 1;
    }
}
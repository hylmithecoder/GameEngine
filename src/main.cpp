#define _WIN32_WINNT 0x0A00
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
#include <Application.hpp>
#include "SceneRenderer2D.hpp"
#include <Debugger.hpp>

// Global state management


// Global application manager
std::unique_ptr<ApplicationManager> g_app;

// Signal handlers for proper cleanup
void SignalHandler(int signal) {
    Debug::Logger::Log("Received signal: " + std::to_string(signal));
    if (g_app) {
        g_app->Shutdown();
    }
    exit(signal);
}

// Windows console handler for Ctrl+C, close button, etc.
BOOL WINAPI ConsoleHandler(DWORD signal) {
    Debug::Logger::Log("Console event: " + std::to_string(signal));
    if (g_app) {
        g_app->Shutdown();
    }
    return TRUE;
}

int main(int argc, char* argv[]) {
    // Set up signal handlers
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    std::signal(SIGABRT, SignalHandler);
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    
    try {        
        // Create application manager
        g_app = std::make_unique<ApplicationManager>();
        
        // Launch engine
        if (!g_app->LaunchEngine()) {
            Debug::Logger::Log("Failed to launch engine", Debug::LogLevel::CRASH);
            
            return 1;
        }
        
        // Initialize application
        if (!g_app->Initialize()) {
            Debug::Logger::Log("Failed to initialize application", Debug::LogLevel::CRASH);
            return 1;
        }
        
        
        // Run main loop
        g_app->Run();
        
        // Explicit shutdown before cleanup
        g_app->Shutdown();
        g_app.reset();
        Debug::Logger::Log("Application manager cleaned up");
        g_app = nullptr;
        
        Debug::Logger::Log("=== Ilmee Editor Terminated Successfully ===");
        return 0;
    } catch (const std::exception& e) {
        Debug::Logger::Log("Unhandled exception: " + std::string(e.what()), Debug::LogLevel::CRASH);
        if (g_app) {
            g_app->Shutdown();
            g_app.reset();
            g_app = nullptr;
        }
        return 1;
    }
    
    // Cleanup will be called automatically by ApplicationManager destructor
    g_app.reset();
    
    Debug::Logger::Log("=== Ilmee Editor Terminated ===");
    return 0;
}
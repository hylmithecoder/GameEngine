// Launcher.cpp
#include <Windows.h>
#include <iostream>
#include <thread>
#include <chrono>

// Function typedefs
typedef bool (*EngineInitFunc)(const char*, int, int);
typedef void (*EngineRunFunc)();
typedef void (*EngineShutdownFunc)();

void ShowLoadingWindow() {
    // Simple fake loading window (replace with your own SDL2 or Win32 window)
    std::cout << "============================\n";
    std::cout << "   Starting Game Engine...   \n";
    std::cout << "============================\n";
    for (int i = 0; i < 5; ++i) {
        std::cout << ".";
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    std::cout << "\nDone!\n";
}

int main() {
    HMODULE engineDLL = LoadLibraryA("bin/libCoreEngine.dll");
    if (!engineDLL) {
        MessageBoxA(nullptr, "Failed to load CoreEngine.dll", "Launcher", MB_ICONERROR);
        return -1;
    }

    EngineInitFunc Init = (EngineInitFunc)GetProcAddress(engineDLL, "EngineInit");
    EngineRunFunc Run = (EngineRunFunc)GetProcAddress(engineDLL, "EngineRun");
    EngineShutdownFunc Shutdown = (EngineShutdownFunc)GetProcAddress(engineDLL, "EngineShutdown");

    if (!Init || !Run || !Shutdown) {
        MessageBoxA(nullptr, "Failed to find one or more engine functions", "Launcher", MB_ICONERROR);
        return -1;
    }

    ShowLoadingWindow();

    if (!Init("Ilmee Game Engine", 1280, 720)) {
        MessageBoxA(nullptr, "Engine failed to initialize", "Launcher", MB_ICONERROR);
        return -1;
    }

    Run();
    Shutdown();

    FreeLibrary(engineDLL);
    return 0;
}

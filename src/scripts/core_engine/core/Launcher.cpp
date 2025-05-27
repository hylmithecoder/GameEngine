// Launcher.cpp
#include <Windows.h>
#include <iostream>
#include <thread>
#include <chrono>

// Function typedefs
typedef bool (*EngineInitFunc)(const char*, int, int);
typedef void (*EngineRunFunc)();
typedef void (*EngineShutdownFunc)();
typedef bool (*EditorInitFunc)(const char*, int, int);

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
    HMODULE engineDLL = LoadLibraryA("bin/libIlmeeeEngine.dll");
    if (!engineDLL) {
        MessageBoxA(nullptr, "Failed to load libIlmeeeEngine.dll", "Launcher", MB_ICONERROR);
        return -1;
    }

    HMODULE editorDLL = LoadLibraryA("bin/libIlmeeeEditor.dll");
    if (!editorDLL) {
        MessageBoxA(nullptr, "Failed to load IlmeeeEditor.dll", "Launcher", MB_ICONERROR);
        FreeLibrary(engineDLL);
        return -1;
    }

    EngineInitFunc Init = (EngineInitFunc)GetProcAddress(engineDLL, "EngineInit");
    EngineRunFunc Run = (EngineRunFunc)GetProcAddress(engineDLL, "EngineRun");
    EngineShutdownFunc Shutdown = (EngineShutdownFunc)GetProcAddress(engineDLL, "EngineShutdown");
    EditorInitFunc InitEditor = (EditorInitFunc)GetProcAddress(editorDLL, "EditorInit");

    if (!Init || !Run || !Shutdown) {
        MessageBoxA(nullptr, "Failed to find one or more engine functions", "Launcher", MB_ICONERROR);
        return -1;
    }

    ShowLoadingWindow();

    if (!Init("Ilmee Game Engine", 1280, 720)) {
        MessageBoxA(nullptr, "Engine failed to initialize", "Launcher", MB_ICONERROR);
        return -1;
    }

    // Start engine's TCP server
    typedef bool (*StartServerFunc)();
    StartServerFunc StartServer = (StartServerFunc)GetProcAddress(editorDLL, "StartServer");
    if (!StartServer || !StartServer()) {
        MessageBoxA(nullptr, "Failed to start engine server", "Launcher", MB_ICONERROR);
        return -1;
    }
    if (!InitEditor("Ilmee Editor", 1280, 720)) {
        MessageBoxA(nullptr, "Editor failed to initialize", "Launcher", MB_ICONERROR);
        Shutdown();
        return -1;
    }

    // Connect editor to engine
    typedef bool (*ConnectToEngineFunc)();
    ConnectToEngineFunc ConnectToEngine = (ConnectToEngineFunc)GetProcAddress(editorDLL, "ConnectToEngine");
    if (!ConnectToEngine || !ConnectToEngine()) {
        MessageBoxA(nullptr, "Failed to connect editor to engine", "Launcher", MB_ICONERROR);
        return -1;
    }
    typedef bool (*SendCommandToEngineFunc)(const char*);
    SendCommandToEngineFunc SendCommandToEngine = (SendCommandToEngineFunc)GetProcAddress(editorDLL, "SendCommandToEngine");
    if (!SendCommandToEngine || !SendCommandToEngine("Halo From Launcher.cpp")) {
        MessageBoxA(nullptr, "Failed to send initial command to engine", "Launcher", MB_ICONERROR);
        return -1;
    }

    Run();
    Shutdown();

    FreeLibrary(engineDLL);
    FreeLibrary(editorDLL);
    return 0;
}

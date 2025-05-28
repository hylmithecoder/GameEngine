#include <Windows.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <functional>
#include <CommCtrl.h>
#include "HandlerLauncher.cpp"
using namespace std;

int main() {
    // Initialize COM for modern UI effects
    CoInitialize(nullptr);
    
    LoadingWindow loadingWindow;
    DLLManager dllManager;
    
    if (!loadingWindow.Create()) {
        DLLManager::ShowError("Failed to create loading window");
        return -1;
    }
    
    loadingWindow.Show();
    loadingWindow.StartLoadingAnimation();
    
    // Execute loading sequence
    LaunchSequence sequence(loadingWindow, dllManager);
    bool success = sequence.Execute();
    
    loadingWindow.Hide();
    
    if (!success) {
        CoUninitialize();
        return -1;
    }
    
    // Run the engine
    auto Run = dllManager.GetFunction<EngineRunFunc>(dllManager.GetEngineDLL(), "EngineRun");
    auto Shutdown = dllManager.GetFunction<EngineShutdownFunc>(dllManager.GetEngineDLL(), "EngineShutdown");
    auto SendCommand = dllManager.GetFunction<SendCommandToEngineFunc>(dllManager.GetEngineDLL(), "SendCommandToEngine");
    if (Run && Shutdown) {
        Run();
        Shutdown();
        SendCommand("Exit");
    }
    
    CoUninitialize();
    return 0;
}
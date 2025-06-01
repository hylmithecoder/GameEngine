#include <Windows.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <functional>
#include <CommCtrl.h>
#include "HandlerLauncher.cpp"
using namespace std;
using namespace Debug;

typedef string (*TestString)();
typedef string (*ReceivedMessages)();
int main(int argc, char* argv[]) {
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
    // This not working
    // auto RunEditor = dllManager.GetFunction<EditorRunFunc>(dllManager.GetEditorDLL(), "EditorRun");
    SendCommandToEngineFunc SendCommand = dllManager.GetFunction<SendCommandToEngineFunc>(dllManager.GetEngineDLL(), "SendCommandToEngine");

    if (Run && Shutdown) {
        cout << "Running engine..." << endl;
        Run();
    }

    CoUninitialize();
    return 0;
}
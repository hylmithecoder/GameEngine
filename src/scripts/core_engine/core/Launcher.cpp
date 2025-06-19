// #include <Windows.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <functional>
// #include <CommCtrl.h>
#include "HandlerLauncher.cpp"
using namespace std;
using namespace Debug;

typedef string (*ReceivedMessages)();

int main(int argc, char* argv[]) {
    // Initialize COM for modern UI effects
    // CoInitialize(nullptr);
    
    LoadingWindow loadingWindow;
    LibraryManager libManager;
    // DLLManager dllManager;
    
    if (!loadingWindow.Create()) {

        LibraryManager::ShowError("Failed to create loading window");
        return -1;
    }
    
    loadingWindow.Show();
    // loadingWindow.StartLoadingAnimation();
    
    // Execute loading sequence
    LaunchSequence sequence(loadingWindow, libManager);
    bool success = sequence.Execute();
    
    loadingWindow.Hide();
    
    // if (!success) {
    //     CoUninitialize();
    //     return -1;
    // }
    
    // Run the engine
    auto Run = libManager.GetFunction<EngineRunFunc>(libManager.GetEngineLib(), "EngineRun");
    auto Shutdown = libManager.GetFunction<EngineShutdownFunc>(libManager.GetEngineLib(), "EngineShutdown");
    // This not working
    // auto RunEditor = dllManager.GetFunction<EditorRunFunc>(dllManager.GetEditorDLL(), "EditorRun");
    SendCommandToEngineFunc SendCommand = libManager.GetFunction<SendCommandToEngineFunc>(libManager.GetEditorLib(), "SendCommandToEngine");

    if (Run && Shutdown) {
        cout << "Running engine..." << endl;
        Run();
    }

    // CoUninitialize();
    return 0;
}
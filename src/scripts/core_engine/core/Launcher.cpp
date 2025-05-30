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
    
    TestString Test = dllManager.GetFunction<TestString>(dllManager.GetEditorDLL(), "TestString");
    if (Test) {
        Logger::Log("Test: " + Test(), LogLevel::SUCCESS);
    }
    
    ReceivedMessages messages = dllManager.GetFunction<ReceivedMessages>(dllManager.GetEditorDLL(), "GetCommandFromEngine");
    if (messages)
    {
        Logger::Log("Pesan Di terima dari Editor", LogLevel::SUCCESS);
        Logger::Log("Messages: " + messages(), LogLevel::SUCCESS);
    }

    if (Run && Shutdown) {
        cout << "Running engine..." << endl;
        // std::vector<std::string> messages = sequence.GetPendingMessages();
        // for (const auto& msg : messages) {
        //     cout << msg << endl;
        // }
        while (true)
        {
            SendCommand("Say Hi From Runtime");
            Run();
        }
        // cout << "After Run" << endl;
        // thread editorThread([&]() { RunEditor(); });
        // editorThread.join();
        // Shutdown();
        // SendCommand("Exit");
        // RunEditor();
        // Shutdown();
        // while (true)
        // {
        //     SendCommand("Hallo Guys if you see this in runtime this is just loop for test");
        // }
    }
    cout << "After Run" << endl;
    Shutdown();
    CoUninitialize();
    return 0;
}
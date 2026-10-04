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

// Started by the editor (GameEngineSDL) as
//   HandlerIlmeeeEngine --ipc-url ws://127.0.0.1:<port>/
// with the session token in ILMEEE_IPC_TOKEN. Without --ipc-url it still runs,
// just without an editor to talk to.
int main(int argc, char* argv[]) {
    using namespace ilmeee::net;

    std::string ipcUrl;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == proto::kIpcUrlArg && i + 1 < argc)
            ipcUrl = argv[++i];
    }
    std::string ipcToken;
    if (const char *t = std::getenv(proto::kIpcTokenEnv)) {
        ipcToken = t;
        // Anything this process spawns has no business holding it.
        unsetenv(proto::kIpcTokenEnv);
    }

    LoadingWindow loadingWindow;
    LibraryManager libManager;

    if (!loadingWindow.Create()) {
        LibraryManager::ShowError("Failed to create loading window");
        return -1;
    }

    loadingWindow.Show();

    // Execute loading sequence
    LaunchSequence sequence(loadingWindow, libManager, ipcUrl, ipcToken);
    bool success = sequence.Execute();

    loadingWindow.Hide();

    if (!success) {
        sequence.Shutdown();
        return -1;
    }

    // Run the engine until it is stopped (engine.stop from the editor, or the
    // editor going away).
    auto Run = libManager.GetFunction<EngineRunFunc>(libManager.GetEngineLib(), "EngineRun");
    auto Shutdown = libManager.GetFunction<EngineShutdownFunc>(libManager.GetEngineLib(), "EngineShutdown");

    if (Run && !sequence.StopRequested()) {
        cout << "Running engine..." << endl;
        Run();
    }

    sequence.Shutdown();
    if (Shutdown)
        Shutdown();
    return 0;
}

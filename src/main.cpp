#define _WIN32_WINNT 0x0A00
#define SDL_MAIN_HANDLED
#include <SDL.h>
// #include <SDL_syswm.h>
#include <SDL_ttf.h>
#include "header/ui/MainWindow.hpp"
#include "scripts/ui/Check_Environment.cpp"
#include <NetworkManager.hpp>
#include "SceneRenderer2D.hpp"
#include <Debugger.hpp>

NetworkManager networkManager;

void LaunchGame() {
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    // Start TCP server before launching the game
    if (!networkManager.startServer()) {
        MessageBoxA(0, "Failed to start network server", "Error", MB_OK);
        return;
    }

    std::string command = "HandlerIlmeeeEngine.exe -project MyGameProject";
    BOOL result = CreateProcessA(
        NULL, command.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi
    );

    if (result) {
        // Wait for connection establishment
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Send initial configuration
        networkManager.sendMessage("init:MyGameProject");
        
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        MessageBoxA(0, "Failed to launch HandlerIlmeeeEngine.exe", "Error", MB_OK);
    }
}

int main(int argc, char* argv[]) {
    // Check environment
    LaunchGame();
    for (int i = 0; i < 5; ++i) {
        std::cout << "";
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    Environment env;
    env.detectDriveInfo();
    env.printEnvironment();
    env.printDriveInfo();
    av_log_set_level(AV_LOG_ERROR);
    
    // SceneRenderer2D renderer(1280, 720);


    // assets.load_assets();
    // Init SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        SDL_Log("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    MainWindow window("Ilmeee Editor", 1280, 720);

    char cwd[512];
    _getcwd(cwd, sizeof(cwd));
    std::cout << "Working Directory: " << cwd << std::endl;

    // window.assets.load_assets();

    while (window.running()) {

        std::string message = networkManager.receiveMessage();
        if (!message.empty()) {
            Debug::Logger::Log("Received message: " + message);
        }
        std::string sendMessage = "Halo World";
        networkManager.sendMessage("Halo World");

        window.handleEvents();
        window.update();
        window.render();
    }

    SDL_Quit();
    return 0;
}


// int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
//     return main(__argc, __argv);
// }
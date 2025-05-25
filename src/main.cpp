#define _WIN32_WINNT 0x0A00
#define SDL_MAIN_HANDLED
#include <SDL.h>
// #include <SDL_syswm.h>
#include <SDL_ttf.h>
#include "header/ui/MainWindow.hpp"
#include "scripts/ui/Check_Environment.cpp"
#include "SceneRenderer2D.hpp"

void LaunchGame() {
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    std::string command = "HandlerIlmeeeEngine.exe -project MyGameProject";

    BOOL result = CreateProcessA(
        NULL,               // No module name (use command line)
        command.data(),     // Command line
        NULL,               // Process handle not inheritable
        NULL,               // Thread handle not inheritable
        FALSE,              // Set handle inheritance to FALSE
        0,                  // No creation flags
        NULL,               // Use parent's environment block
        NULL,               // Use parent's starting directory 
        &si,                // Pointer to STARTUPINFO structure
        &pi                 // Pointer to PROCESS_INFORMATION structure
    );

    if (result) {
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
#define _WIN32_WINNT 0x0A00
#define SDL_MAIN_HANDLED
#include <SDL.h>
// #include <SDL_syswm.h>
#include <SDL_ttf.h>
#include "header/ui/MainWindow.hpp"
#include "scripts/ui/Check_Environment.cpp"
#include "SceneRenderer2D.hpp"

int main(int argc, char* argv[]) {
    // Check environment
    
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

    MainWindow window("GameEngine SDL", 1280, 720);

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
#include "assets.hpp"
#include <iostream>
#include <SDL.h>
#include <SDL_opengl.h>
using namespace std;
// Untuk memuat gambar
// #define STB_IMAGE_IMPLEMENTATION
// #include "stb_image.h"

int main(int argc, char* argv[])
{
    // Inisialisasi SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        cerr << "SDL gagal diinisialisasi: " << SDL_GetError() << endl;
        return -1;
    }

    // Membuat window SDL dengan OpenGL
    SDL_Window* window = SDL_CreateWindow("ImGui dengan Background Gambar", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(1); // Enable VSync

    // Inisialisasi ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    io.Fonts->AddFontFromFileTTF("assets/fonts/zh-cn.ttf", 16.0f);
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);   // Core
    ImGui_ImplOpenGL3_Init("#version 130"); // Core

    // Memuat tekstur menggunakan Assets
    Assets assetManager;
    TextureData backgroundTexture;
    if (!assetManager.LoadTextureFromFile("assets/images/backgrounds/shun_small.webp", &backgroundTexture)) {
        cerr << "Gagal memuat gambar background!" << endl;
    }
    else {
        cout << "Gambar background berhasil dimuat!" << endl;
    }
    cout << assetManager.LoadTextureFromFile("assets/images/backgrounds/shun_small.webp", &backgroundTexture) << endl;

    bool done = false;
    while (!done)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) done = true;
        }

        ImGui_ImplOpenGL3_NewFrame();   // Core
        ImGui_ImplSDL2_NewFrame();  // Core
        ImGui::NewFrame();

        // Menggambar background
        if (backgroundTexture.TextureID != 0) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::Begin("Background", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs);
            ImGui::Image((ImTextureID)(intptr_t)backgroundTexture.TextureID, io.DisplaySize);
            ImGui::End();
            ImGui::PopStyleVar(2);
        }

        // Contoh window sederhana
        ImGui::Begin("Hello!");
        ImGui::Text("Tekstur background telah dimuat.");
        ImGui::End();

        // Render
        ImGui::Render();
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // Cleanup
    if (backgroundTexture.TextureID != 0)
        glDeleteTextures(1, &backgroundTexture.TextureID);

    ImGui_ImplOpenGL3_Shutdown(); // Core
    ImGui_ImplSDL2_Shutdown();  // Core
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    return main(__argc, __argv);
}
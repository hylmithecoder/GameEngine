#include "../../include/ui/assets.hpp"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <iostream>

using namespace std;

int main(int argc, char *argv[]) {
  // Inisialisasi SDL
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    cerr << "SDL gagal diinisialisasi: " << SDL_GetError() << endl;
    return -1;
  }

  // Membuat window SDL dengan Vulkan support
  SDL_Window *window =
      SDL_CreateWindow("ImGui dengan Background Gambar", 1280, 720,
                       SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

  if (!window) {
    cerr << "Gagal membuat window: " << SDL_GetError() << endl;
    return -1;
  }

  // Inisialisasi ImGui
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  io.Fonts->AddFontFromFileTTF("assets/fonts/zh-cn.ttf", 16.0f);
  ImGui::StyleColorsDark();

  ImGui_ImplSDL3_InitForVulkan(window);
  // ImGui_ImplVulkan_Init needs structures which we don't have here yet.
  // For a test app, we'll just leave it uninitialized or mock it.

  // Memuat tekstur menggunakan Assets
  Assets assetManager;
  TextureData backgroundTexture;
  if (!assetManager.LoadTextureFromFile(
          "assets/images/backgrounds/shun_small.webp", &backgroundTexture)) {
    cerr << "Gagal memuat gambar background!" << endl;
  } else {
    cout << "Gambar background berhasil dimuat!" << endl;
  }

  bool done = false;
  while (!done) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT)
        done = true;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Menggambar background
    if (backgroundTexture.TextureID != 0) {
      ImGui::SetNextWindowPos(ImVec2(0, 0));
      ImGui::SetNextWindowSize(io.DisplaySize);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
      ImGui::Begin("Background", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs);
      ImGui::Image(backgroundTexture.TextureID, io.DisplaySize);
      ImGui::End();
      ImGui::PopStyleVar(2);
    }

    // Contoh window sederhana
    ImGui::Begin("Hello!");
    ImGui::Text("Tekstur background telah dimuat.");
    ImGui::End();

    // Render
    ImGui::Render();
    // In Vulkan, we would submit command buffers here.
  }

  // Cleanup
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
  return main(__argc, __argv);
}
#endif
#include "assets.hpp"

// Untuk memuat gambar
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
// Fungsi untuk memuat tekstur dari file gambar
bool Assets::LoadTextureFromFile(const char* filename, TextureData* out_texture)
{
    // Memuat gambar dari file
    int width, height, channels;
    unsigned char* data = stbi_load(filename, &width, &height, &channels, 4);
    if (data == NULL) {
        fprintf(stderr, "Error: Failed to load image: %s\n", filename);
        return false;
    }

    // Buat tekstur OpenGL
    GLuint texture_id;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    
    // Setup parameter tekstur
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Upload data gambar ke GPU
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    // glDeleteTextures(1, &texture_id);
    // Simpan informasi tekstur dan bebaskan memori gambar
    *out_texture = {texture_id, width, height};
    stbi_image_free(data);
    
    return true;
}

// int main(int argc, char* argv[])
// {
//     // Setup SDL
//     if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
//         printf("Error: %s\n", SDL_GetError());
//         return -1;
//     }

//     // Decide GL+GLSL versions
// #if defined(IMGUI_IMPL_OPENGL_ES2)
//     // GL ES 2.0
//     const char* glsl_version = "#version 100";
//     SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
//     SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
//     SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
//     SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
// #elif defined(__APPLE__)
//     // GL 3.2 Core + GLSL 150
//     const char* glsl_version = "#version 150";
//     SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
//     SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
//     SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
//     SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
// #else
//     // GL 3.0 + GLSL 130
//     const char* glsl_version = "#version 130";
//     SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
//     SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
//     SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
//     SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
// #endif

//     // Create window with graphics context
//     SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
//     SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
//     SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
//     SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
//     SDL_Window* window = SDL_CreateWindow("Dear ImGui SDL2+OpenGL3 example", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
//     SDL_GLContext gl_context = SDL_GL_CreateContext(window);
//     SDL_GL_MakeCurrent(window, gl_context);
//     SDL_GL_SetSwapInterval(1); // Enable vsync

//     // Setup Dear ImGui context
//     IMGUI_CHECKVERSION();
//     ImGui::CreateContext();
//     ImGuiIO& io = ImGui::GetIO(); (void)io;
//     io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
//     io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

//     // Setup Dear ImGui style
//     ImGui::StyleColorsDark();

//     // Setup Platform/Renderer backends
//     ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
//     ImGui_ImplOpenGL3_Init(glsl_version);

//     // Load background image
//     TextureData background_texture;
//     Assets asset;
//     bool ret = asset.LoadTextureFromFile("assets/images/backgrounds/shiroko_bluearchive.jpg", &background_texture);
//     if (!ret) {
//         fprintf(stderr, "Failed to load background image!\n");
//         // Continue anyway, just won't have a background
//     }

//     // Main loop
//     bool done = false;
//     while (!done)
//     {
//         // Poll and handle events
//         SDL_Event event;
//         while (SDL_PollEvent(&event))
//         {
//             ImGui_ImplSDL2_ProcessEvent(&event);
//             if (event.type == SDL_QUIT)
//                 done = true;
//             if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
//                 done = true;
//         }

//         // Start the Dear ImGui frame
//         ImGui_ImplOpenGL3_NewFrame();
//         ImGui_ImplSDL2_NewFrame();
//         ImGui::NewFrame();

//         // Background window
//         if (background_texture.TextureID != 0) {
//             ImGui::SetNextWindowPos(ImVec2(0, 0));
//             ImGui::SetNextWindowSize(io.DisplaySize);
//             ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
//             ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
//             ImGui::Begin("Background", nullptr, 
//                 ImGuiWindowFlags_NoDecoration | 
//                 ImGuiWindowFlags_NoInputs | 
//                 ImGuiWindowFlags_NoNav | 
//                 ImGuiWindowFlags_NoBringToFrontOnFocus | 
//                 ImGuiWindowFlags_NoFocusOnAppearing | 
//                 ImGuiWindowFlags_NoMove);
            
//             // Display the background image - Perbaikan untuk typedef ImTextureID
//             ImGui::Image((ImTextureID)(intptr_t)background_texture.TextureID, io.DisplaySize);
            
//             ImGui::End();
//             ImGui::PopStyleVar(2);
//         }

//         // Demo window
//         ImGui::Begin("Hello, world!");
//         ImGui::Text("This is a window with background image.");
//         ImGui::Button("Click me");
//         ImGui::End();

//         // Rendering
//         ImGui::Render();
//         glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
//         glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
//         glClear(GL_COLOR_BUFFER_BIT);
//         ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
//         SDL_GL_SwapWindow(window);
//     }

//     // Cleanup
//     if (background_texture.TextureID != 0)
//         glDeleteTextures(1, &background_texture.TextureID);

//     ImGui_ImplOpenGL3_Shutdown();
//     ImGui_ImplSDL2_Shutdown();
//     ImGui::DestroyContext();

//     SDL_GL_DeleteContext(gl_context);
//     SDL_DestroyWindow(window);
//     SDL_Quit();

//     return 0;
// }

// int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
// {
//     return main(__argc, __argv); // atau langsung taruh isi main() di sini
// }
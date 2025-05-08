#pragma once
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <SDL_main.h>
#include <SDL.h>
#include <windows.h>
// Untuk OpenGL 3
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif


struct TextureData {
    GLuint TextureID = 0;
    int Width = 0;
    int Height = 0;
};

class Assets {
public:
    // Assets();
    // ~Assets();

    bool LoadTextureFromFile(const char* filename, TextureData* out_texture);

    // Variabel
    // GLuint TextureID = 0;
    // int Width = 0;
    // int Height = 0;
};

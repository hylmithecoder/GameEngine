#pragma once
// #include <GL/glew.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include <glad/glad.h>
#include "imgui_impl_opengl3.h"
#include <iostream>
#include <SDL_main.h>
#include <SDL.h>
#include <windows.h>
#include <string>
#include <cstdlib>
// Untuk OpenGL 3
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif


struct TextureData {
    ImTextureID TextureID = 0;
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

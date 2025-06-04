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
#include <algorithm>
#include <vector>
#include <Debugger.hpp>
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

struct Color {
    float r, g, b;
};

class Assets {
public:
    float r, g, b;
    // Color dominantColor;
    Color GetDominantColor(unsigned char* data, int width, int height, int channels);
    // Assets();
    // ~Assets();

    bool LoadTextureFromFile(const char* filename, TextureData* out_texture);

    // Variabel
    // GLuint TextureID = 0;
    // int Width = 0;
    // int Height = 0;
};

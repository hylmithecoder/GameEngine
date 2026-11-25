#pragma once
#include "../core_engine/Debugger.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdlib>
#include <imgui.h>
#include <iostream>
#include <string>
#include <vector>
using namespace Debug;

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
  Color GetDominantColor(unsigned char *data, int width, int height,
                         int channels);
  // Assets();
  // ~Assets();

  bool LoadTextureFromFile(const char *filename, TextureData *out_texture);

  // Variabel
  // GLuint TextureID = 0;
  // int Width = 0;
  // int Height = 0;
};

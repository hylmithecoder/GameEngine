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
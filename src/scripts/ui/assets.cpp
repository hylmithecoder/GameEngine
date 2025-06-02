#include "assets.hpp"

// Untuk memuat gambar
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
// Fungsi untuk memuat tekstur dari file gambar
bool Assets::LoadTextureFromFile(const char* filename, TextureData* out_texture)
{
    // Flip image vertically during loading
    // stbi_set_flip_vertically_on_load(true);

    // Load image from file
    int width, height, channels;
    unsigned char* data = stbi_load(filename, &width, &height, &channels, 4);
    if (data == NULL) {
        std::cerr << "Failed to load texture: " << filename << std::endl;
        return false;
    }

    // Create OpenGL texture
    GLuint texture_id;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    
    // Setup texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Upload image data to GPU
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

    // Store texture info and free image memory
    *out_texture = {texture_id, width, height};
    stbi_image_free(data);
    
    // Reset flip setting to default
    stbi_set_flip_vertically_on_load(false);
    return true;
}
#include "TextureManager.hpp"
#include <iostream>
#include <stb_image.h>

TextureManager::~TextureManager() {
    ClearTextures();
}

GLuint TextureManager::LoadTexture(const std::string& path) {
    // Check if texture is already loaded
    auto it = textureCache.find(path);
    if (it != textureCache.end()) {
        return it->second;
    }
    
    // Load image from file
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true); // Flip textures to match OpenGL's coordinate system
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 0);
    
    if (!data) {
        std::cerr << "Failed to load texture: " << path << std::endl;
        return 0;
    }
    
    // Create OpenGL texture
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    
    // Set texture wrapping/filtering options
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // Upload data and generate mipmaps
    GLenum format = channels == 4 ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    
    // Free image data
    stbi_image_free(data);
    
    // Store texture in cache
    textureCache[path] = textureID;
    
    std::cout << "Loaded texture: " << path << " (" << width << "x" << height 
              << ", " << channels << " channels)" << std::endl;
    
    return textureID;
}

GLuint TextureManager::GetTexture(const std::string& path) const {
    auto it = textureCache.find(path);
    if (it != textureCache.end()) {
        return it->second;
    }
    return 0;
}

void TextureManager::ClearTextures() {
    for (const auto& [path, textureID] : textureCache) {
        glDeleteTextures(1, &textureID);
    }
    textureCache.clear();
}
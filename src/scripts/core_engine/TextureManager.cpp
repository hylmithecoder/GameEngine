#include "TextureManager.hpp"
#include <iostream>
// Properly implement stb_image
// #define STB_IMAGE_IMPLEMENTATION
#include <sstream>
#include <filesystem>
#include <algorithm>
// #include <assets.hpp>

using namespace std;

TextureManager::~TextureManager() {
    ClearTextures();
}

GLuint TextureManager::LoadTexture(const std::string& path) {
    // Convert path to absolute path and normalize it
    std::string normalizedPath = path;
    std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
    
    // Check if texture is already loaded
    auto it = textureCache.find(normalizedPath);
    if (it != textureCache.end()) {
        cout << "Returning cached texture ID: " << it->second << " for path: " << normalizedPath << endl;
        return it->second;
    }
    
    // Verify file exists before attempting to load
    if (!std::filesystem::exists(normalizedPath)) {
        std::cerr << "ERROR: File does not exist: " << normalizedPath << std::endl;
        // Return a default texture ID or 0
        return 0;
    }
    
    cout << "Loading texture from path: " << normalizedPath << endl;
    
    // Load image from file with error handling
    int width = 0, height = 0, channels = 0;
    stbi_set_flip_vertically_on_load(true); // Flip textures to match OpenGL's coordinate system
    
    unsigned char* data = nullptr;
    try {
        data = stbi_load(normalizedPath.c_str(), &width, &height, &channels, 0);
    }
    catch (const std::exception& e) {
        std::cerr << "Exception loading texture: " << e.what() << std::endl;
        return 0;
    }
    
    if (!data) {
        std::cerr << "Failed to load texture: " << normalizedPath << " - " << stbi_failure_reason() << std::endl;
        return 0;
    }
    
    // Create OpenGL texture with error checking
    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    
    if (textureID == 0) {
        std::cerr << "Failed to generate texture ID" << std::endl;
        stbi_image_free(data);
        return 0;
    }
    
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
    textureCache[normalizedPath] = textureID;
    
    std::cout << "Successfully loaded texture: " << normalizedPath
              << " (" << width << "x" << height
              << ", " << channels << " channels), ID: " << textureID << std::endl;
    
    return textureID;
}

GLuint TextureManager::GetTexture(const std::string& path) const {
    std::string normalizedPath = path;
    std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
    
    auto it = textureCache.find(normalizedPath);
    if (it != textureCache.end()) {
        return it->second;
    }
    
    std::cerr << "Warning: Texture not found in cache: " << normalizedPath << std::endl;
    return 0;
}

void TextureManager::ClearTextures() {
    for (const auto& [path, textureID] : textureCache) {
        if (textureID > 0) {
            glDeleteTextures(1, &textureID);
        }
    }
    textureCache.clear();
}
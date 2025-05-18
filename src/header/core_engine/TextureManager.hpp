#pragma once

#include <GLHeader.hpp>
#include <string>
#include <unordered_map>
#include <stb_image.h>

class TextureManager {
public:
    TextureManager() = default;
    ~TextureManager();
    
    // Load texture from file, returning the texture ID
    GLuint LoadTexture(const std::string& path);
    
    // Get texture ID for already loaded texture
    GLuint GetTexture(const std::string& path) const;
    
    // Clear all loaded textures
    void ClearTextures();
    // Cache of loaded textures (path -> textureID)
    std::unordered_map<std::string, GLuint> textureCache;

private:
};
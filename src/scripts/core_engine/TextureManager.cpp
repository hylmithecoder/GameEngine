// #define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <iostream>
#include <GLES2/gl2.h>
#include "TextureManager.hpp"

std::unordered_map<std::string, GLuint> TextureManager::textureCache;

GLuint TextureManager::LoadTexture(const std::string& path) {
    if (textureCache.find(path) != textureCache.end()) {
        return textureCache[path];
    }

    int width, height, channels;
    stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) {
        std::cerr << "Failed to load texture: " << path << std::endl;
        return 0;
    }

    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(data);

    textureCache[path] = texID;
    return texID;
}

void TextureManager::UnloadAll() {
    for (auto& pair : textureCache) {
        glDeleteTextures(1, &pair.second);
    }
    textureCache.clear();
}

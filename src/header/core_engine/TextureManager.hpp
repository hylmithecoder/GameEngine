#pragma once
#include <string>
#include <unordered_map>
// #include <GL/glew.h>
#include <GL/gl.h>

class TextureManager {
public:
    static GLuint LoadTexture(const std::string& path);
    static void UnloadAll();

private:
    static std::unordered_map<std::string, GLuint> textureCache;
};

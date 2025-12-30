#pragma once
#include <string>
#include <vector>

// Simple Scene object structure
struct GameObject {
    std::string name;
    float x, y;
    float width, height;
    std::string spritePath;
    float rotation = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
};

struct Scene {
    std::string sceneName;
    std::vector<GameObject> objects;
};
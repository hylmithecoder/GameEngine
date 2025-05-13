#pragma once
#include <string>
#include <vector>

struct GameObject {
    std::string name;
    float x, y;
    std::string spritePath;
};

struct Scene {
    std::string sceneName;
    std::vector<GameObject> objects;
};

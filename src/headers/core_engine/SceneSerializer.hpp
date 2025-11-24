#pragma once
#include "Scene.hpp"
#include <json.hpp>
#include <fstream>

using json = nlohmann::json;

class SceneSerializer {
public:
    // HandlerProject handlerProject;
    static void SaveScene(const Scene& scene, const std::string& path) ;

    Scene LoadScene(const std::string& path);
};

#include "SceneSerializer.hpp"
#include <fstream>
#include <json.hpp>
#include <iostream>
#include <string>
#include <filesystem>
#include <algorithm>

using namespace std;
using json = nlohmann::json;

void SceneSerializer::SaveScene(const Scene& scene, const std::string& path) {
    json j;
    j["sceneName"] = scene.sceneName;

    for (const auto& obj : scene.objects) {
        j["objects"].push_back({
            {"name", obj.name},
            {"x", obj.x},
            {"y", obj.y},
            {"width", obj.width},
            {"height", obj.height},
            {"spritePath", obj.spritePath},
            {"rotation", obj.rotation},
            {"scaleX", obj.scaleX},
            {"scaleY", obj.scaleY}
        });
    }

    try {
        std::ofstream out(path);
        if (!out.is_open()) {
            std::cerr << "Error: Could not open file for writing: " << path << std::endl;
            return;
        }
        out << j.dump(4);
        std::cout << "Scene saved successfully to: " << path << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error saving scene: " << e.what() << std::endl;
    }
}

Scene SceneSerializer::LoadScene(const std::string& path) {
    Scene scene;
    
    try {
        // Check if file exists
        if (!std::filesystem::exists(path)) {
            std::cerr << "Error: Scene file does not exist: " << path << std::endl;
            return scene;
        }
        
        std::ifstream in(path);
        if (!in.is_open()) {
            std::cerr << "Error: Could not open scene file: " << path << std::endl;
            return scene;
        }
        
        json j;
        in >> j;
        
        scene.sceneName = j["sceneName"].get<std::string>();
        
        std::cout << "Loading scene: " << scene.sceneName << std::endl;
        
        for (const auto& jObj : j["objects"]) {
            GameObject obj;
            obj.name = jObj["name"];
            obj.x = jObj["x"];
            obj.y = jObj["y"];
            obj.width = jObj["width"];
            obj.height = jObj["height"];
            
            // Handle the sprite path - ensure it's properly formatted
            if (jObj.contains("spritePath") && !jObj["spritePath"].is_null()) {
                obj.spritePath = jObj["spritePath"];
                // Normalize path separators
                std::replace(obj.spritePath.begin(), obj.spritePath.end(), '\\', '/');
                
                // Check if the texture file exists
                if (!std::filesystem::exists(obj.spritePath)) {
                    std::cerr << "Warning: Sprite file does not exist: " << obj.spritePath << std::endl;
                }
            } else {
                obj.spritePath = ""; // Set default if missing
            }
            
            obj.rotation = jObj.value("rotation", 0.0f);
            obj.scaleX = jObj.value("scaleX", 1.0f);
            obj.scaleY = jObj.value("scaleY", 1.0f);
            
            scene.objects.push_back(obj);
            
            std::cout << "Loaded object: " << obj.name 
                      << " (Position: " << obj.x << ", " << obj.y 
                      << ", Size: " << obj.width << "x" << obj.height 
                      << ", Sprite: " << obj.spritePath << ")" << std::endl;
        }
        
        std::cout << "Scene loaded successfully with " << scene.objects.size() << " objects" << std::endl;
    } 
    catch (const json::exception& e) {
        std::cerr << "JSON parsing error: " << e.what() << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Error loading scene: " << e.what() << std::endl;
    }
    
    return scene;
}
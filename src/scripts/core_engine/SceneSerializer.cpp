#include "SceneSerializer.hpp"
#include <fstream>
#include <json.hpp>
#include <iostream>
#include <string>
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
            {"spritePath", obj.spritePath}
        });
    }

    std::ofstream out(path);
    out << j.dump(4);
}

Scene SceneSerializer::LoadScene(const std::string& path) {
    std::ifstream in(path);
    json j;
    in >> j;

    Scene scene;
    scene.sceneName = j["sceneName"].get<std::string>();

    for (const auto& jObj : j["objects"]) {
        GameObject obj;
        obj.name = jObj["name"];
        obj.x = jObj["x"];
        obj.y = jObj["y"];
        obj.spritePath = jObj["spritePath"];
        scene.objects.push_back(obj);
        string info = "Name: " + obj.name + " Position x: " + std::to_string(obj.x) + " Position y: " + std::to_string(obj.y) + " Sprite Path: " + obj.spritePath;
        cout << obj.name << endl;
        cout << obj.x << endl;
        cout << obj.y << endl;
        cout << obj.spritePath << endl;        
    }
    cout << "Load Scene" << endl;
    cout << scene.sceneName << endl;
    cout << scene.objects.size() << endl;
    return scene;
}

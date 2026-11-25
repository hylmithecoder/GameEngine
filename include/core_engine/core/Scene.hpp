#pragma once
#include <vector>
#include <memory>
#include "GameObject.hpp"

class Scene {
public:
    void AddObject(std::shared_ptr<GameObject> obj) {
        objects.push_back(obj);
    }

    void Awake() {
        for (auto& obj : objects) obj->Awake();
    }

    void Start() {
        for (auto& obj : objects) obj->Start();
    }

    void Update(float dt) {
        for (auto& obj : objects) obj->Update(dt);
    }

private:
    std::vector<std::shared_ptr<GameObject>> objects;
};
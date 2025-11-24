#pragma once
#include <vector>
#include <memory>
#include "Component.hpp"

class GameObject {
public:
    void AddComponent(std::shared_ptr<Component> comp) {
        components.push_back(comp);
    }

    void Awake() {
        for (auto& comp : components) comp->Awake();
    }

    void Start() {
        for (auto& comp : components) comp->Start();
    }

    void Update(float dt) {
        for (auto& comp : components) comp->Update(dt);
    }

    void OnEnable() {
        for (auto& comp : components) comp->OnEnable();
    }

    void OnDisable() {
        for (auto& comp : components) comp->OnDisable();
    }

private:
    std::vector<std::shared_ptr<Component>> components;
};
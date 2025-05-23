#pragma once
#include "Scene.hpp"

class LifecycleManager {
public:
    static void SetActiveScene(std::shared_ptr<Scene> scene) {
        activeScene = scene;
    }

    static void Awake() {
        if (activeScene) activeScene->Awake();
    }

    static void Start() {
        if (activeScene) activeScene->Start();
    }

    static void Update(float dt) {
        if (activeScene) activeScene->Update(dt);
    }

private:
    static std::shared_ptr<Scene> activeScene;
};
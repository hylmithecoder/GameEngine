#include "GameEngine.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

namespace GameEngine {
    
    // Static members
    std::unique_ptr<Engine> Engine::instance = nullptr;
    
    // Engine Implementation
    Engine::Engine() : currentState(EngineState::Uninitialized) {}
    
    Engine::~Engine() {
        if (currentState != EngineState::Uninitialized) {
            Shutdown();
        }
    }
    
    Engine* Engine::GetInstance() {
        if (!instance) {
            instance = std::make_unique<Engine>();
        }
        return instance.get();
    }
    
    void Engine::DestroyInstance() {
        if (instance) {
            instance->Shutdown();
            instance.reset();
        }
    }
    
    bool Engine::Initialize(const std::string& windowTitle, int width, int height) {
        if (currentState != EngineState::Uninitialized) {
            LogWarning("Engine already initialized");
            return false;
        }
        LogInfo("Thanks for using Ilmeee Engine!");
        LogInfo("Window: "+windowTitle+" ("+std::to_string(width)+"x"+std::to_string(height)+")");
        LogInfo("Initializing Game Engine...");
        
        // Initialize subsystems here
        if (!InternalInitialize()) {
            LogError("Failed to initialize engine subsystems");
            return false;
        }
        
        // Call user initialization callback
        if (initCallback && !initCallback()) {
            LogError("User initialization callback failed");
            return false;
        }
        
        currentState = EngineState::Initialized;
        LogInfo("Game Engine initialized successfully");
        return true;
    }
    
    void Engine::Run() {
        if (currentState != EngineState::Initialized && currentState != EngineState::Paused) {
            LogError("Engine must be initialized before running");
            return;
        }
        
        currentState = EngineState::Running;
        LogInfo("Starting game loop...");
        
        auto lastTime = std::chrono::high_resolution_clock::now();
        const float targetFPS = 60.0f;
        const float targetFrameTime = 1.0f / targetFPS;
        
        while (currentState == EngineState::Running) {
            auto currentTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;
            
            // Cap delta time to prevent spiral of death
            deltaTime = std::min(deltaTime, 0.1f);
            
            // Update
            InternalUpdate(deltaTime);
            
            // Render
            InternalRender();
            
            // Frame rate limiting
            float frameTime = std::chrono::duration<float>(
                std::chrono::high_resolution_clock::now() - currentTime).count();
            
            if (frameTime < targetFrameTime) {
                float sleepTime = targetFrameTime - frameTime;
                std::this_thread::sleep_for(
                    std::chrono::duration<float>(sleepTime));
            }
        }
        
        LogInfo("Game loop ended");
    }
    
    void Engine::Stop() {
        if (currentState == EngineState::Running) {
            currentState = EngineState::Stopped;
            LogInfo("Engine stopped");
        }
    }
    
    void Engine::Pause() {
        if (currentState == EngineState::Running) {
            currentState = EngineState::Paused;
            LogInfo("Engine paused");
        }
    }
    
    void Engine::Resume() {
        if (currentState == EngineState::Paused) {
            currentState = EngineState::Running;
            LogInfo("Engine resumed");
        }
    }
    
    void Engine::Shutdown() {
        if (currentState == EngineState::Uninitialized) {
            return;
        }
        
        LogInfo("Shutting down Game Engine...");
        
        // Call user shutdown callback
        if (shutdownCallback) {
            shutdownCallback();
        }
        
        // Shutdown active scene
        if (activeScene) {
            activeScene->Shutdown();
            activeScene.reset();
        }
        
        // Internal cleanup
        InternalShutdown();
        
        currentState = EngineState::Uninitialized;
        LogInfo("Game Engine shut down");
    }
    
    bool Engine::InternalInitialize() {
        // Initialize rendering system, input system, audio system, etc.
        // This is where you'd initialize DirectX, OpenGL, SDL, etc.
        
        LogInfo("Initializing rendering system...");
        LogInfo("Initializing input system...");
        LogInfo("Initializing audio system...");
        
        return true;
    }
    
    void Engine::InternalUpdate(float deltaTime) {
        // Update active scene
        if (activeScene) {
            activeScene->Update(deltaTime);
        }
        
        // Call user update callback
        if (updateCallback) {
            updateCallback(deltaTime);
        }
    }
    
    void Engine::InternalRender() {
        // Clear screen, set up matrices, etc.
        
        // Render active scene
        if (activeScene) {
            activeScene->Render();
        }
        
        // Call user render callback
        if (renderCallback) {
            renderCallback();
        }
        
        // Present/swap buffers
    }
    
    void Engine::InternalShutdown() {
        LogInfo("Cleaning up rendering system...");
        LogInfo("Cleaning up input system...");
        LogInfo("Cleaning up audio system...");
    }
    
    void Engine::SetInitCallback(std::function<bool()> callback) {
        initCallback = callback;
    }
    
    void Engine::SetUpdateCallback(std::function<void(float)> callback) {
        updateCallback = callback;
    }
    
    void Engine::SetRenderCallback(std::function<void()> callback) {
        renderCallback = callback;
    }
    
    void Engine::SetShutdownCallback(std::function<void()> callback) {
        shutdownCallback = callback;
    }
    
    void Engine::SetActiveScene(std::unique_ptr<Scene> scene) {
        if (activeScene) {
            activeScene->Shutdown();
        }
        activeScene = std::move(scene);
        if (activeScene) {
            activeScene->Initialize();
        }
    }
    
    Scene* Engine::GetActiveScene() const {
        return activeScene.get();
    }
    
    float Engine::GetDeltaTime() const {
        // Return last calculated delta time
        return 0.016f; // 60 FPS default
    }
    
    double Engine::GetTime() const {
        static auto startTime = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double>(currentTime - startTime).count();
    }
    
    // Scene Implementation
    Scene::Scene(const std::string& sceneName) : name(sceneName) {}
    
    Scene::~Scene() {
        gameObjects.clear();
    }
    
    void Scene::Update(float deltaTime) {
        for (auto& obj : gameObjects) {
            if (obj && obj->IsActive()) {
                obj->Update(deltaTime);
            }
        }
    }
    
    void Scene::Render() {
        for (auto& obj : gameObjects) {
            if (obj && obj->IsActive()) {
                obj->Render();
            }
        }
    }
    
    GameObject* Scene::CreateGameObject(const std::string& name) {
        auto obj = std::make_unique<GameObject>(name);
        GameObject* ptr = obj.get();
        gameObjects.push_back(std::move(obj));
        ptr->Start();
        return ptr;
    }
    
    void Scene::DestroyGameObject(GameObject* obj) {
        gameObjects.erase(
            std::remove_if(gameObjects.begin(), gameObjects.end(),
                [obj](const std::unique_ptr<GameObject>& go) {
                    return go.get() == obj;
                }),
            gameObjects.end()
        );
    }
    
    GameObject* Scene::FindGameObject(const std::string& name) {
        for (auto& obj : gameObjects) {
            if (obj && obj->GetName() == name) {
                return obj.get();
            }
        }
        return nullptr;
    }
    
    // GameObject Implementation
    GameObject::GameObject(const std::string& objName) 
        : name(objName), active(true) {}
    
    GameObject::~GameObject() {
        components.clear();
    }
    
    void GameObject::Update(float deltaTime) {
        for (auto& component : components) {
            if (component && component->IsEnabled()) {
                component->Update(deltaTime);
            }
        }
    }
    
    // Component Implementation
    Component::Component() : owner(nullptr), enabled(true) {}
    
    Component::~Component() {}
    
    // Utility Functions
    void LogInfo(const std::string& message) {
        if (message.empty()) return;
        if (message.find("ERROR") != std::string::npos) {
            CoreDebugger::LogError("Log message contains 'ERROR': " + message);
            return;
        }
        CoreDebugger::LogInfo(message);
    }
    
    void LogWarning(const std::string& message) {
        CoreDebugger::LogWarning(message);
    }
    
    void LogError(const std::string& message) {
        CoreDebugger::LogError(message);
    }

    void LogSuccess(const std::string& message) {
        CoreDebugger::LogSuccess(message);
    }
    
    // C-style API Implementation
    extern "C" {
        bool EngineInit(const char* title, int width, int height) {
            return Engine::GetInstance()->Initialize(std::string(title), width, height);
        }
        
        void EngineRun() {
            Engine::GetInstance()->Run();
        }
        
        void EngineStop() {
            Engine::GetInstance()->Stop();
        }
        
        void EngineShutdown() {
            Engine::GetInstance()->Shutdown();
        }
        
        int EngineGetState() {
            return static_cast<int>(Engine::GetInstance()->GetState());
        }
    }
}

// DLL Entry Point
#ifdef _WIN32
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        GameEngine::LogInfo("GameEngine DLL loaded");
        break;
    case DLL_PROCESS_DETACH:
        GameEngine::Engine::DestroyInstance();
        GameEngine::LogInfo("GameEngine DLL unloaded");
        break;
    }
    return TRUE;
}
#endif
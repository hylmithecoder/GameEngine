#pragma once

#ifdef GAMEENGINE_EXPORTS
#define GAMEENGINE_API __declspec(dllexport)
#else
#define GAMEENGINE_API __declspec(dllimport)
#endif

#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace GameEngine {
    
    // Forward declarations
    class Scene;
    class GameObject;
    class Component;
    
    // Engine States
    enum class EngineState {
        Uninitialized,
        Initialized,
        Running,
        Paused,
        Stopped
    };
    
    // Core Engine Class
    class GAMEENGINE_API Engine {
    private:
        static std::unique_ptr<Engine> instance;
        EngineState currentState;
        std::unique_ptr<Scene> activeScene;
        
        // Lifecycle callbacks
        std::function<bool()> initCallback;
        std::function<void(float)> updateCallback;
        std::function<void()> renderCallback;
        std::function<void()> shutdownCallback;
        
        // Internal methods
        bool InternalInitialize();
        void InternalUpdate(float deltaTime);
        void InternalRender();
        void InternalShutdown();
        
    public:
        Engine();
        ~Engine();
        
        // Singleton access
        static Engine* GetInstance();
        static void DestroyInstance();
        
        // Lifecycle management
        bool Initialize(const std::string& windowTitle = "Game Engine", 
                       int width = 1280, int height = 720);
        void Run();
        void Stop();
        void Pause();
        void Resume();
        void Shutdown();
        
        // Callback setters
        void SetInitCallback(std::function<bool()> callback);
        void SetUpdateCallback(std::function<void(float)> callback);
        void SetRenderCallback(std::function<void()> callback);
        void SetShutdownCallback(std::function<void()> callback);
        
        // State management
        EngineState GetState() const { return currentState; }
        bool IsRunning() const { return currentState == EngineState::Running; }
        
        // Scene management
        void SetActiveScene(std::unique_ptr<Scene> scene);
        Scene* GetActiveScene() const;
        
        // Utility
        float GetDeltaTime() const;
        double GetTime() const;
    };
    
    // Scene Class
    class GAMEENGINE_API Scene {
    private:
        std::vector<std::unique_ptr<GameObject>> gameObjects;
        std::string name;
        
    public:
        Scene(const std::string& sceneName);
        virtual ~Scene();
        
        // Lifecycle
        virtual void Initialize() {}
        virtual void Update(float deltaTime);
        virtual void Render();
        virtual void Shutdown() {}
        
        // GameObject management
        GameObject* CreateGameObject(const std::string& name = "GameObject");
        void DestroyGameObject(GameObject* obj);
        GameObject* FindGameObject(const std::string& name);
        
        const std::string& GetName() const { return name; }
    };
    
    // GameObject Class
    class GAMEENGINE_API GameObject {
    private:
        std::string name;
        bool active;
        std::vector<std::unique_ptr<Component>> components;
        
    public:
        GameObject(const std::string& objName);
        virtual ~GameObject();
        
        // Lifecycle
        virtual void Start() {}
        virtual void Update(float deltaTime);
        virtual void Render() {}
        virtual void Destroy() {}
        
        // Component management
        template<typename T>
        T* AddComponent();
        
        template<typename T>
        T* GetComponent();
        
        template<typename T>
        void RemoveComponent();
        
        // Properties
        const std::string& GetName() const { return name; }
        void SetActive(bool isActive) { active = isActive; }
        bool IsActive() const { return active; }
    };
    
    // Component Base Class
    class GAMEENGINE_API Component {
    protected:
        GameObject* owner;
        bool enabled;
        
    public:
        Component();
        virtual ~Component();
        
        // Lifecycle
        virtual void Start() {}
        virtual void Update(float deltaTime) {}
        virtual void Render() {}
        virtual void OnDestroy() {}
        
        // Properties
        GameObject* GetOwner() const { return owner; }
        void SetEnabled(bool isEnabled) { enabled = isEnabled; }
        bool IsEnabled() const { return enabled; }
        
        friend class GameObject;
    };
    
    // Utility Functions
    GAMEENGINE_API void LogInfo(const std::string& message);
    GAMEENGINE_API void LogWarning(const std::string& message);
    GAMEENGINE_API void LogError(const std::string& message);
    
    // C-style API for easier integration
    extern "C" {
        GAMEENGINE_API bool EngineInit(const char* title, int width, int height);
        GAMEENGINE_API void EngineRun();
        GAMEENGINE_API void EngineStop();
        GAMEENGINE_API void EngineShutdown();
        GAMEENGINE_API int EngineGetState();
    }
}

// Template implementations
namespace GameEngine {
    template<typename T>
    T* GameObject::AddComponent() {
        static_assert(std::is_base_of<Component, T>::value, "T must inherit from Component");
        auto component = std::make_unique<T>();
        component->owner = this;
        T* ptr = component.get();
        components.push_back(std::move(component));
        ptr->Start();
        return ptr;
    }
    
    template<typename T>
    T* GameObject::GetComponent() {
        static_assert(std::is_base_of<Component, T>::value, "T must inherit from Component");
        for (auto& component : components) {
            T* casted = dynamic_cast<T*>(component.get());
            if (casted) {
                return casted;
            }
        }
        return nullptr;
    }
    
    template<typename T>
    void GameObject::RemoveComponent() {
        static_assert(std::is_base_of<Component, T>::value, "T must inherit from Component");
        components.erase(
            std::remove_cv(components.begin(), components.end(),
                [this](const std::unique_ptr<Component>& component) {
                    return dynamic_cast<T*>(component.get()) != nullptr;
                }),
            components.end()
        );
    }
}
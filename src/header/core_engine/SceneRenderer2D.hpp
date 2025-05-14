#pragma once
#include <string>
#include "Scene.hpp"
#include "TextureManager.hpp"
#include <imgui_impl_sdl2.h>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

class SceneRenderer2D {
public:
    SceneRenderer2D(int width, int height);
    ~SceneRenderer2D();

    void SetViewportSize(int width, int height);
    void RenderSceneToTexture(const Scene& scene);
    void RenderScene(); // Test function
    GLuint GetViewportTextureID() const;
    enum class EditMode {
        SELECT,
        MOVE,
        ROTATE,
        SCALE
    };

    // Method untuk interaksi
    void SetEditMode(EditMode mode);
    void SetGridVisible(bool visible);
    void SetGridSize(float size);
    void SetSnapToGrid(bool snap);
    void ResetCamera();
    void SetCameraZoom(float zoom);
    void DrawSelectionGizmo(const GameObject& obj);

    // Method konversi koordinat
    glm::vec3 ViewportToWorldPosition(float viewX, float viewY, float viewZ) const;
    glm::vec3 WorldToViewportPosition(float worldX, float worldY, float worldZ) const;
    glm::vec3 cameraPosition;
    Scene currentScene;
    
    // Method handling interaksi
    void HandleClick(float worldX, float worldY);
    void HandleDrag(float deltaX, float deltaY);
    void HandleZoom(float delta);
    void MoveSelected(float deltaX, float deltaY);
    void DrawGrid(const glm::mat4& projection, const glm::mat4& view);
    void DeleteSelected();
    bool HasSelectedObject() const;

    // Method untuk mendapatkan dimensi
    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    GLuint gridShaderProgram;
    GLuint gizmoShaderProgram;

private:
    int width, height;
    
    // Framebuffer objects
    GLuint framebuffer = 0;
    GLuint textureID = 0;      // Color attachment
    GLuint rbo = 0;            // Render buffer object for depth/stencil
    
    // Shader program
    GLuint shaderProgram = 0;
    
    // Quad rendering
    GLuint quadVAO = 0, quadVBO = 0, quadEBO = 0;
    
    // Texture manager
    TextureManager textureManager;
    
    // Initialize components
    void Init();
    void InitQuad();
    void CreateFramebuffer();
    void DestroyFramebuffer();
    
    // Helper functions
    void DrawSprite(GLuint textureID, float x, float y, float width = 64.0f, float height = 64.0f, 
                   float rotation = 0.0f, float scaleX = 1.0f, float scaleY = 1.0f);
    GLuint LoadShaderFromFile(const std::string& path, GLenum type);
    GLuint CreateShaderProgram(const std::string& vertPath, const std::string& fragPath);
    std::string LoadFileAsString(const std::string& path);
    GLuint CreateWhiteTexture();

    // Camera properties
    // Removed duplicate declaration of cameraPosition
    float cameraZoom = 1.0f;
    
    // Grid properties
    bool gridVisible = false;
    float gridSize = 32.0f;
    bool snapToGrid = true;
    
    // Edit properties
    EditMode currentMode = EditMode::SELECT;
    GameObject* selectedObject = nullptr;
    int selectedObjectIndex = -1;
};
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
    void LastGridShaderProgram();
    void SetEditMode(EditMode mode);
    void SetGridVisible(bool visible);
    void SetGridSize(float size);
    void SetSnapToGrid(bool snap);
    void ResetCamera();
    void SetCameraZoom(float zoom);
    void DrawSelectionGizmo(const GameObject& obj);

    // Method konversi koordinat
    glm::vec2 ViewportToWorldPosition(float viewX, float viewY) const;
    glm::vec2 WorldToViewportPosition(float worldX, float worldY) const;
    glm::vec2 cameraPosition;
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
    void InitGridBuffers();

    float cameraZoom = 1.0f;

private:
    int width, height;

    // Viewport attributes
    float zoom = 1.0f;
    ImVec2 pan = ImVec2(0.0f, 0.0f);
    ImVec2 lastMousePos = ImVec2(0.0f, 0.0f);
    bool isDragging = false;
        
    // Grid settings
    float gridSize = 32.0f;
    ImVec4 gridColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
    ImVec4 bgColor = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    
    // Framebuffer objects
    GLuint framebuffer;
    GLuint textureID;      // Color attachment
    GLuint rbo;            // Render buffer object for depth/stencil
    
    // Shader program
    GLuint vertexShader, fragmentShader;
    GLuint shaderProgram;
    GLuint gridShaderProgram;
    GLuint gizmoShaderProgram;
    GLuint m_GridVAO, m_GridVBO;
    int m_MaxGridLines = 1000;
    
    // Quad rendering
    GLuint quadVAO = 0, quadVBO = 0, quadEBO = 0;
    
    // Texture manager
    TextureManager textureManager;
    
    // Initialize components
    void Init();
    void InitQuad();
    void CreateFramebuffer();
    void DestroyFramebuffer();
    void InitShaders();
    void checkProgramLinking(GLuint program);
    
    bool checkShaderUniforms(GLuint program);
    void validateUniformLocation(GLuint program, const char* uniformName, const char* uniformType);
    // Helper functions
    void DrawSprite(GLuint textureID, float x, float y, float width = 64.0f, float height = 64.0f, 
                   float rotation = 0.0f, float scaleX = 1.0f, float scaleY = 1.0f);
    GLuint LoadShaderFromFile(const std::string& path, GLenum type);
    GLuint CreateShaderProgram(const std::string& vertPath, const std::string& fragPath);
    std::string LoadFileAsString(const std::string& path);
    GLuint CreateWhiteTexture();
    
    // Grid properties
    bool gridVisible = true;
    bool snapToGrid = true;
    
    // Edit properties
    EditMode currentMode = EditMode::SELECT;
    GameObject* selectedObject = nullptr;
    int selectedObjectIndex = -1;
};

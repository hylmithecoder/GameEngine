#pragma once
#include <string>
#include "Scene.hpp"
#include "TextureManager.hpp"
#include <imgui_impl_sdl2.h>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

class SceneRenderer3D {
public:
    SceneRenderer3D(int width, int height);
    ~SceneRenderer3D();

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

    enum class ProjectionType {
        PERSPECTIVE,
        ORTHOGRAPHIC
    };

    // Method untuk interaksi
    void SetEditMode(EditMode mode);
    void SetGridVisible(bool visible);
    void SetGridSize(float size);
    void SetGridPlane(int plane); // 0=XY, 1=XZ, 2=YZ
    void SetSnapToGrid(bool snap);
    void ResetCamera();
    void SetCameraZoom(float zoom);
    void DrawSelectionGizmo(const GameObject& obj);
    void SetProjectionType(ProjectionType type);

    // Camera controls
    void SetCameraPosition(const glm::vec3& position);
    void SetCameraTarget(const glm::vec3& target);
    void RotateCamera(float yaw, float pitch);
    
    // Method konversi koordinat
    glm::vec3 ViewportToWorldPosition(float viewX, float viewY, float viewDepth = 0.0f) const;
    glm::vec3 WorldToViewportPosition(const glm::vec3& worldPos) const;
    
    // Public camera properties (might want to encapsulate these in a Camera class later)
    glm::vec3 cameraPosition = glm::vec3(0.0f, 0.0f, 5.0f);
    glm::vec3 cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
    float cameraYaw = -90.0f; // Camera looks at negative Z by default
    float cameraPitch = 0.0f;
    Scene currentScene;
    GameObject objects;
    
    // Method handling interaksi
    void HandleClick(float viewX, float viewY);
    void HandleDrag(float deltaX, float deltaY, bool isRotating = false);
    void HandleZoom(float delta);
    void MoveSelected(float deltaX, float deltaY, float deltaZ = 0.0f);
    void DrawGrid(const glm::mat4& projection, const glm::mat4& view);
    void DeleteSelected();
    bool HasSelectedObject() const;

    // Method untuk mendapatkan dimensi
    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    
    // 3D specific functions
    void InitializeAxes();
    void DrawAxes(const glm::mat4& projection, const glm::mat4& view);
    
    // Shader programs
    GLuint gridShaderProgram;
    GLuint gizmoShaderProgram;
    GLuint modelShaderProgram; // For 3D models
    GLuint axisShaderProgram;  // For coordinate axes
    
    void InitGridBuffers();
    void InitAxesBuffers();

private:
    int width, height;
    
    // Framebuffer objects
    GLuint framebuffer = 0;
    GLuint textureID = 0;      // Color attachment
    GLuint depthTextureID = 0; // Depth attachment as texture
    GLuint rbo = 0;            // Render buffer object for depth/stencil
    
    // Shader programs (moved some to public for easier access)
    GLuint shaderProgram = 0;
    
    // Buffers
    GLuint m_GridVAO = 0, m_GridVBO = 0;
    GLuint m_AxesVAO = 0, m_AxesVBO = 0;
    int m_MaxGridLines = 200;
    
    // Quad rendering (for 2D sprites and UI elements)
    GLuint quadVAO = 0, quadVBO = 0, quadEBO = 0;
    
    // Texture manager
    TextureManager textureManager;
    
    // Initialize components
    void Init();
    void InitQuad();
    void CreateFramebuffer();
    void DestroyFramebuffer();
    void Cleanup();
    
    // Helper functions
    void DrawSprite(GLuint textureID, const glm::vec3& position, const glm::vec2& size = glm::vec2(64.0f),
                   float rotation = 0.0f, const glm::vec2& scale = glm::vec2(1.0f));
    void DrawModel(GLuint modelVAO, int vertexCount, const glm::vec3& position,
                  const glm::vec3& rotation = glm::vec3(0.0f), const glm::vec3& scale = glm::vec3(1.0f));
    GLuint LoadShaderFromFile(const std::string& path, GLenum type);
    GLuint CreateShaderProgram(const std::string& vertPath, const std::string& fragPath);
    std::string LoadFileAsString(const std::string& path);
    GLuint CreateWhiteTexture();

    // Calculate view and projection matrices
    glm::mat4 CalculateViewMatrix() const;
    glm::mat4 CalculateProjectionMatrix() const;
    
    // Camera properties
    float cameraZoom = 1.0f;
    float fieldOfView = 45.0f; // For perspective projection
    float nearPlane = 0.1f;    // Near clipping plane 
    float farPlane = 1000.0f;  // Far clipping plane
    ProjectionType projType = ProjectionType::PERSPECTIVE;
    
    // Grid properties
    bool gridVisible = true;
    float gridSize = 1.0f;     // Changed default for 3D (1 unit = 1 meter is common)
    bool snapToGrid = true;
    int gridPlane = 0;         // 0=XY, 1=XZ, 2=YZ
    
    // Edit properties
    EditMode currentMode = EditMode::SELECT;
    GameObject* selectedObject = nullptr;
    int selectedObjectIndex = -1;
    
    // Ray casting for picking in 3D
    bool CastRay(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, 
                GameObject** hitObject, float* hitDistance);
};
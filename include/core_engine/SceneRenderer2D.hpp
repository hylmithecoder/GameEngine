#pragma once
#include "Scene.hpp"
#include "TextureManager.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

class SceneRenderer2D {
public:
  SceneRenderer2D(int width, int height);
  ~SceneRenderer2D();

  void SetViewportSize(int width, int height);
  void RenderSceneToTexture(const Scene &scene);
  void RenderScene(); // Test function

  // Returns the descriptor set for ImGui to display the viewport
  VkDescriptorSet GetViewportDescriptorSet() const;

  enum class EditMode { SELECT, MOVE, ROTATE, SCALE };

  // Method untuk interaksi
  void SetEditMode(EditMode mode);
  void SetGridVisible(bool visible);
  void SetGridSize(float size);
  void SetSnapToGrid(bool snap);
  void ResetCamera();
  void SetCameraZoom(float zoom);
  void DrawSelectionGizmo(const GameObject &obj);

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
  void DrawGrid(VkCommandBuffer cmd, const glm::mat4 &projection,
                const glm::mat4 &view);
  void DeleteSelected();
  bool HasSelectedObject() const;
  void SetGridColor(float r, float g, float b, float a);
  void SetBackgroundColor(float r, float g, float b, float a);

  // Method untuk mendapatkan dimensi
  int GetWidth() const { return width; }
  int GetHeight() const { return height; }

  float cameraZoom = 1.0f;
  float GetZoom() const { return zoom; }
  float zoom = 1.0f;

  // Vulkan external initialization
  void SetVulkanContext(VkDevice device, VkPhysicalDevice physicalDevice,
                        VkQueue graphicsQueue, VkCommandPool commandPool,
                        VkDescriptorPool descriptorPool);

private:
  int width, height;

  // Viewport attributes
  ImVec2 pan = ImVec2(0.0f, 0.0f);
  ImVec2 lastMousePos = ImVec2(0.0f, 0.0f);
  bool isDragging = false;

  // Grid settings
  float gridSize = 32.0f;
  ImVec4 gridColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
  ImVec4 bgColor = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);

  // Vulkan Context
  VkDevice device = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkQueue graphicsQueue = VK_NULL_HANDLE;
  VkCommandPool commandPool = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

  // Offscreen resources
  struct Offscreen {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
  } offscreen;

  // Pipelines
  VkPipeline spritePipeline = VK_NULL_HANDLE;
  VkPipelineLayout spritePipelineLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout spriteDescriptorSetLayout = VK_NULL_HANDLE;
  VkPipeline gridPipeline = VK_NULL_HANDLE;
  VkPipelineLayout gridPipelineLayout = VK_NULL_HANDLE;
  VkPipeline gizmoPipeline = VK_NULL_HANDLE;
  VkPipelineLayout gizmoPipelineLayout = VK_NULL_HANDLE;
  // Self-contained "hello triangle" — no descriptor sets, no vertex
  // buffer, no UBO; vertex positions baked in the shader via
  // gl_VertexIndex. Serves as a render-pipeline smoke test inside the
  // Scene panel until proper scene content is wired up.
  VkPipeline testTrianglePipeline = VK_NULL_HANDLE;
  VkPipelineLayout testTrianglePipelineLayout = VK_NULL_HANDLE;
  bool showTestTriangle = false;

  // 3D mesh pipeline (pos + normal vertex, MVP+model push constants,
  // depth-tested). Used to render OBJ meshes loaded into meshes3d.
  VkPipeline meshPipeline = VK_NULL_HANDLE;
  VkPipelineLayout meshPipelineLayout = VK_NULL_HANDLE;

  // Vertex data
  struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
  } quadBuffer, gridBuffer;

  struct MeshVertex {
    glm::vec3 pos;
    glm::vec3 normal;
  };

  struct Mesh3D {
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    uint32_t indexCount = 0;
    uint32_t vertexCount = 0;
    glm::vec3 aabbMin{0.0f};
    glm::vec3 aabbMax{0.0f};
    // Auto-fit baseline applied first (center on origin + uniform scale
    // to ~1.5 units) so the user-facing transform starts at identity.
    glm::mat4 autoFit = glm::mat4(1.0f);
    glm::vec3 userPosition = glm::vec3(0.0f);
    glm::vec3 userRotation = glm::vec3(0.0f); // euler degrees
    glm::vec3 userScale = glm::vec3(1.0f);
    std::string path;
    std::string displayName;

    glm::mat4 ComputeModel() const;
  };
  std::vector<Mesh3D> meshes3d;

  // Static 3D grid drawn on the XZ plane (Y=0) as a spatial reference.
  struct GridResources {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    uint32_t vertexCount = 0;
  } grid3d;
  bool grid3dVisible = true;

  // Texture manager
  TextureManager textureManager;

  // Initialize components
  void Init();
  void InitVulkanResources();
  void CreateOffscreenResources();
  void DestroyOffscreenResources();
  void InitPipelines();

  // Helper functions
  void DrawSprite(VkCommandBuffer cmd, VkDescriptorSet textureDescriptor,
                  const glm::mat4 &mvp);
  VkShaderModule LoadShader(const std::string &path);

  // Test triangle helpers
  void InitTestTrianglePipeline();
  void DrawTestTriangle(VkCommandBuffer cmd);

  // Mesh pipeline helpers
  void InitMeshPipeline();
  void DrawMeshes(VkCommandBuffer cmd, const glm::mat4 &viewProj);
  bool UploadMeshBuffers(Mesh3D &mesh, const std::vector<MeshVertex> &verts,
                         const std::vector<uint32_t> &indices);
  void DestroyMesh(Mesh3D &mesh);

  // Grid helpers
  void InitGridResources();
  void DrawGrid3D(VkCommandBuffer cmd, const glm::mat4 &viewProj);

public:
  // Load an OBJ file into the 3D mesh list. Returns true on success.
  bool LoadObjMesh(const std::string &path);

  // Procedural primitives — built on the same Mesh3D pipeline so they
  // pick up the same lighting, picking, transform UI, etc. Each call
  // appends a new mesh and returns true on success.
  bool LoadCube(const std::string &name = "Cube", float size = 1.0f);
  bool LoadSphere(const std::string &name = "Sphere", float radius = 0.5f,
                  int segments = 24, int rings = 16);
  bool LoadPlane(const std::string &name = "Plane", float size = 2.0f);

  // Clear all loaded meshes (used when switching projects).
  void ClearMeshes3D();

  // Last value of `name` parameter for the i-th mesh (defaults to the
  // OBJ filename stem or "Cube N" for primitives). Used by the
  // hierarchy / inspector to label entries.
  const std::string &GetMesh3DName(size_t i) const;

  size_t GetMesh3DCount() const { return meshes3d.size(); }
  glm::vec3 GetMesh3DPosition(size_t i) const;
  glm::vec3 GetMesh3DRotation(size_t i) const;
  glm::vec3 GetMesh3DScale(size_t i) const;
  const std::string &GetMesh3DPath(size_t i) const;
  uint32_t GetMesh3DVertexCount(size_t i) const;
  uint32_t GetMesh3DTriangleCount(size_t i) const;
  void SetMesh3DTransform(size_t i, const glm::vec3 &position,
                          const glm::vec3 &rotationEuler,
                          const glm::vec3 &scale);
  // Translate the i-th mesh in the camera's screen plane by (dxPx, dyPx)
  // mouse pixels. Resolves pixel→world using the object's distance from
  // the camera and the viewport height.
  void DragMesh3DScreen(size_t i, float dxPx, float dyPx, int viewportHeight);

  void SetGrid3DVisible(bool v) { grid3dVisible = v; }
  bool IsGrid3DVisible() const { return grid3dVisible; }

  // FPS-style 3D camera driven by the Scene viewport. The 2D pan/zoom
  // controls are kept for sprite scenes; when meshes3d is non-empty the
  // 3D camera takes over the matrix used for rendering.
  struct Camera3D {
    glm::vec3 position = glm::vec3(0.0f, 1.6f, 3.0f);
    float yaw = -90.0f; // facing -Z
    float pitch = 0.0f;
    float fovDeg = 60.0f;
    float nearPlane = 0.01f;
    float farPlane = 500.0f;
    float moveSpeed = 2.5f;
    float mouseSensitivity = 0.12f;
  } camera3d;

  // One-frame snapshot of input collected by the Scene window. Only the
  // panel that owns mouse/keyboard focus should populate this; the
  // renderer treats it as authoritative.
  struct ViewportInput {
    bool hovered = false;
    bool rmbDown = false;
    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;
    bool wDown = false;
    bool aDown = false;
    bool sDown = false;
    bool dDown = false;
    bool qDown = false;
    bool eDown = false;
    bool shiftDown = false;
    float scroll = 0.0f;
    float deltaTime = 1.0f / 60.0f;
  };
  void UpdateCamera3D(const ViewportInput &in);
  bool HasMesh3D() const { return !meshes3d.empty(); }

  void SetTestTriangleVisible(bool v) { showTestTriangle = v; }
  bool IsTestTriangleVisible() const { return showTestTriangle; }

private:
  // Grid properties
  bool gridVisible = true;
  bool snapToGrid = true;

  // Edit properties
  EditMode currentMode = EditMode::SELECT;
  GameObject *selectedObject = nullptr;
  int selectedObjectIndex = -1;

  uint32_t findMemoryType(uint32_t typeFilter,
                          VkMemoryPropertyFlags properties);
};

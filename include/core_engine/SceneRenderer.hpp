#pragma once
#include "PMXLoader.hpp"
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

class SceneRenderer {
public:
  SceneRenderer(int width, int height);
  ~SceneRenderer();

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

  // Second offscreen target: renders the scene from the in-scene player
  // camera for the "Camera Preview" window. Fixed 16:9 size; shares the
  // main offscreen render pass.
  Offscreen preview;
  int previewWidth = 480;
  int previewHeight = 270;
  bool previewReady = false;

  // Pipelines
  VkPipeline spritePipeline = VK_NULL_HANDLE;
  VkPipelineLayout spritePipelineLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout spriteDescriptorSetLayout = VK_NULL_HANDLE;
  VkPipeline gridPipeline = VK_NULL_HANDLE;
  VkPipelineLayout gridPipelineLayout = VK_NULL_HANDLE;
  // Editor-only line gizmos (dashed camera frustums + light direction
  // rays). Lines are rebuilt on the CPU each frame into a growable
  // host-visible buffer — counts are tiny (a few cameras/lights).
  VkPipeline gizmoPipeline = VK_NULL_HANDLE;
  VkPipelineLayout gizmoPipelineLayout = VK_NULL_HANDLE;
  VkBuffer gizmoVertexBuffer = VK_NULL_HANDLE;
  VkDeviceMemory gizmoVertexMemory = VK_NULL_HANDLE;
  VkDeviceSize gizmoCapacity = 0;
  uint32_t gizmoVertexCount = 0;
  bool gizmoVisible = true;
  // Self-contained "hello triangle" — no descriptor sets, no vertex
  // buffer, no UBO; vertex positions baked in the shader via
  // gl_VertexIndex. Serves as a render-pipeline smoke test inside the
  // Scene panel until proper scene content is wired up.
  VkPipeline testTrianglePipeline = VK_NULL_HANDLE;
  VkPipelineLayout testTrianglePipelineLayout = VK_NULL_HANDLE;
  bool showTestTriangle = false;

  // 3D mesh pipeline (pos + normal + uv vertex, MVP+model+material push
  // constants, depth-tested, samples one albedo texture per submesh).
  VkPipeline meshPipeline = VK_NULL_HANDLE;
  VkPipelineLayout meshPipelineLayout = VK_NULL_HANDLE;
  // set=0 binding=0 = combined image sampler (fragment). Defined identically
  // to ImGui's texture layout so descriptors handed out by TextureManager
  // (which uses ImGui_ImplVulkan_AddTexture) bind to this pipeline directly.
  VkDescriptorSetLayout meshTextureSetLayout = VK_NULL_HANDLE;
  // 1x1 white fallback bound for submeshes with no texture (Vulkan requires
  // every declared sampler be bound even when the shader ignores it).
  VkImage whiteImage = VK_NULL_HANDLE;
  VkDeviceMemory whiteMemory = VK_NULL_HANDLE;
  VkImageView whiteView = VK_NULL_HANDLE;
  VkDescriptorSet whiteDescriptor = VK_NULL_HANDLE;

  // Vertex data
  struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
  } quadBuffer, gridBuffer;

public:
  struct MeshVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv{0.0f};
  };

  // A contiguous index range that shares one material/texture. For PMX
  // models each PMXMaterial maps to one SubMesh (the "detected surface"
  // the user can click and re-texture); OBJ meshes and primitives get a
  // single SubMesh covering the whole index buffer.
  struct SubMesh {
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
    std::string name;                       // material name (PMX) or ""
    std::string texturePath;                // resolved absolute path, "" = none
    glm::vec3 diffuse{0.84f, 0.80f, 0.74f}; // tint / fallback color
    VkDescriptorSet textureDescriptor =
        VK_NULL_HANDLE; // owned by TextureManager
    bool hasTexture = false;
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
    // PMX bone hierarchy (empty for OBJ meshes / primitives)
    std::vector<pmx::PMXBone> bones;

    // Per-material surface ranges. Always at least one entry after a
    // successful load; DrawMeshes issues one draw per submesh so each can
    // bind its own texture.
    std::vector<SubMesh> submeshes;

    // CPU copy of the geometry kept for click-picking (ray-triangle tests
    // run against these in object space, transformed by ComputeModel()).
    // Indices match the GPU index buffer 1:1 so a hit triangle maps back
    // to the owning submesh range.
    std::vector<glm::vec3> cpuPositions;
    std::vector<uint32_t> cpuIndices;

    // Light specific properties
    bool isLight = false;
    float lightGamma = 1.05f;
    glm::vec3 lightColor{1.0f, 0.96f, 0.88f};
    float lightIntensity = 1.0f;
    int lightType = 0; // 0 = Directional, 1 = Point, 2 = Spotlight
    float lightRange = 10.0f;
    float lightSpotAngle = 30.0f;

    // Camera specific properties (active only when isCamera == true). This
    // is the in-scene "player" camera, distinct from the editor free-cam.
    bool isCamera = false;
    int camProjection = 0;     // 0 = Perspective, 1 = Orthographic
    float camFov = 60.0f;      // perspective vertical FOV (degrees)
    float camOrthoSize = 5.0f; // orthographic half-height (world units)
    float camNear = 0.1f;
    float camFar = 100.0f;

    // Physics specific properties
    bool hasPhysics = false;
    bool useGravity = true;
    bool isKinematic = false;
    float mass = 1.0f;
    float drag = 0.0f;
    float gravityY = -9.81f;
    glm::vec3 velocity = glm::vec3(0.0f);

    // Audio specific properties
    bool hasAudio = false;
    std::string audioPath = "";
    bool isPlaying = false;

    // Optional source location for the call site that spawned this mesh.
    // Populated via SetMesh3DDebugSource() and surfaced by Inspect Mode
    // (--debug overlay) when hovering the object in the viewport.
    std::string debugSrcFile;
    int debugSrcLine = 0;

    glm::mat4 ComputeModel() const;
  };
  std::vector<Mesh3D> meshes3d;

private:

  // Infinite ground grid drawn on the XZ plane (Y=0). Rendered as one
  // fullscreen triangle whose fragment shader raycasts the plane, so it
  // covers exactly the visible ground with constant memory (no per-line
  // vertex buffer). The buffer fields are unused now but kept for the
  // shared cleanup path.
  struct GridResources {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    uint32_t vertexCount = 0;
  } grid3d;
  bool grid3dVisible = true;

  // Decorative sun sphere drawn in the sky (unlit, depth test off). Kept
  // separate from meshes3d so it stays out of the hierarchy / selection.
  struct SunResources {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    uint32_t indexCount = 0;
  } sun;
  bool sunVisible = true;

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
  // Lazily create the 1x1 white fallback texture + its descriptor.
  void InitWhiteTexture();
  // Resolve a texture path to a bindable descriptor, falling back to the
  // white texture on empty path or load failure.
  VkDescriptorSet ResolveTextureDescriptor(const std::string &path);

  // Grid helpers
  void InitGridResources();
  void DrawGrid3D(VkCommandBuffer cmd, const glm::mat4 &view,
                  const glm::mat4 &proj);

  // Sun helpers
  void InitSunResources();
  void DrawSun(VkCommandBuffer cmd, const glm::mat4 &viewProj);

  // Gizmo (dashed lines) helpers
  void InitGizmoPipeline();
  void DrawGizmos(VkCommandBuffer cmd, const glm::mat4 &viewProj);

  // Preview (player-camera) helpers
  void CreatePreviewResources();
  void DestroyPreviewResources();
  // Record the 3D world (sun, grid, meshes, optional gizmos) for the given
  // camera matrices into an already-open render pass.
  void RecordWorld(VkCommandBuffer cmd, const glm::mat4 &view3d,
                   const glm::mat4 &proj3d, bool drawGizmos);
  // Compute the player camera's view/proj for the given aspect ratio.
  // Returns false if there is no camera object in the scene.
  bool ComputePlayerCameraMatrices(float aspect, glm::mat4 &view,
                                   glm::mat4 &proj) const;

public:
  // Load an OBJ file into the 3D mesh list. Returns true on success.
  bool LoadObjMesh(const std::string &path);

  // Load a PMX (MikuMikuDance) model. Parses geometry, materials,
  // and bones. Renders in bind pose using the mesh pipeline.
  bool LoadPMXMesh(const std::string &path);

  // Load an FBX model via ufbx. Merges all meshes into one Mesh3D with
  // per-material submeshes (surfaces) and auto-loads diffuse textures.
  bool LoadFbxMesh(const std::string &path);

  // Procedural primitives — built on the same Mesh3D pipeline so they
  // pick up the same lighting, picking, transform UI, etc. Each call
  // appends a new mesh and returns true on success.
  bool LoadCube(const std::string &name = "Cube", float size = 1.0f);
  bool LoadSphere(const std::string &name = "Sphere", float radius = 0.5f,
                  int segments = 24, int rings = 16);
  bool LoadPlane(const std::string &name = "Plane", float size = 2.0f);
  bool LoadLight(const std::string &name = "Light", int type = 0,
                 const glm::vec3 &color = glm::vec3(1.0f, 0.96f, 0.88f),
                 float intensity = 1.0f, float range = 10.0f,
                 float spotAngle = 30.0f, float gamma = 1.05f);

  // In-scene player camera (separate from the editor free-cam). Appears as
  // a small marker mesh plus a dashed frustum gizmo, and can be previewed.
  bool LoadCamera(const std::string &name = "Camera", int projection = 0,
                  float fov = 60.0f, float orthoSize = 5.0f, float nearP = 0.1f,
                  float farP = 100.0f);

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
  uint32_t GetMesh3DBoneCount(size_t i) const;
  void SetMesh3DTransform(size_t i, const glm::vec3 &position,
                          const glm::vec3 &rotationEuler,
                          const glm::vec3 &scale);

  // --- Inspect Mode source tracking ---
  // Attach the file:line of the call site that spawned mesh i. Called by
  // scene bootstrap / spawn UI so the --debug overlay can report where a
  // hovered scene object was created. Safe to call with a stale index.
  void SetMesh3DDebugSource(size_t i, const char *file, int line);
  const std::string &GetMesh3DDebugSrcFile(size_t i) const;
  int GetMesh3DDebugSrcLine(size_t i) const;

  // --- Surfaces (submeshes) and per-surface texturing ---
  // A "surface" is one material range of the mesh. For PMX these are the
  // model's materials (auto-detected); OBJ/primitives expose a single one.
  uint32_t GetMesh3DSubmeshCount(size_t i) const;
  const std::string &GetMesh3DSubmeshName(size_t i, uint32_t sub) const;
  const std::string &GetMesh3DSubmeshTexture(size_t i, uint32_t sub) const;
  glm::vec3 GetMesh3DSubmeshDiffuse(size_t i, uint32_t sub) const;
  // Load `path` and bind it to surface `sub` of mesh `i`. Empty path or a
  // load failure clears the binding (falls back to the diffuse color).
  bool BindMesh3DSubmeshTexture(size_t i, uint32_t sub,
                                const std::string &path);
  void ClearMesh3DSubmeshTexture(size_t i, uint32_t sub);

  // Click-pick: cast a ray through panel pixel (pxX,pxY) and return the
  // nearest mesh + surface hit. Returns false if the ray misses everything.
  bool PickMesh3DSurface(float pxX, float pxY, int &outMeshIndex,
                         int &outSubmesh) const;
  // Build a world-space ray from a panel pixel using the current 3D camera.
  bool ScreenToRay(float pxX, float pxY, glm::vec3 &outOrigin,
                   glm::vec3 &outDir) const;

  // Light property accessors
  bool IsMesh3DLight(size_t i) const;
  float GetMesh3DLightGamma(size_t i) const;
  glm::vec3 GetMesh3DLightColor(size_t i) const;
  float GetMesh3DLightIntensity(size_t i) const;
  int GetMesh3DLightType(size_t i) const;
  float GetMesh3DLightRange(size_t i) const;
  float GetMesh3DLightSpotAngle(size_t i) const;

  void SetMesh3DLightGamma(size_t i, float gamma);
  void SetMesh3DLightColor(size_t i, const glm::vec3 &color);
  void SetMesh3DLightIntensity(size_t i, float intensity);
  void SetMesh3DLightType(size_t i, int type);
  void SetMesh3DLightRange(size_t i, float range);
  void SetMesh3DLightSpotAngle(size_t i, float angle);

  // Camera property accessors
  bool IsMesh3DCamera(size_t i) const;
  int GetMesh3DCameraProjection(size_t i) const;
  float GetMesh3DCameraFov(size_t i) const;
  float GetMesh3DCameraOrthoSize(size_t i) const;
  float GetMesh3DCameraNear(size_t i) const;
  float GetMesh3DCameraFar(size_t i) const;
  void SetMesh3DCameraProjection(size_t i, int projection);
  void SetMesh3DCameraFov(size_t i, float fov);
  void SetMesh3DCameraOrthoSize(size_t i, float size);
  void SetMesh3DCameraNear(size_t i, float nearP);
  void SetMesh3DCameraFar(size_t i, float farP);

  // Player-camera preview. Render the scene from the first camera object
  // into the preview target; the Scene UI shows it in a small window.
  bool HasPlayerCamera() const;
  void RenderPlayerCameraPreview();
  VkDescriptorSet GetPlayerCameraPreviewDescriptor() const;
  int GetPreviewWidth() const { return previewWidth; }
  int GetPreviewHeight() const { return previewHeight; }
  // Translate the i-th mesh in the camera's screen plane by (dxPx, dyPx)
  // mouse pixels. Resolves pixel→world using the object's distance from
  // the camera and the viewport height.
  void DragMesh3DScreen(size_t i, float dxPx, float dyPx, int viewportHeight);

  void SetGrid3DVisible(bool v) { grid3dVisible = v; }
  bool IsGrid3DVisible() const { return grid3dVisible; }

  void SetSunVisible(bool v) { sunVisible = v; }
  bool IsSunVisible() const { return sunVisible; }

  // Forward direction of the 3D camera (normalized), matching the render
  // path. Useful for placing objects in front of the camera.
  glm::vec3 GetCameraForward() const;

  // Unproject a viewport pixel (top-left origin, panel pixels) onto the
  // y=0 ground plane using the current 3D camera. Returns false when the
  // ray is parallel to the ground or the hit is behind the camera.
  bool ScreenToGround(float pxX, float pxY, glm::vec3 &outWorld) const;

  bool IsSnapToGrid() const { return snapToGrid; }

  // 2D grid helpers — exposed so the Scene panel can render a dynamic
  // ImDrawList overlay that respects pan/zoom without needing a
  // dedicated Vulkan grid pipeline.
  float GetGridSize() const { return gridSize; }
  bool IsGridVisible() const { return gridVisible; }

  // Unity-style directional sun light. Direction points *toward* the
  // light source (i.e. the shader computes max(dot(N, sunDir), 0)).
  struct SunLight {
    glm::vec3 direction = glm::normalize(glm::vec3(0.55f, 0.80f, 0.30f));
    glm::vec3 color = glm::vec3(1.0f, 0.96f, 0.88f);
    float intensity = 0.90f;
  } sunLight;

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

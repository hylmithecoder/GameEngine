#pragma once
#include "Scene.hpp"
#include "TextureManager.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <string>
#include <unordered_map>
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
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
  } offscreen;

  // Pipelines
  VkPipeline spritePipeline = VK_NULL_HANDLE;
  VkPipelineLayout spritePipelineLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout spriteDescriptorSetLayout = VK_NULL_HANDLE;
  VkPipeline gridPipeline = VK_NULL_HANDLE;
  VkPipelineLayout gridPipelineLayout = VK_NULL_HANDLE;
  VkPipeline gizmoPipeline = VK_NULL_HANDLE;
  VkPipelineLayout gizmoPipelineLayout = VK_NULL_HANDLE;

  // Vertex data
  struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
  } quadBuffer, gridBuffer;

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

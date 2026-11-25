#include "core_engine/SceneRenderer.hpp"
#include <algorithm>

bool SceneRenderer::LoadWorldText(const std::string &name,
                                 const ilmeee::WorldTextComponent &text) {
  if (device == VK_NULL_HANDLE) return false;
  Mesh3D mesh;
  mesh.isWorldText = true;
  mesh.worldText = text;
  mesh.path = "<primitive:text>";
  mesh.displayName = name;
  meshes3d.push_back(std::move(mesh));
  UpdateWorldTexts();
  return true; // empty/whitespace text is also a valid object
}

void SceneRenderer::UpdateWorldTexts() {
  for (auto &mesh : meshes3d) {
    if (!mesh.isWorldText || (mesh.builtText == mesh.worldText.text &&
        mesh.builtTextSize == mesh.worldText.size &&
        mesh.builtTextAlignment == mesh.worldText.alignment)) continue;
    WaitOffscreenWork();
    // The editor's main viewport and Game preview may both reference it.
    vkDeviceWaitIdle(device);
    const auto geometry = ilmeee::BuildWorldTextGeometry(mesh.worldText);
    std::vector<MeshVertex> vertices;
    vertices.reserve(geometry.vertices.size());
    for (const auto &v : geometry.vertices)
      vertices.push_back({v.position, {0, 0, 1}, v.uv});
    // Build into temporary buffers: an allocation failure keeps the old mesh
    // and retries on the next frame, rather than destroying valid content.
    Mesh3D uploaded;
    if (!vertices.empty() && !UploadMeshBuffers(uploaded, vertices, geometry.indices)) {
      DestroyMesh(uploaded);
      continue;
    }
    if (!worldTextFontTexture) {
      const auto font = ilmeee::GetWorldTextFontImage();
      worldTextFontTexture = CreateDynamicTexture(font.width, font.height);
      if (worldTextFontTexture) UpdateDynamicTexture(worldTextFontTexture, font.rgba);
    }
    if (!worldTextFontTexture) { DestroyMesh(uploaded); continue; }
    DestroyMesh(mesh);
    mesh.vertexBuffer = uploaded.vertexBuffer;
    mesh.vertexMemory = uploaded.vertexMemory;
    mesh.indexBuffer = uploaded.indexBuffer;
    mesh.indexMemory = uploaded.indexMemory;
    mesh.indexCount = uploaded.indexCount;
    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    mesh.cpuPositions = std::move(uploaded.cpuPositions);
    mesh.cpuIndices = std::move(uploaded.cpuIndices);
    mesh.submeshes.clear(); // rendered with the dedicated font material
    mesh.aabbMin = geometry.min;
    mesh.aabbMax = geometry.max;
    mesh.builtText = mesh.worldText.text;
    mesh.builtTextSize = mesh.worldText.size;
    mesh.builtTextAlignment = mesh.worldText.alignment;
  }
}

#include "../../include/ui/SvgIconManager.hpp"
#include "../../include/core_engine/Debugger.hpp"

#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace {

std::string ToLowerAscii(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

std::string ToUpperAscii(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return (char)std::toupper(c); });
  return s;
}

std::filesystem::path GetExecutableDir() {
#if defined(__linux__)
  char buf[4096] = {};
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len > 0) {
    buf[len] = '\0';
    return std::filesystem::path(buf).parent_path();
  }
#endif
  return {};
}

} // namespace

void SvgIconManager::Init(VulkanHandler *vkHandler) { vulkan_ = vkHandler; }

std::string SvgIconManager::ResolveSvgFilePath(const std::string &svgPath) {
  std::error_code ec;
  if (std::filesystem::exists(svgPath, ec))
    return svgPath;

  std::filesystem::path rel(svgPath);
  if (rel.is_relative()) {
    std::filesystem::path parentTry = std::filesystem::path("..") / rel;
    if (std::filesystem::exists(parentTry, ec))
      return parentTry.string();

    std::filesystem::path exeDir = GetExecutableDir();
    if (!exeDir.empty()) {
      std::filesystem::path exeTry = exeDir / rel;
      if (std::filesystem::exists(exeTry, ec))
        return exeTry.string();
      std::filesystem::path exeParentTry = exeDir.parent_path() / rel;
      if (std::filesystem::exists(exeParentTry, ec))
        return exeParentTry.string();
    }
  }
  return svgPath;
}

unsigned char *SvgIconManager::RasterizeSvg(const std::string &path, int size) {
  if (size <= 0)
    return nullptr;

  std::string resolved = ResolveSvgFilePath(path);
  NSVGimage *image = nsvgParseFromFile(resolved.c_str(), "px", 96.0f);
  if (!image || image->width <= 0.0f || image->height <= 0.0f) {
    if (image)
      nsvgDelete(image);
    Debug::Log("SvgIconManager: failed to parse SVG: " + path,
               Debug::LogLevel::WARNING);
    return nullptr;
  }

  NSVGrasterizer *rast = nsvgCreateRasterizer();
  if (!rast) {
    nsvgDelete(image);
    return nullptr;
  }

  size_t byteCount = (size_t)size * (size_t)size * 4;
  unsigned char *pixels = (unsigned char *)malloc(byteCount);
  if (!pixels) {
    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);
    return nullptr;
  }
  memset(pixels, 0, byteCount);

  float scaleX = (float)size / image->width;
  float scaleY = (float)size / image->height;
  float scale = (scaleX < scaleY) ? scaleX : scaleY;

  float offsetX = ((float)size - image->width * scale) * 0.5f;
  float offsetY = ((float)size - image->height * scale) * 0.5f;

  nsvgRasterize(rast, image, offsetX, offsetY, scale, pixels, size, size,
                size * 4);

  nsvgDeleteRasterizer(rast);
  nsvgDelete(image);

  return pixels;
}

ImTextureID SvgIconManager::UploadRgbaPixels(const unsigned char *pixels,
                                             int width, int height) {
  if (!vulkan_ || !pixels || width <= 0 || height <= 0)
    return (ImTextureID)0;

  VkDevice device = vulkan_->GetDevice();
  VkPhysicalDevice physDevice = vulkan_->GetPhysicalDevice();
  if (!device || !physDevice)
    return (ImTextureID)0;

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = static_cast<uint32_t>(width);
  imageInfo.extent.height = static_cast<uint32_t>(height);
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  imageInfo.tiling = VK_IMAGE_TILING_LINEAR;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
  imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VkImage textureImage = VK_NULL_HANDLE;
  if (vkCreateImage(device, &imageInfo, nullptr, &textureImage) != VK_SUCCESS) {
    return (ImTextureID)0;
  }

  VkMemoryRequirements memReqs{};
  vkGetImageMemoryRequirements(device, textureImage, &memReqs);

  VkPhysicalDeviceMemoryProperties memProps{};
  vkGetPhysicalDeviceMemoryProperties(physDevice, &memProps);

  uint32_t memTypeIndex = UINT32_MAX;
  for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
    if ((memReqs.memoryTypeBits & (1u << i)) &&
        (memProps.memoryTypes[i].propertyFlags &
         (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      memTypeIndex = i;
      break;
    }
  }
  if (memTypeIndex == UINT32_MAX) {
    vkDestroyImage(device, textureImage, nullptr);
    return (ImTextureID)0;
  }

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex = memTypeIndex;

  VkDeviceMemory texMemory = VK_NULL_HANDLE;
  if (vkAllocateMemory(device, &allocInfo, nullptr, &texMemory) != VK_SUCCESS) {
    vkDestroyImage(device, textureImage, nullptr);
    return (ImTextureID)0;
  }

  vkBindImageMemory(device, textureImage, texMemory, 0);

  VkImageSubresource subresource{};
  subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  subresource.mipLevel = 0;
  subresource.arrayLayer = 0;
  VkSubresourceLayout layout{};
  vkGetImageSubresourceLayout(device, textureImage, &subresource, &layout);

  void *mapped = nullptr;
  if (vkMapMemory(device, texMemory, 0, VK_WHOLE_SIZE, 0, &mapped) !=
      VK_SUCCESS) {
    vkFreeMemory(device, texMemory, nullptr);
    vkDestroyImage(device, textureImage, nullptr);
    return (ImTextureID)0;
  }

  const size_t srcRowBytes = static_cast<size_t>(width) * 4;
  unsigned char *dstBase =
      static_cast<unsigned char *>(mapped) + layout.offset;
  for (int y = 0; y < height; ++y) {
    unsigned char *dstRow = dstBase + static_cast<size_t>(y) * layout.rowPitch;
    const unsigned char *srcRow = pixels + static_cast<size_t>(y) * srcRowBytes;
    memcpy(dstRow, srcRow, srcRowBytes);
  }
  vkUnmapMemory(device, texMemory);

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = textureImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  VkImageView textureView = VK_NULL_HANDLE;
  if (vkCreateImageView(device, &viewInfo, nullptr, &textureView) !=
      VK_SUCCESS) {
    vkFreeMemory(device, texMemory, nullptr);
    vkDestroyImage(device, textureImage, nullptr);
    return (ImTextureID)0;
  }

  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;

  VkSampler sampler = VK_NULL_HANDLE;
  if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
    vkDestroyImageView(device, textureView, nullptr);
    vkFreeMemory(device, texMemory, nullptr);
    vkDestroyImage(device, textureImage, nullptr);
    return (ImTextureID)0;
  }

  VkDescriptorSet descSet = ImGui_ImplVulkan_AddTexture(
      sampler, textureView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  return (ImTextureID)descSet;
}

ImTextureID SvgIconManager::GetIcon(const std::string &svgPath, int size) {
  std::string key = svgPath + ":" + std::to_string(size);
  auto it = cache_.find(key);
  if (it != cache_.end())
    return it->second;

  if (!vulkan_)
    return (ImTextureID)0;

  unsigned char *pixels = RasterizeSvg(svgPath, size);
  if (!pixels)
    return (ImTextureID)0;

  ImTextureID texId = UploadRgbaPixels(pixels, size, size);
  free(pixels);

  if (texId)
    cache_[key] = texId;

  return texId;
}

void SvgIconManager::BakeFontIcons(ImFontAtlas *atlas, ImFont *font,
                                   float fontSizePx) {
  if (!atlas || !font)
    return;

  struct GlyphSpec {
    ImWchar codepoint;
    const char *svgPath;
  };

  static const GlyphSpec kGlyphs[] = {
      {0xE100, "assets/icons/svg/ui_hierarchy.svg"},
      {0xE101, "assets/icons/svg/ui_scene.svg"},
      {0xE102, "assets/icons/svg/ui_game.svg"},
      {0xE103, "assets/icons/svg/ui_inspector.svg"},
      {0xE104, "assets/icons/svg/ui_explorer.svg"},
      {0xE105, "assets/icons/svg/ui_console.svg"},
      {0xE106, "assets/icons/svg/ui_animation.svg"},
      {0xE107, "assets/icons/svg/ui_lighting.svg"},
      {0xE108, "assets/icons/svg/ui_device.svg"},
      {0xE109, "assets/icons/svg/ui_video.svg"},
      {0xE10A, "assets/icons/svg/ui_build.svg"},
      {0xE10B, "assets/icons/svg/ui_select.svg"},
      {0xE10C, "assets/icons/svg/ui_move.svg"},
      {0xE10D, "assets/icons/svg/ui_rotate.svg"},
      {0xE10E, "assets/icons/svg/ui_scale.svg"},
      {0xE10F, "assets/icons/svg/ui_grid.svg"},
      {0xE110, "assets/icons/svg/ui_snap.svg"},
      {0xE111, "assets/icons/svg/ui_refresh.svg"},
      {0xE112, "assets/icons/svg/ui_eye.svg"},
      {0xE113, "assets/icons/svg/ui_back.svg"},
      {0xE114, "assets/icons/svg/ui_filter.svg"},
      {0xE115, "assets/icons/svg/ui_search.svg"},
      {0xE116, "assets/icons/svg/ui_save.svg"},
      {0xE117, "assets/icons/svg/ui_text3d.svg"},
      {0xE118, "assets/icons/svg/ui_canvas.svg"},
      {0xE119, "assets/icons/svg/ui_warn.svg"},
      {0xE11A, "assets/icons/svg/ui_error.svg"},
      {0xE11B, "assets/icons/svg/info.svg"},
      {0xE11C, "assets/icons/svg/plus.svg"},
      {0xE11D, "assets/icons/svg/trash.svg"},
      {0xE11E, "assets/icons/svg/box.svg"},
      {0xE11F, "assets/icons/svg/camera.svg"},
  };

  const int iconPx = std::max(12, (int)std::round(fontSizePx - 1.0f));
  const float offsetY = std::max(0.0f, (fontSizePx - (float)iconPx) * 0.5f + 0.5f);

  std::vector<int> rectIds;
  rectIds.reserve(sizeof(kGlyphs) / sizeof(kGlyphs[0]));
  for (const auto &g : kGlyphs) {
    int id = atlas->AddCustomRectFontGlyph(
        font, g.codepoint, iconPx, iconPx, (float)iconPx + 2.0f,
        ImVec2(0.0f, offsetY));
    rectIds.push_back(id);
  }

  unsigned char *texPixels = nullptr;
  int texW = 0;
  int texH = 0;
  atlas->GetTexDataAsRGBA32(&texPixels, &texW, &texH);
  if (!texPixels || texW <= 0 || texH <= 0)
    return;

  ImU32 *dstAtlas = reinterpret_cast<ImU32 *>(texPixels);
  for (size_t i = 0; i < rectIds.size(); ++i) {
    const ImFontAtlasCustomRect *rect = atlas->GetCustomRectByIndex(rectIds[i]);
    if (!rect || !rect->IsPacked())
      continue;

    unsigned char *rgba = RasterizeSvg(kGlyphs[i].svgPath, iconPx);
    if (!rgba)
      continue;

    for (int y = 0; y < iconPx; ++y) {
      ImU32 *dstRow = dstAtlas + (size_t)(rect->Y + y) * (size_t)texW + rect->X;
      const unsigned char *srcRow = rgba + (size_t)y * (size_t)iconPx * 4;
      for (int x = 0; x < iconPx; ++x) {
        unsigned char a = srcRow[x * 4 + 3];
        dstRow[x] = IM_COL32(255, 255, 255, a);
      }
    }
    free(rgba);
  }
}

AssetIconInfo SvgIconManager::ResolveAssetIcon(
    const std::filesystem::path &path, bool isDirectory, bool isOpen) {
  AssetIconInfo info;

  std::string filenameLower = ToLowerAscii(path.filename().string());

  if (isDirectory) {
    info.accentColor = ImVec4(0.92f, 0.70f, 0.27f, 1.0f);
    if (isOpen) {
      info.svgPath = "assets/icons/svg/folder_open.svg";
      return info;
    }
    if (filenameLower == "assets") {
      info.svgPath = "assets/icons/svg/folder_assets.svg";
      info.accentColor = ImVec4(0.22f, 0.60f, 0.93f, 1.0f);
    } else if (filenameLower == "scripts" || filenameLower == "src" ||
               filenameLower == "include" || filenameLower == "code") {
      info.svgPath = "assets/icons/svg/folder_scripts.svg";
      info.accentColor = ImVec4(0.31f, 0.56f, 0.97f, 1.0f);
    } else if (filenameLower == "scenes" || filenameLower == "levels") {
      info.svgPath = "assets/icons/svg/folder_scenes.svg";
      info.accentColor = ImVec4(0.96f, 0.62f, 0.04f, 1.0f);
    } else if (filenameLower == "models" || filenameLower == "3dmodels" ||
               filenameLower == "meshes" || filenameLower == "characters") {
      info.svgPath = "assets/icons/svg/folder_models.svg";
      info.accentColor = ImVec4(0.08f, 0.72f, 0.65f, 1.0f);
    } else if (filenameLower == "audio" || filenameLower == "sounds" ||
               filenameLower == "music" || filenameLower == "sfx" ||
               filenameLower == "bgm") {
      info.svgPath = "assets/icons/svg/folder_audio.svg";
      info.accentColor = ImVec4(0.93f, 0.28f, 0.60f, 1.0f);
    } else {
      info.svgPath = "assets/icons/svg/folder.svg";
    }
    return info;
  }

  // Special filenames without conventional extensions
  if (filenameLower == "cmakelists.txt") {
    info.svgPath = "assets/icons/svg/ext_json.svg";
    info.accentColor = ImVec4(0.98f, 0.45f, 0.09f, 1.0f);
    info.badgeLabel = "CMAKE";
    return info;
  }
  if (filenameLower == "makefile" || filenameLower == "dockerfile" ||
      filenameLower == "flake.nix" || filenameLower == "shell.nix") {
    info.svgPath = "assets/icons/svg/ext_script.svg";
    info.accentColor = ImVec4(0.92f, 0.70f, 0.03f, 1.0f);
    info.badgeLabel = (filenameLower.find(".nix") != std::string::npos)
                          ? "NIX"
                          : "BUILD";
    return info;
  }

  std::string ext = ToLowerAscii(path.extension().string());

  // Blender DCC files (.blend, .blend1, .blend2, ...)
  if (ext == ".blend" || ext == ".blend1" || ext == ".blend2") {
    info.svgPath = "assets/icons/svg/ext_blend.svg";
    info.accentColor = ImVec4(0.92f, 0.46f, 0.00f, 1.0f);
    info.badgeLabel = (ext == ".blend") ? "BLEND" : ToUpperAscii(ext.substr(1));
    return info;
  }

  // 3D Models & Meshes
  if (ext == ".pmx" || ext == ".pmd" || ext == ".fbx" || ext == ".obj" ||
      ext == ".gltf" || ext == ".glb" || ext == ".dae" || ext == ".stl" ||
      ext == ".3ds" || ext == ".ply") {
    info.svgPath = "assets/icons/svg/ext_model.svg";
    info.accentColor = ImVec4(0.06f, 0.65f, 0.91f, 1.0f);
    info.badgeLabel = ToUpperAscii(ext.substr(1));
    info.isModelPreviewable =
        (ext == ".pmx" || ext == ".pmd" || ext == ".fbx" || ext == ".obj" ||
         ext == ".gltf" || ext == ".glb" || ext == ".dae");
    return info;
  }

  // Materials
  if (ext == ".mtl" || ext == ".mat" || ext == ".ilmeeemat") {
    info.svgPath = "assets/icons/svg/ext_material.svg";
    info.accentColor = ImVec4(0.66f, 0.33f, 0.97f, 1.0f);
    info.badgeLabel = (ext == ".mtl") ? "MTL" : "MAT";
    return info;
  }

  // Archives & Compressed Packages
  if (ext == ".zip" || ext == ".7z" || ext == ".rar" || ext == ".tar" ||
      ext == ".gz" || ext == ".tgz" || ext == ".xz" || ext == ".zst" ||
      ext == ".bz2") {
    info.svgPath = "assets/icons/svg/ext_archive.svg";
    info.accentColor = ImVec4(0.96f, 0.62f, 0.04f, 1.0f);
    info.badgeLabel = ToUpperAscii(ext.substr(1));
    return info;
  }

  // Android Packages
  if (ext == ".apk" || ext == ".aab") {
    info.svgPath = "assets/icons/svg/ext_android.svg";
    info.accentColor = ImVec4(0.24f, 0.86f, 0.52f, 1.0f);
    info.badgeLabel = ToUpperAscii(ext.substr(1));
    return info;
  }

  // Engine Scenes & Prefabs
  if (ext == ".ilmeeescene" || ext == ".unity" || ext == ".scene") {
    info.svgPath = "assets/icons/svg/ext_scene.svg";
    info.accentColor = ImVec4(0.23f, 0.51f, 0.96f, 1.0f);
    info.badgeLabel = "SCENE";
    return info;
  }
  if (ext == ".prefab" || ext == ".ilmeeeprefab") {
    info.svgPath = "assets/icons/svg/ext_prefab.svg";
    info.accentColor = ImVec4(0.15f, 0.39f, 0.92f, 1.0f);
    info.badgeLabel = "PREFAB";
    return info;
  }

  // Animations & State Controllers
  if (ext == ".vmd" || ext == ".ilmeeeanim" || ext == ".anim" ||
      ext == ".bvh") {
    info.svgPath = "assets/icons/svg/ext_anim.svg";
    info.accentColor = ImVec4(0.06f, 0.73f, 0.51f, 1.0f);
    info.badgeLabel = (ext == ".ilmeeeanim") ? "ANIM" : ToUpperAscii(ext.substr(1));
    return info;
  }
  if (ext == ".ilmeeectrl" || ext == ".controller") {
    info.svgPath = "assets/icons/svg/ext_controller.svg";
    info.accentColor = ImVec4(0.96f, 0.25f, 0.37f, 1.0f);
    info.badgeLabel = "CTRL";
    return info;
  }

  // C / C++ Source & Headers
  if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c") {
    info.svgPath = "assets/icons/svg/ext_cpp.svg";
    info.accentColor = ImVec4(0.23f, 0.51f, 0.96f, 1.0f);
    info.badgeLabel = (ext == ".c") ? "C" : "CPP";
    return info;
  }
  if (ext == ".hpp" || ext == ".h" || ext == ".hh" || ext == ".hxx" ||
      ext == ".inl") {
    info.svgPath = "assets/icons/svg/ext_header.svg";
    info.accentColor = ImVec4(0.66f, 0.33f, 0.97f, 1.0f);
    info.badgeLabel = (ext == ".h") ? "H" : "HPP";
    return info;
  }

  // Scripts
  if (ext == ".py" || ext == ".lua" || ext == ".sh" || ext == ".js" ||
      ext == ".ts" || ext == ".bat" || ext == ".nix") {
    info.svgPath = "assets/icons/svg/ext_script.svg";
    info.accentColor = ImVec4(0.92f, 0.70f, 0.03f, 1.0f);
    info.badgeLabel = ToUpperAscii(ext.substr(1));
    return info;
  }

  // Shaders & GPU Kernels
  if (ext == ".glsl" || ext == ".vert" || ext == ".frag" || ext == ".comp" ||
      ext == ".geom" || ext == ".tesc" || ext == ".tese" || ext == ".spv" ||
      ext == ".shader" || ext == ".hlsl" || ext == ".cl") {
    info.svgPath = "assets/icons/svg/ext_shader.svg";
    info.accentColor = ImVec4(0.93f, 0.28f, 0.60f, 1.0f);
    info.badgeLabel =
        (ext == ".shader") ? "SHDR" : ToUpperAscii(ext.substr(1));
    return info;
  }

  // Config / Structured Data
  if (ext == ".json" || ext == ".toml" || ext == ".yaml" || ext == ".yml" ||
      ext == ".ini" || ext == ".cfg" || ext == ".xml" || ext == ".cmake" ||
      ext == ".gradle" || ext == ".properties") {
    info.svgPath = "assets/icons/svg/ext_json.svg";
    info.accentColor = ImVec4(0.98f, 0.45f, 0.09f, 1.0f);
    std::string raw = ToUpperAscii(ext.substr(1));
    if (raw.size() > 5)
      raw = raw.substr(0, 5);
    info.badgeLabel = raw;
    return info;
  }

  // Text & Docs
  if (ext == ".txt" || ext == ".md" || ext == ".log" || ext == ".csv" ||
      ext == ".rst" || ext == ".license") {
    info.svgPath = "assets/icons/svg/ext_text.svg";
    info.accentColor = ImVec4(0.58f, 0.64f, 0.72f, 1.0f);
    info.badgeLabel = ToUpperAscii(ext.substr(1));
    return info;
  }

  // Fonts
  if (ext == ".ttf" || ext == ".otf" || ext == ".woff" || ext == ".woff2") {
    info.svgPath = "assets/icons/svg/ext_font.svg";
    info.accentColor = ImVec4(0.96f, 0.25f, 0.37f, 1.0f);
    info.badgeLabel = ToUpperAscii(ext.substr(1));
    return info;
  }

  // Binaries & Libraries
  if (ext == ".so" || ext == ".dll" || ext == ".a" || ext == ".lib" ||
      ext == ".o" || ext == ".obj" || ext == ".bin" || ext == ".exe" ||
      ext == ".dex") {
    info.svgPath = "assets/icons/svg/ext_binary.svg";
    info.accentColor = ImVec4(0.39f, 0.45f, 0.55f, 1.0f);
    info.badgeLabel = ToUpperAscii(ext.substr(1));
    return info;
  }

  // Images & Textures
  if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
      ext == ".tga" || ext == ".gif" || ext == ".hdr" || ext == ".psd" ||
      ext == ".webp" || ext == ".exr" || ext == ".ktx" || ext == ".dds" ||
      ext == ".svg") {
    info.svgPath = (ext == ".svg") ? path.string()
                                   : std::string("assets/icons/svg/ext_image.svg");
    info.accentColor = ImVec4(0.13f, 0.77f, 0.37f, 1.0f);
    info.badgeLabel = (ext == ".jpeg") ? "JPG" : ToUpperAscii(ext.substr(1));
    info.isImagePreviewable =
        (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
         ext == ".tga" || ext == ".gif" || ext == ".hdr" || ext == ".psd");
    return info;
  }

  // Audio
  if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac" ||
      ext == ".m4a" || ext == ".aac" || ext == ".opus") {
    info.svgPath = "assets/icons/svg/ext_audio.svg";
    info.accentColor = ImVec4(0.93f, 0.28f, 0.60f, 1.0f);
    info.badgeLabel = ToUpperAscii(ext.substr(1));
    return info;
  }

  // Video
  if (ext == ".mp4" || ext == ".webm" || ext == ".mkv" || ext == ".avi" ||
      ext == ".mov") {
    info.svgPath = "assets/icons/svg/ext_video.svg";
    info.accentColor = ImVec4(0.55f, 0.36f, 0.96f, 1.0f);
    info.badgeLabel = ToUpperAscii(ext.substr(1));
    info.isVideoPreviewable = true;
    return info;
  }

  // Fallback generic file
  info.svgPath = "assets/icons/svg/file.svg";
  info.accentColor = ImVec4(0.39f, 0.45f, 0.55f, 1.0f);
  if (ext.size() > 1) {
    std::string raw = ToUpperAscii(ext.substr(1));
    if (raw.size() > 5)
      raw = raw.substr(0, 5);
    info.badgeLabel = raw;
  } else {
    info.badgeLabel = "FILE";
  }
  return info;
}

void SvgIconManager::DrawIcon(const std::string &svgPath, int size) {
  ImTextureID tex = GetIcon(svgPath, size);
  if (tex) {
    ImGui::Image(tex, ImVec2((float)size, (float)size));
    ImGui::SameLine();
  }
}

bool SvgIconManager::DrawIconButton(const char *id, const std::string &svgPath,
                                    int size, const ImVec4 &bgColor,
                                    const ImVec4 &tintColor) {
  ImTextureID tex = GetIcon(svgPath, size);
  if (!tex)
    return false;

  ImGui::PushStyleColor(ImGuiCol_Button, bgColor);
  ImGui::PushStyleColor(
      ImGuiCol_ButtonHovered,
      ImVec4(std::min(1.0f, bgColor.x + 0.10f),
             std::min(1.0f, bgColor.y + 0.10f),
             std::min(1.0f, bgColor.z + 0.10f),
             std::max(0.85f, bgColor.w)));
  ImGui::PushStyleColor(
      ImGuiCol_ButtonActive,
      ImVec4(std::max(0.0f, bgColor.x - 0.04f),
             std::max(0.0f, bgColor.y - 0.04f),
             std::max(0.0f, bgColor.z - 0.04f),
             std::max(0.95f, bgColor.w)));

  bool clicked = ImGui::ImageButton(id, tex, ImVec2((float)size, (float)size),
                                    ImVec2(0, 0), ImVec2(1, 1),
                                    ImVec4(0, 0, 0, 0), tintColor);
  ImGui::PopStyleColor(3);

  return clicked;
}

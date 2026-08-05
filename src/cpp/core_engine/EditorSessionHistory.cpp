#include "../../../include/core_engine/EditorSessionHistory.hpp"
#include "../../../include/core_engine/UserDataDir.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <string_view>
#include <system_error>

namespace {

constexpr std::size_t kMaximumHistoryEntries = 256;

std::string YamlQuote(const std::string &value) {
  std::string result;
  result.reserve(value.size() + 2);
  result.push_back('"');
  for (unsigned char c : value) {
    switch (c) {
    case '\\':
      result += "\\\\";
      break;
    case '"':
      result += "\\\"";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      if (c < 0x20) {
        std::ostringstream escaped;
        escaped << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(c);
        result += escaped.str();
      } else {
        result.push_back(static_cast<char>(c));
      }
      break;
    }
  }
  result.push_back('"');
  return result;
}

std::string FloatText(float value) {
  std::ostringstream stream;
  stream << std::setprecision(std::numeric_limits<float>::max_digits10)
         << value;
  return stream.str();
}

std::string Indent(int count) {
  return std::string(static_cast<size_t>(count), ' ');
}

void WriteString(std::ostream &out, int indent, std::string_view key,
                 const std::string &value) {
  out << Indent(indent) << key << ": " << YamlQuote(value) << '\n';
}

void WriteBool(std::ostream &out, int indent, std::string_view key,
               bool value) {
  out << Indent(indent) << key << ": " << (value ? "true" : "false") << '\n';
}

void WriteInt(std::ostream &out, int indent, std::string_view key, int value) {
  out << Indent(indent) << key << ": " << value << '\n';
}

void WriteSize(std::ostream &out, int indent, std::string_view key,
               std::size_t value) {
  out << Indent(indent) << key << ": " << value << '\n';
}

void WriteFloat(std::ostream &out, int indent, std::string_view key,
                float value) {
  out << Indent(indent) << key << ": " << FloatText(value) << '\n';
}

void WriteVec2(std::ostream &out, int indent, std::string_view key,
               const glm::vec2 &value) {
  out << Indent(indent) << key << ": [" << FloatText(value.x) << ", "
      << FloatText(value.y) << "]\n";
}

void WriteVec3(std::ostream &out, int indent, std::string_view key,
               const glm::vec3 &value) {
  out << Indent(indent) << key << ": [" << FloatText(value.x) << ", "
      << FloatText(value.y) << ", " << FloatText(value.z) << "]\n";
}

void WriteVec4(std::ostream &out, int indent, std::string_view key,
               const glm::vec4 &value) {
  out << Indent(indent) << key << ": [" << FloatText(value.x) << ", "
      << FloatText(value.y) << ", " << FloatText(value.z) << ", "
      << FloatText(value.w) << "]\n";
}

void AppendFloat(std::ostringstream &out, float value) {
  out << std::setprecision(std::numeric_limits<float>::max_digits10) << value
      << ';';
}

void AppendString(std::ostringstream &out, const std::string &value) {
  out << value.size() << ':' << value << ';';
}

void AppendVec2(std::ostringstream &out, const glm::vec2 &value) {
  AppendFloat(out, value.x);
  AppendFloat(out, value.y);
}

void AppendVec3(std::ostringstream &out, const glm::vec3 &value) {
  AppendFloat(out, value.x);
  AppendFloat(out, value.y);
  AppendFloat(out, value.z);
}

void AppendVec4(std::ostringstream &out, const glm::vec4 &value) {
  AppendFloat(out, value.x);
  AppendFloat(out, value.y);
  AppendFloat(out, value.z);
  AppendFloat(out, value.w);
}

void AppendBool(std::ostringstream &out, bool value) {
  out << (value ? '1' : '0') << ';';
}

void AppendEntitySignature(std::ostringstream &out,
                           const SceneRenderer::EditorEntitySnapshot &entity) {
  AppendString(out, entity.name);
  AppendString(out, entity.path);
  AppendVec3(out, entity.position);
  AppendVec3(out, entity.rotation);
  AppendVec3(out, entity.scale);
  AppendBool(out, entity.isLight);
  AppendFloat(out, entity.lightGamma);
  AppendVec3(out, entity.lightColor);
  AppendFloat(out, entity.lightIntensity);
  out << entity.lightType << ';';
  AppendFloat(out, entity.lightRange);
  AppendFloat(out, entity.lightSpotAngle);
  AppendBool(out, entity.isCamera);
  out << entity.camProjection << ';';
  AppendFloat(out, entity.camFov);
  AppendFloat(out, entity.camOrthoSize);
  AppendFloat(out, entity.camNear);
  AppendFloat(out, entity.camFar);
  AppendBool(out, entity.hasPhysics);
  AppendBool(out, entity.useGravity);
  AppendBool(out, entity.isKinematic);
  AppendFloat(out, entity.mass);
  AppendFloat(out, entity.drag);
  AppendFloat(out, entity.gravityY);
  AppendVec3(out, entity.velocity);
  AppendBool(out, entity.hasAudio);
  AppendString(out, entity.audioPath);
  AppendBool(out, entity.isPlaying);
  AppendString(out, entity.debugSrcFile);
  out << entity.debugSrcLine << ';';
  out << entity.submeshTextures.size() << ';';
  for (const std::string &texture : entity.submeshTextures)
    AppendString(out, texture);
}

std::string SnapshotSignature(const EditorSessionHistory::Snapshot &snapshot) {
  std::ostringstream out;
  AppendString(out, snapshot.selectedName);
  out << snapshot.selectedSurface << ';';
  AppendString(out, snapshot.sceneName);
  out << snapshot.sceneObjects.size() << ';';
  for (const GameObject &object : snapshot.sceneObjects) {
    AppendString(out, object.name);
    AppendFloat(out, object.x);
    AppendFloat(out, object.y);
    AppendFloat(out, object.width);
    AppendFloat(out, object.height);
    AppendString(out, object.spritePath);
    AppendFloat(out, object.rotation);
    AppendFloat(out, object.scaleX);
    AppendFloat(out, object.scaleY);
    AppendBool(out, object.hasPhysics);
    AppendBool(out, object.useGravity);
    AppendBool(out, object.isKinematic);
    AppendFloat(out, object.mass);
    AppendFloat(out, object.drag);
    AppendFloat(out, object.gravityY);
    AppendBool(out, object.hasAudio);
    AppendString(out, object.audioPath);
    AppendBool(out, object.isPlaying);
  }

  AppendVec2(out, snapshot.renderer.cameraPosition);
  AppendFloat(out, snapshot.renderer.cameraZoom);
  AppendFloat(out, snapshot.renderer.zoom);
  AppendFloat(out, snapshot.renderer.gridSize);
  AppendVec4(out, snapshot.renderer.gridColor);
  AppendVec4(out, snapshot.renderer.backgroundColor);
  out << snapshot.renderer.editMode << ';';
  AppendBool(out, snapshot.renderer.gridVisible);
  AppendBool(out, snapshot.renderer.snapToGrid);
  AppendBool(out, snapshot.renderer.grid3dVisible);
  AppendBool(out, snapshot.renderer.sunVisible);
  AppendVec3(out, snapshot.renderer.camera3d.position);
  AppendFloat(out, snapshot.renderer.camera3d.yaw);
  AppendFloat(out, snapshot.renderer.camera3d.pitch);
  AppendFloat(out, snapshot.renderer.camera3d.fovDeg);
  AppendFloat(out, snapshot.renderer.camera3d.nearPlane);
  AppendFloat(out, snapshot.renderer.camera3d.farPlane);
  AppendFloat(out, snapshot.renderer.camera3d.moveSpeed);
  AppendFloat(out, snapshot.renderer.camera3d.mouseSensitivity);
  AppendVec3(out, snapshot.renderer.sunLight.direction);
  AppendVec3(out, snapshot.renderer.sunLight.color);
  AppendFloat(out, snapshot.renderer.sunLight.intensity);
  out << snapshot.renderer.entities.size() << ';';
  for (const auto &entity : snapshot.renderer.entities)
    AppendEntitySignature(out, entity);
  return out.str();
}

std::string SafeProjectSlug(const std::string &projectPath) {
  std::filesystem::path path(projectPath);
  std::string name = path.filename().string();
  if (name.empty())
    name = "standalone";

  std::string slug;
  slug.reserve(name.size());
  for (unsigned char c : name) {
    if (std::isalnum(c) || c == '-' || c == '_')
      slug.push_back(static_cast<char>(c));
    else
      slug.push_back('_');
  }
  if (slug.empty())
    slug = "standalone";
  return slug;
}

std::string SessionToken() {
  static constexpr char alphabet[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  std::mt19937 generator(static_cast<unsigned int>(
      std::chrono::high_resolution_clock::now().time_since_epoch().count()));
  std::string token;
  token.reserve(8);
  std::uniform_int_distribution<std::size_t> distribution(0,
                                                          sizeof(alphabet) - 2);
  for (int i = 0; i < 8; ++i)
    token.push_back(alphabet[distribution(generator)]);
  return token;
}

std::string CurrentDate() {
  std::time_t now = std::time(nullptr);
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  std::ostringstream out;
  out << std::put_time(&local, "%d-%m-%Y");
  return out.str();
}

void WriteGameObject(std::ostream &out, const GameObject &object) {
  out << "        - name: " << YamlQuote(object.name) << '\n';
  WriteFloat(out, 10, "x", object.x);
  WriteFloat(out, 10, "y", object.y);
  WriteFloat(out, 10, "width", object.width);
  WriteFloat(out, 10, "height", object.height);
  WriteString(out, 10, "sprite_path", object.spritePath);
  WriteFloat(out, 10, "rotation", object.rotation);
  WriteFloat(out, 10, "scale_x", object.scaleX);
  WriteFloat(out, 10, "scale_y", object.scaleY);
  WriteBool(out, 10, "has_physics", object.hasPhysics);
  WriteBool(out, 10, "use_gravity", object.useGravity);
  WriteBool(out, 10, "is_kinematic", object.isKinematic);
  WriteFloat(out, 10, "mass", object.mass);
  WriteFloat(out, 10, "drag", object.drag);
  WriteFloat(out, 10, "gravity_y", object.gravityY);
  WriteBool(out, 10, "has_audio", object.hasAudio);
  WriteString(out, 10, "audio_path", object.audioPath);
  WriteBool(out, 10, "is_playing", object.isPlaying);
}

void WriteEntity(std::ostream &out,
                 const SceneRenderer::EditorEntitySnapshot &entity) {
  out << "        - name: " << YamlQuote(entity.name) << '\n';
  WriteString(out, 10, "path", entity.path);
  WriteVec3(out, 10, "position", entity.position);
  WriteVec3(out, 10, "rotation", entity.rotation);
  WriteVec3(out, 10, "scale", entity.scale);

  WriteBool(out, 10, "is_light", entity.isLight);
  WriteFloat(out, 10, "light_gamma", entity.lightGamma);
  WriteVec3(out, 10, "light_color", entity.lightColor);
  WriteFloat(out, 10, "light_intensity", entity.lightIntensity);
  WriteInt(out, 10, "light_type", entity.lightType);
  WriteFloat(out, 10, "light_range", entity.lightRange);
  WriteFloat(out, 10, "light_spot_angle", entity.lightSpotAngle);

  WriteBool(out, 10, "is_camera", entity.isCamera);
  WriteInt(out, 10, "camera_projection", entity.camProjection);
  WriteFloat(out, 10, "camera_fov", entity.camFov);
  WriteFloat(out, 10, "camera_ortho_size", entity.camOrthoSize);
  WriteFloat(out, 10, "camera_near", entity.camNear);
  WriteFloat(out, 10, "camera_far", entity.camFar);

  WriteBool(out, 10, "has_physics", entity.hasPhysics);
  WriteBool(out, 10, "use_gravity", entity.useGravity);
  WriteBool(out, 10, "is_kinematic", entity.isKinematic);
  WriteFloat(out, 10, "mass", entity.mass);
  WriteFloat(out, 10, "drag", entity.drag);
  WriteFloat(out, 10, "gravity_y", entity.gravityY);
  WriteVec3(out, 10, "velocity", entity.velocity);
  WriteBool(out, 10, "has_audio", entity.hasAudio);
  WriteString(out, 10, "audio_path", entity.audioPath);
  WriteBool(out, 10, "is_playing", entity.isPlaying);

  WriteString(out, 10, "debug_source_file", entity.debugSrcFile);
  WriteInt(out, 10, "debug_source_line", entity.debugSrcLine);
  if (entity.submeshTextures.empty()) {
    out << "          textures: []\n";
  } else {
    out << "          textures:\n";
    for (std::size_t sub = 0; sub < entity.submeshTextures.size(); ++sub) {
      out << "            - index: " << sub << '\n';
      WriteString(out, 14, "path", entity.submeshTextures[sub]);
    }
  }
}

void WriteSnapshot(std::ostream &out, std::size_t index,
                   const EditorSessionHistory::Snapshot &snapshot) {
  out << "  - index: " << index << '\n';
  WriteString(out, 4, "selected_name", snapshot.selectedName);
  WriteInt(out, 4, "selected_surface", snapshot.selectedSurface);

  out << "    scene:\n";
  WriteString(out, 6, "name", snapshot.sceneName);
  if (snapshot.sceneObjects.empty()) {
    out << "      objects: []\n";
  } else {
    out << "      objects:\n";
    for (const GameObject &object : snapshot.sceneObjects)
      WriteGameObject(out, object);
  }

  const SceneRenderer::EditorSnapshot &renderer = snapshot.renderer;
  out << "    renderer:\n";
  WriteVec2(out, 6, "camera_position", renderer.cameraPosition);
  WriteFloat(out, 6, "camera_zoom", renderer.cameraZoom);
  WriteFloat(out, 6, "zoom", renderer.zoom);
  WriteFloat(out, 6, "grid_size", renderer.gridSize);
  WriteVec4(out, 6, "grid_color", renderer.gridColor);
  WriteVec4(out, 6, "background_color", renderer.backgroundColor);
  WriteInt(out, 6, "edit_mode", renderer.editMode);
  WriteBool(out, 6, "grid_visible", renderer.gridVisible);
  WriteBool(out, 6, "snap_to_grid", renderer.snapToGrid);
  WriteBool(out, 6, "grid_3d_visible", renderer.grid3dVisible);
  WriteBool(out, 6, "sun_visible", renderer.sunVisible);

  out << "      camera_3d:\n";
  WriteVec3(out, 8, "position", renderer.camera3d.position);
  WriteFloat(out, 8, "yaw", renderer.camera3d.yaw);
  WriteFloat(out, 8, "pitch", renderer.camera3d.pitch);
  WriteFloat(out, 8, "fov", renderer.camera3d.fovDeg);
  WriteFloat(out, 8, "near", renderer.camera3d.nearPlane);
  WriteFloat(out, 8, "far", renderer.camera3d.farPlane);
  WriteFloat(out, 8, "move_speed", renderer.camera3d.moveSpeed);
  WriteFloat(out, 8, "mouse_sensitivity", renderer.camera3d.mouseSensitivity);

  out << "      sun:\n";
  WriteVec3(out, 8, "direction", renderer.sunLight.direction);
  WriteVec3(out, 8, "color", renderer.sunLight.color);
  WriteFloat(out, 8, "intensity", renderer.sunLight.intensity);

  if (renderer.entities.empty()) {
    out << "      entities: []\n";
  } else {
    out << "      entities:\n";
    for (const auto &entity : renderer.entities)
      WriteEntity(out, entity);
  }
}

} // namespace

void EditorSessionHistory::Start(const std::string &projectPath,
                                 const Snapshot &initial) {
  Clear();
  projectPath_ = projectPath;

  const std::filesystem::path root = ilmeee::UserDataRoot();
  const std::string slug = SafeProjectSlug(projectPath);
  std::error_code ec;
  do {
    sessionDirectory_ = root / (slug + "-" + SessionToken());
  } while (std::filesystem::exists(sessionDirectory_, ec));
  std::filesystem::create_directories(sessionDirectory_, ec);

  sessionFilePath_ = sessionDirectory_ / (CurrentDate() + ".yml");
  history_.push_back(initial);
  cursor_ = 0;
  active_ = true;
  Persist();
}

void EditorSessionHistory::Push(const Snapshot &snapshot) {
  if (!active_)
    return;

  if (!history_.empty() &&
      SnapshotSignature(history_[cursor_]) == SnapshotSignature(snapshot)) {
    return;
  }

  if (cursor_ + 1 < history_.size())
    history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(cursor_ + 1),
                   history_.end());
  history_.push_back(snapshot);
  cursor_ = history_.size() - 1;

  if (history_.size() > kMaximumHistoryEntries) {
    history_.erase(history_.begin());
    --cursor_;
  }
  Persist();
}

bool EditorSessionHistory::CanUndo() const {
  return active_ && !history_.empty() && cursor_ > 0;
}

bool EditorSessionHistory::CanRedo() const {
  return active_ && !history_.empty() && cursor_ + 1 < history_.size();
}

bool EditorSessionHistory::Undo(Snapshot &out) {
  if (!CanUndo())
    return false;
  --cursor_;
  out = history_[cursor_];
  Persist();
  return true;
}

bool EditorSessionHistory::Redo(Snapshot &out) {
  if (!CanRedo())
    return false;
  ++cursor_;
  out = history_[cursor_];
  Persist();
  return true;
}

void EditorSessionHistory::Clear() {
  projectPath_.clear();
  sessionDirectory_.clear();
  sessionFilePath_.clear();
  history_.clear();
  cursor_ = 0;
  active_ = false;
}

void EditorSessionHistory::Persist() const {
  if (!active_ || sessionFilePath_.empty())
    return;

  std::error_code ec;
  std::filesystem::create_directories(sessionDirectory_, ec);
  std::ofstream out(sessionFilePath_, std::ios::trunc);
  if (!out.is_open())
    return;

  out << "format: ilmeee-editor-session-v1\n";
  WriteString(out, 0, "project", projectPath_);
  WriteString(out, 0, "session_directory", sessionDirectory_.string());
  WriteSize(out, 0, "cursor", cursor_);
  WriteSize(out, 0, "history_count", history_.size());
  if (history_.empty()) {
    out << "history: []\n";
  } else {
    out << "history:\n";
    for (std::size_t i = 0; i < history_.size(); ++i)
      WriteSnapshot(out, i, history_[i]);
  }
}

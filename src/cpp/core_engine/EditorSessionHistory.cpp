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

// String and List script values; scalar keys are written by the caller.
void WriteScriptExtras(std::ostream &out, int indent,
                       const ilmeee::ScriptValue &value) {
  using Type = ilmeee::ScriptValueType;
  if (value.type == Type::String)
    WriteString(out, indent, "string", value.string);
  if (value.type != Type::List)
    return;
  WriteInt(out, indent, "element_type", static_cast<int>(value.elementType));
  out << Indent(indent) << "items:" << (value.items.empty() ? " []\n" : "\n");
  for (const auto &item : value.items) {
    out << Indent(indent + 2) << "- ";
    if (item.type == Type::Float)
      out << FloatText(item.number);
    else if (item.type == Type::Bool)
      out << (item.boolean ? "true" : "false");
    else if (item.type == Type::Object)
      out << item.object;
    else if (item.type == Type::Vec3)
      out << '[' << FloatText(item.vec3[0]) << ", " << FloatText(item.vec3[1])
          << ", " << FloatText(item.vec3[2]) << ']';
    else
      out << YamlQuote(item.string);
    out << '\n';
  }
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

void AppendScriptValue(std::ostringstream &out, const ilmeee::ScriptValue &value) {
  AppendString(out, value.name);
  out << static_cast<uint32_t>(value.type) << ';' << value.object << ';';
  AppendFloat(out, value.number);
  AppendBool(out, value.boolean);
  for (float component : value.vec3)
    AppendFloat(out, component);
  AppendString(out, value.string);
  out << static_cast<uint32_t>(value.elementType) << ';' << value.items.size()
      << ';';
  for (const auto &item : value.items)
    AppendScriptValue(out, item);
}

void AppendEntitySignature(std::ostringstream &out,
                           const SceneRenderer::EditorEntitySnapshot &entity) {
  out << entity.objectId << ';' << entity.scripts.size() << ';';
  for (const auto &script : entity.scripts) {
    AppendString(out, script.source);
    AppendString(out, script.className);
    AppendBool(out, script.enabled);
    out << script.values.size() << ';';
    for (const auto &value : script.values)
      AppendScriptValue(out, value);
  }
  AppendString(out, entity.name);
  AppendString(out, entity.path);
  AppendVec3(out, entity.position);
  AppendVec3(out, entity.rotation);
  AppendVec3(out, entity.scale);
  AppendBool(out, entity.movable);
  AppendString(out, entity.movementScript);
  AppendFloat(out, entity.movementSpeed);
  out << entity.movementRig.camera << ';'
      << static_cast<int>(entity.movementRig.style) << ';';
  AppendFloat(out, entity.movementRig.distance);
  AppendFloat(out, entity.movementRig.height);
  AppendString(out, entity.movementRig.moveJoystick);
  AppendString(out, entity.movementRig.lookJoystick);
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
  AppendBool(out, entity.isAudioEmitter);
  AppendBool(out, entity.audio.enabled);
  AppendString(out, entity.audio.clip);
  AppendFloat(out, entity.audio.volume);
  AppendBool(out, entity.audio.loop);
  AppendBool(out, entity.audio.playOnStart);
  AppendBool(out, entity.audio.spatial);
  AppendFloat(out, entity.audio.minDistance);
  AppendFloat(out, entity.audio.maxDistance);
  AppendBool(out, entity.video.enabled);
  AppendString(out, entity.video.clip);
  AppendBool(out, entity.video.loop);
  AppendBool(out, entity.video.playOnStart);
  AppendFloat(out, entity.video.volume);
  AppendBool(out, entity.video.unlit);
  out << entity.video.surface << ';';
  AppendBool(out, entity.dialog.enabled);
  out << entity.dialog.box << ';' << entity.dialog.text << ';'
      << entity.dialog.speaker << ';' << entity.dialog.prompt << ';';
  AppendString(out, entity.dialog.lines);
  AppendString(out, entity.dialog.file);
  out << static_cast<uint32_t>(entity.dialog.trigger) << ';';
  AppendFloat(out, entity.dialog.range);
  AppendFloat(out, entity.dialog.charsPerSecond);
  AppendBool(out, entity.dialog.repeatable);
  AppendBool(out, entity.dialog.freezePlayer);
  AppendBool(out, entity.animation.enabled);
  AppendString(out, entity.animation.clip);
  AppendString(out, entity.animation.controller);
  AppendFloat(out, entity.animation.speed);
  AppendBool(out, entity.animation.loop);
  AppendBool(out, entity.isWorldText);
  AppendString(out, entity.worldText.text);
  AppendFloat(out, entity.worldText.size);
  AppendVec4(out, entity.worldText.color);
  out << entity.worldText.alignment << ';';
  AppendBool(out, entity.worldText.visible);
  AppendString(out, entity.debugSrcFile);
  out << entity.debugSrcLine << ';';
  out << entity.submeshTextures.size() << ';';
  for (const std::string &texture : entity.submeshTextures)
    AppendString(out, texture);
}

std::string SnapshotSignature(const EditorSessionHistory::Snapshot &snapshot) {
  std::ostringstream out;
  AppendString(out, snapshot.selectedName);
  AppendBool(out, snapshot.hasCanvas);
  out << snapshot.selectedUiElement << ';';
  AppendFloat(out, snapshot.ui.referenceWidth);
  AppendFloat(out, snapshot.ui.referenceHeight);
  for (const auto &e : snapshot.ui.elements) {
    SceneRenderer::EditorEntitySnapshot scripts;
    scripts.objectId = e.objectId;
    scripts.scripts = e.scripts;
    AppendEntitySignature(out, scripts);
    AppendString(out, e.id);
    out << static_cast<int>(e.kind) << ';' << static_cast<int>(e.anchor) << ';';
    for (float value : {e.x, e.y, e.width, e.height, e.fontSize,
                       e.color.x, e.color.y, e.color.z, e.color.w,
                       e.background.x, e.background.y, e.background.z, e.background.w})
      AppendFloat(out, value);
    AppendString(out, e.text);
    AppendString(out, e.imagePath);
    AppendString(out, e.action);
    AppendBool(out, e.visible);
  }
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
  out << "          object_id: " << entity.objectId << '\n';
  out << "          scripts:" << (entity.scripts.empty() ? " []\n" : "\n");
  for (const auto &script : entity.scripts) {
    out << "            - source: " << YamlQuote(script.source) << '\n';
    WriteString(out, 14, "class", script.className);
    WriteBool(out, 14, "enabled", script.enabled);
    out << "              fields:" << (script.values.empty() ? " []\n" : "\n");
    for (const auto &value : script.values) {
      out << "                - name: " << YamlQuote(value.name) << '\n';
      WriteInt(out, 18, "type", static_cast<int>(value.type));
      WriteFloat(out, 18, "number", value.number);
      WriteBool(out, 18, "boolean", value.boolean);
      out << "                  object_id: " << value.object << '\n';
      WriteVec3(out, 18, "vec3",
                {value.vec3[0], value.vec3[1], value.vec3[2]});
      WriteScriptExtras(out, 18, value);
    }
  }
  WriteString(out, 10, "path", entity.path);
  WriteVec3(out, 10, "position", entity.position);
  WriteVec3(out, 10, "rotation", entity.rotation);
  WriteVec3(out, 10, "scale", entity.scale);
  WriteBool(out, 10, "movable", entity.movable);
  WriteString(out, 10, "movement_script", entity.movementScript);
  WriteFloat(out, 10, "movement_speed", entity.movementSpeed);
  WriteInt(out, 10, "movement_camera", entity.movementRig.camera);
  WriteInt(out, 10, "camera_style", static_cast<int>(entity.movementRig.style));
  WriteFloat(out, 10, "camera_distance", entity.movementRig.distance);
  WriteFloat(out, 10, "camera_height", entity.movementRig.height);
  WriteString(out, 10, "move_joystick", entity.movementRig.moveJoystick);
  WriteString(out, 10, "look_joystick", entity.movementRig.lookJoystick);

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
  WriteBool(out, 10, "is_audio_source", entity.isAudioEmitter);
  WriteBool(out, 10, "audio_enabled", entity.audio.enabled);
  WriteString(out, 10, "audio_clip", entity.audio.clip);
  WriteFloat(out, 10, "audio_volume", entity.audio.volume);
  WriteBool(out, 10, "audio_loop", entity.audio.loop);
  WriteBool(out, 10, "audio_play_on_start", entity.audio.playOnStart);
  WriteBool(out, 10, "audio_spatial", entity.audio.spatial);
  WriteFloat(out, 10, "audio_min_distance", entity.audio.minDistance);
  WriteFloat(out, 10, "audio_max_distance", entity.audio.maxDistance);
  WriteBool(out, 10, "video_enabled", entity.video.enabled);
  WriteString(out, 10, "video_clip", entity.video.clip);
  WriteBool(out, 10, "video_loop", entity.video.loop);
  WriteBool(out, 10, "video_play_on_start", entity.video.playOnStart);
  WriteFloat(out, 10, "video_volume", entity.video.volume);
  WriteBool(out, 10, "video_unlit", entity.video.unlit);
  WriteInt(out, 10, "video_surface", entity.video.surface);
  WriteBool(out, 10, "dialog_enabled", entity.dialog.enabled);
  out << "          dialog_ui: [" << entity.dialog.box << ", "
      << entity.dialog.text << ", " << entity.dialog.speaker << ", "
      << entity.dialog.prompt << "]\n";
  WriteString(out, 10, "dialog_lines", entity.dialog.lines);
  WriteString(out, 10, "dialog_file", entity.dialog.file);
  WriteInt(out, 10, "dialog_trigger", static_cast<int>(entity.dialog.trigger));
  WriteFloat(out, 10, "dialog_range", entity.dialog.range);
  WriteFloat(out, 10, "dialog_speed", entity.dialog.charsPerSecond);
  WriteBool(out, 10, "dialog_repeatable", entity.dialog.repeatable);
  WriteBool(out, 10, "dialog_freeze_player", entity.dialog.freezePlayer);
  WriteBool(out, 10, "animation_enabled", entity.animation.enabled);
  WriteString(out, 10, "animation_clip", entity.animation.clip);
  WriteString(out, 10, "animation_controller", entity.animation.controller);
  WriteFloat(out, 10, "animation_speed", entity.animation.speed);
  WriteBool(out, 10, "animation_loop", entity.animation.loop);
  WriteBool(out, 10, "world_text", entity.isWorldText);
  WriteString(out, 10, "text", entity.worldText.text);
  WriteFloat(out, 10, "text_size", entity.worldText.size);
  WriteVec4(out, 10, "text_color", entity.worldText.color);
  WriteInt(out, 10, "text_alignment", entity.worldText.alignment);
  WriteBool(out, 10, "text_visible", entity.worldText.visible);

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
  WriteBool(out, 4, "has_canvas", snapshot.hasCanvas);
  WriteInt(out, 4, "selected_ui_element", snapshot.selectedUiElement);
  out << "    canvas:\n";
  WriteFloat(out, 6, "reference_width", snapshot.ui.referenceWidth);
  WriteFloat(out, 6, "reference_height", snapshot.ui.referenceHeight);
  out << (snapshot.ui.elements.empty() ? "      elements: []\n" : "      elements:\n");
  for (const auto &e : snapshot.ui.elements) {
    out << "        - name: " << YamlQuote(e.id) << '\n';
    out << "          object_id: " << e.objectId << '\n';
    WriteInt(out, 10, "kind", static_cast<int>(e.kind));
    WriteInt(out, 10, "anchor", static_cast<int>(e.anchor));
    WriteVec2(out, 10, "position", {e.x, e.y});
    WriteVec2(out, 10, "size", {e.width, e.height});
    WriteFloat(out, 10, "font_size", e.fontSize);
    WriteString(out, 10, "text", e.text);
    WriteString(out, 10, "image", e.imagePath);
    WriteString(out, 10, "action", e.action);
    WriteBool(out, 10, "visible", e.visible);
    WriteVec4(out, 10, "color", {e.color.x, e.color.y, e.color.z, e.color.w});
    WriteVec4(out, 10, "background", {e.background.x, e.background.y, e.background.z, e.background.w});
    out << (e.scripts.empty() ? "          scripts: []\n" : "          scripts:\n");
    for (const auto &script : e.scripts) {
      WriteString(out, 12, "- source", script.source);
      WriteString(out, 14, "class", script.className);
      WriteBool(out, 14, "enabled", script.enabled);
      out << (script.values.empty() ? "              values: []\n" : "              values:\n");
      for (const auto &value : script.values) {
        WriteString(out, 16, "- name", value.name);
        WriteInt(out, 18, "type", static_cast<int>(value.type));
        WriteFloat(out, 18, "number", value.number);
        WriteBool(out, 18, "boolean", value.boolean);
        out << "                  object: " << value.object << '\n';
        WriteVec3(out, 18, "vec3", {value.vec3[0], value.vec3[1], value.vec3[2]});
        WriteScriptExtras(out, 18, value);
      }
    }
  }

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
  history_.push_back({std::make_shared<const Snapshot>(initial),
                      SnapshotSignature(initial)});
  cursor_ = 0;
  active_ = true;
  Persist();
}

void EditorSessionHistory::Push(const Snapshot &snapshot) {
  if (!active_)
    return;

  std::string signature = SnapshotSignature(snapshot);
  if (!history_.empty() && history_[cursor_].signature == signature)
    return;

  if (cursor_ + 1 < history_.size())
    history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(cursor_ + 1),
                   history_.end());
  history_.push_back(
      {std::make_shared<const Snapshot>(snapshot), std::move(signature)});
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
  out = *history_[cursor_].snapshot;
  Persist();
  return true;
}

bool EditorSessionHistory::Redo(Snapshot &out) {
  if (!CanRedo())
    return false;
  ++cursor_;
  out = *history_[cursor_].snapshot;
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

void EditorSessionHistory::Persist() {
  if (!active_ || sessionFilePath_.empty())
    return;
  PersistJob job{projectPath_, sessionDirectory_, sessionFilePath_, cursor_, {}};
  job.history.reserve(history_.size());
  for (const auto &entry : history_)
    job.history.push_back(entry.snapshot);
  {
    std::lock_guard lock(writerMutex_);
    pendingJob_ = std::move(job);
    if (!writer_.joinable())
      writer_ = std::thread(&EditorSessionHistory::WriterLoop, this);
  }
  writerWake_.notify_one();
}

void EditorSessionHistory::WriterLoop() {
  for (;;) {
    PersistJob job;
    {
      std::unique_lock lock(writerMutex_);
      writerWake_.wait(lock, [&] { return writerStop_ || pendingJob_; });
      if (!pendingJob_)
        return; // stopping and nothing left to write
      job = std::move(*pendingJob_);
      pendingJob_.reset();
    }

    std::error_code ec;
    std::filesystem::create_directories(job.sessionDirectory, ec);
    // Write beside the target and rename, so a crash mid-write never leaves
    // a truncated session file.
    std::filesystem::path temp = job.sessionFilePath;
    temp += ".tmp";
    {
      std::ofstream out(temp, std::ios::trunc);
      if (!out.is_open())
        continue;
      out << "format: ilmeee-editor-session-v1\n";
      WriteString(out, 0, "project", job.projectPath);
      WriteString(out, 0, "session_directory", job.sessionDirectory.string());
      WriteSize(out, 0, "cursor", job.cursor);
      WriteSize(out, 0, "history_count", job.history.size());
      if (job.history.empty()) {
        out << "history: []\n";
      } else {
        out << "history:\n";
        for (std::size_t i = 0; i < job.history.size(); ++i)
          WriteSnapshot(out, i, *job.history[i]);
      }
    }
    std::filesystem::rename(temp, job.sessionFilePath, ec);
  }
}

EditorSessionHistory::~EditorSessionHistory() {
  {
    std::lock_guard lock(writerMutex_);
    writerStop_ = true; // the last queued write still happens
  }
  writerWake_.notify_one();
  if (writer_.joinable())
    writer_.join();
}

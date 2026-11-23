// Realtime collaboration in the editor: Windows > Realtime Collab.
//
// Glue between the scene and ilmeee::net::CollabClient (protocol + conflict
// rules live there, the per-object wire format in CollabCodec.hpp):
//   local  -> room: CollabSync diffs every object against collabBaseline
//                   (what we last sent or applied) and sends only changes.
//                   Transforms are checked every 50 ms (live gizmo drags);
//                   everything else after an edit was recorded, and once a
//                   second as a safety net for edits that record no history.
//   room -> local : CollabApply updates objects in place when their geometry
//                   is unchanged, recreates them otherwise, and rebases the
//                   baseline from the result so nothing echoes back.
// Remote ops that arrive during Play are held and applied after Stop.
#include "../../../include/ui/MainWindow.hpp"
#include "../../../include/core_engine/SceneLoader.hpp"
#include "../../../include/core_engine/UserDataDir.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>
#include <cstdlib>
#include <fstream>

using namespace ImGui;
using ilmeee::net::CollabOp;
using ilmeee::net::CollabState;
namespace collab = ilmeee::collab;
namespace fs = std::filesystem;

namespace {

constexpr float kXformInterval = 0.05f;  // live transform rate (20 Hz)
constexpr float kFullInterval = 1.0f;    // safety full diff
constexpr float kPresenceInterval = 0.25f;

fs::path CollabSettingsPath() { return ilmeee::UserDataRoot() / "collab.json"; }

uint32_t PackColor(const float c[3]) {
  auto ch = [](float v) { return (uint32_t)std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f); };
  return ch(c[0]) << 16 | ch(c[1]) << 8 | ch(c[2]);
}

ImVec4 UnpackColor(uint32_t c) {
  return ImVec4((float)(c >> 16 & 255) / 255.0f, (float)(c >> 8 & 255) / 255.0f,
                (float)(c & 255) / 255.0f, 1.0f);
}

// Room names allow [A-Za-z0-9_.-]; derive one from the project folder.
std::string SanitizeRoom(const std::string &in) {
  std::string out;
  for (char c : in)
    if (std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.')
      out += c;
    else if (!out.empty() && out.back() != '-')
      out += '-';
  while (!out.empty() && (out.front() == '.' || out.front() == '-'))
    out.erase(out.begin());
  if (out.size() > 64)
    out.resize(64);
  return out;
}

bool SameTextures(const std::vector<ilmeee::SurfaceTexture> &a,
                  const std::vector<ilmeee::SurfaceTexture> &b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (a[i].surfaceIndex != b[i].surfaceIndex || a[i].texturePath != b[i].texturePath)
      return false;
  return true;
}

} // namespace

// ---- settings ---------------------------------------------------------------

void MainWindow::LoadCollabSettings() {
  collabSettingsLoaded = true;
  std::snprintf(collabUrl, sizeof collabUrl, "%s", ilmeee::net::kCollabDefaultUrl);
  const char *user = std::getenv("USER");
  std::snprintf(collabName, sizeof collabName, "%s", user && *user ? user : "editor");
  const std::string project = projectHandler.projectPath.empty()
                                  ? std::string("sandbox")
                                  : fs::path(projectHandler.projectPath).filename().string();
  std::snprintf(collabRoom, sizeof collabRoom, "%s", SanitizeRoom(project).c_str());

  std::ifstream f(CollabSettingsPath());
  if (!f)
    return;
  auto j = ilmeee::net::json::parse(f, nullptr, false);
  if (!j.is_object())
    return;
  auto str = [&](const char *k, char *dst, size_t n) {
    if (auto it = j.find(k); it != j.end() && it->is_string() && !it->get<std::string>().empty())
      std::snprintf(dst, n, "%s", it->get<std::string>().c_str());
  };
  str("url", collabUrl, sizeof collabUrl);
  str("token", collabToken, sizeof collabToken);
  str("name", collabName, sizeof collabName);
  // Room is per project: remembered under rooms[<project path>].
  if (auto rooms = j.find("rooms"); rooms != j.end() && rooms->is_object()) {
    auto it = rooms->find(projectHandler.projectPath);
    if (it != rooms->end() && it->is_string())
      std::snprintf(collabRoom, sizeof collabRoom, "%s", it->get<std::string>().c_str());
  }
  if (auto c = j.find("color"); c != j.end() && c->is_number_unsigned()) {
    const ImVec4 v = UnpackColor(c->get<uint32_t>());
    collabColor[0] = v.x;
    collabColor[1] = v.y;
    collabColor[2] = v.z;
  }
}

void MainWindow::SaveCollabSettings() {
  const fs::path path = CollabSettingsPath();
  ilmeee::net::json j = ilmeee::net::json::object();
  if (std::ifstream in(path); in) {
    auto old = ilmeee::net::json::parse(in, nullptr, false);
    if (old.is_object())
      j = old;
  }
  j["url"] = collabUrl;
  j["token"] = collabToken;
  j["name"] = collabName;
  j["color"] = PackColor(collabColor);
  if (!j.contains("rooms") || !j["rooms"].is_object())
    j["rooms"] = ilmeee::net::json::object();
  j["rooms"][projectHandler.projectPath] = collabRoom;
  {
    std::ofstream out(path, std::ios::trunc);
    out << j.dump(2);
  }
  std::error_code ec; // holds the server token
  fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write,
                  fs::perm_options::replace, ec);
}

// ---- lifecycle --------------------------------------------------------------

bool MainWindow::CollabJoined() const {
  return collab && collab->State() == CollabState::Joined;
}

void MainWindow::StartCollab() {
  if (!sceneRenderer)
    return;
  SaveCollabSettings();
  collab = std::make_unique<ilmeee::net::CollabClient>();
  collab->onLog = [this](const std::string &m, bool error) {
    PushMessage(error ? "[WARNING] " + m : m);
  };
  collab->onJoined = [this](const std::vector<CollabOp> &state, bool fresh) {
    CollabOnJoined(state, fresh);
  };
  collab->onOp = [this](const CollabOp &op) {
    if (SceneStructureLocked())
      collabQueue.push_back(op);
    else
      CollabApply(op);
  };
  collab->onResend = [this](const std::string &key) {
    // A remote delete lost against our write: send the whole object again.
    if (const uint64_t id = collab::ParseObjectKey(key))
      collabBaseline.erase(id);
    collabDirty = true;
  };

  ilmeee::net::CollabOptions o;
  o.url = collabUrl;
  o.token = collabToken;
  o.room = collabRoom;
  o.name = collabName;
  o.color = PackColor(collabColor);
  collabBaseline.clear();
  collabPendingXform.clear();
  collabOrphans.clear();
  collabQueue.clear();
  collabEnvBaseline.clear();
  collabResetPending = false;
  collabOpsSent = collabOpsApplied = 0;
  if (!collab->Start(o))
    PushMessage("[ERROR] Realtime collab: " + collab->LastError());
}

void MainWindow::StopCollab() {
  if (collab)
    collab->Stop();
  collabQueue.clear();
  collabResetPending = false;
}

void MainWindow::UpdateCollab(float deltaTime) {
  if (!collab)
    return;
  collab->Poll();
  if (!CollabJoined() || !sceneRenderer)
    return;

  if (SceneStructureLocked())
    return; // Play mode: neither send runtime changes nor apply remote ones

  if (collabResetPending) {
    // Joined while playing: the room's state was held until Stop.
    std::vector<CollabOp> state;
    state.swap(collabQueue);
    collabResetPending = false;
    CollabOnJoined(state, false);
  } else if (!collabQueue.empty()) {
    std::vector<CollabOp> queued;
    queued.swap(collabQueue);
    for (const auto &op : queued)
      CollabApply(op);
  }

  collabXformTimer += deltaTime;
  collabFullTimer += deltaTime;
  collabPresenceTimer += deltaTime;
  const bool full = collabDirty || collabFullTimer >= kFullInterval;
  if (full || collabXformTimer >= kXformInterval) {
    CollabSync(full);
    collabXformTimer = 0.0f;
    if (full) {
      collabFullTimer = 0.0f;
      collabDirty = false;
    }
  }

  if (collabPresenceTimer >= kPresenceInterval) {
    collabPresenceTimer = 0.0f;
    std::string selection;
    for (size_t i = 0; i < sceneRenderer->GetMesh3DCount(); ++i)
      if (objectName[0] && sceneRenderer->GetMesh3DName(i) == objectName) {
        selection = collab::ObjectKey(sceneRenderer->meshes3d[i].objectId);
        break;
      }
    ilmeee::net::json presence = {{"selection", selection},
                                  {"selectionName", std::string(objectName)}};
    std::string dumped = presence.dump();
    if (dumped != collabLastPresence && collab->SetPresence(presence))
      collabLastPresence = std::move(dumped);
  }
}

// ---- local -> room ----------------------------------------------------------

int MainWindow::CollabFindMesh(uint64_t objectId) const {
  if (!sceneRenderer || !objectId)
    return -1;
  for (size_t i = 0; i < sceneRenderer->GetMesh3DCount(); ++i)
    if (sceneRenderer->meshes3d[i].objectId == objectId)
      return (int)i;
  return -1;
}

static ilmeee::net::json EncodeMeshData(const SceneRenderer &r, size_t i,
                                        const std::string &projectRoot) {
  const ilmeee::SceneEntity e = ilmeee::CaptureSceneEntity(r, i, projectRoot);
  const int parent = r.GetMesh3DParent(i);
  const int cam = r.meshes3d[i].movementRig.camera;
  const uint64_t parentId =
      parent >= 0 && (size_t)parent < r.GetMesh3DCount() ? r.meshes3d[(size_t)parent].objectId : 0;
  const uint64_t camId =
      cam >= 0 && (size_t)cam < r.GetMesh3DCount() ? r.meshes3d[(size_t)cam].objectId : 0;
  return collab::EncodeEntityData(e, parentId, camId);
}

static collab::Xform MeshXform(const SceneRenderer &r, size_t i) {
  return {r.GetMesh3DPosition(i), r.GetMesh3DRotation(i), r.GetMesh3DScale(i)};
}

static ilmeee::IlmeeeScene CaptureEnvironment(const SceneRenderer &r) {
  ilmeee::IlmeeeScene env;
  env.backgroundColor = r.GetBackgroundColor();
  env.npr = r.GetNpr();
  env.bloom = r.GetBloom();
  return env;
}

void MainWindow::CollabRebaseline(size_t i) {
  if (!sceneRenderer || i >= sceneRenderer->GetMesh3DCount())
    return;
  auto &b = collabBaseline[sceneRenderer->meshes3d[i].objectId];
  b.data = EncodeMeshData(*sceneRenderer, i, projectHandler.projectPath).dump();
  b.xform = MeshXform(*sceneRenderer, i);
}

void MainWindow::CollabSync(bool full) {
  SceneRenderer &r = *sceneRenderer;
  std::unordered_set<uint64_t> present;
  present.reserve(r.GetMesh3DCount());
  for (size_t i = 0; i < r.GetMesh3DCount(); ++i) {
    auto &mesh = r.meshes3d[i];
    if (!mesh.objectId)
      mesh.objectId = ilmeee::NewObjectId();
    present.insert(mesh.objectId);
    const std::string key = collab::ObjectKey(mesh.objectId);
    auto it = collabBaseline.find(mesh.objectId);
    if (it == collabBaseline.end() && !full) {
      collabDirty = true; // a new object: needs the full pass
      continue;
    }
    if (full) {
      ilmeee::net::json data = EncodeMeshData(r, i, projectHandler.projectPath);
      std::string dumped = data.dump();
      if (it == collabBaseline.end() || it->second.data != dumped) {
        if (collab->Put(key, collab::kSlotData, std::move(data))) {
          ++collabOpsSent;
          auto &b = collabBaseline[mesh.objectId];
          const bool created = it == collabBaseline.end();
          b.data = std::move(dumped);
          if (created)
            b.xform = collab::Xform{glm::vec3(NAN)}; // force the xform below
          it = collabBaseline.find(mesh.objectId);
        }
      }
    }
    if (it == collabBaseline.end())
      continue;
    const collab::Xform x = MeshXform(r, i);
    if (!(it->second.xform == x) &&
        collab->Put(key, collab::kSlotXform, collab::EncodeXform(x))) {
      ++collabOpsSent;
      it->second.xform = x;
    }
  }

  // Objects that are gone locally.
  for (auto it = collabBaseline.begin(); it != collabBaseline.end();) {
    if (present.contains(it->first)) {
      ++it;
      continue;
    }
    if (collab->Delete(collab::ObjectKey(it->first)))
      ++collabOpsSent;
    it = collabBaseline.erase(it);
  }

  if (full) {
    ilmeee::net::json env = collab::EncodeEnvironment(CaptureEnvironment(r));
    std::string dumped = env.dump();
    if (dumped != collabEnvBaseline &&
        collab->Put(collab::kEnvironmentKey, collab::kSlotData, std::move(env))) {
      ++collabOpsSent;
      collabEnvBaseline = std::move(dumped);
    }
  }
}

// ---- room -> local ----------------------------------------------------------

void MainWindow::CollabOnJoined(const std::vector<CollabOp> &state, bool fresh) {
  collabBaseline.clear();
  collabPendingXform.clear();
  collabOrphans.clear();
  collabQueue.clear();
  collabEnvBaseline.clear();
  if (!sceneRenderer)
    return;
  if (fresh) {
    // Empty room: our scene becomes the room's scene.
    collabDirty = true;
    PushMessage("Realtime collab: room '" + std::string(collabRoom) +
                "' was empty, uploading this scene");
    return;
  }
  if (SceneStructureLocked()) {
    collabQueue = state;
    collabResetPending = true;
    PushMessage("[WARNING] Realtime collab: stop Play to load the room's scene");
    return;
  }
  // The room has a scene: it replaces ours.
  while (sceneRenderer->GetMesh3DCount() > 0)
    sceneRenderer->RemoveMesh3D(sceneRenderer->GetMesh3DCount() - 1);
  objectName[0] = '\0';
  selectedSurface = -1;
  for (const auto &op : state)
    CollabApply(op);
  // Objects whose parent never arrived stay roots.
  collabOrphans.clear();
  PushMessage("Realtime collab: loaded room '" + std::string(collabRoom) + "' (" +
              std::to_string(sceneRenderer->GetMesh3DCount()) + " objects)");
  RecordEditorHistory();
}

void MainWindow::CollabApply(const CollabOp &op) {
  if (!sceneRenderer)
    return;
  SceneRenderer &r = *sceneRenderer;
  const std::string &root = projectHandler.projectPath;
  ++collabOpsApplied;

  if (op.key == collab::kEnvironmentKey) {
    ilmeee::IlmeeeScene env;
    if (op.del || !collab::DecodeEnvironment(op.data, env))
      return;
    r.SetBackgroundColor(env.backgroundColor.r, env.backgroundColor.g,
                         env.backgroundColor.b, env.backgroundColor.a);
    r.SetNpr(env.npr);
    r.SetBloom(env.bloom);
    collabEnvBaseline = collab::EncodeEnvironment(CaptureEnvironment(r)).dump();
    return;
  }

  const uint64_t id = collab::ParseObjectKey(op.key);
  if (!id)
    return;

  if (op.del) {
    collabPendingXform.erase(id);
    collabOrphans.erase(id);
    const int idx = CollabFindMesh(id);
    if (idx < 0)
      return;
    r.RemoveMesh3D((size_t)idx); // with its children, as the sender did
    std::unordered_set<uint64_t> present;
    for (const auto &m : r.meshes3d)
      present.insert(m.objectId);
    std::erase_if(collabBaseline, [&](const auto &kv) { return !present.contains(kv.first); });
    bool selectionAlive = false;
    for (size_t i = 0; i < r.GetMesh3DCount() && !selectionAlive; ++i)
      selectionAlive = r.GetMesh3DName(i) == objectName;
    if (!selectionAlive) {
      objectName[0] = '\0';
      selectedSurface = -1;
    }
    return;
  }

  if (op.slot == collab::kSlotXform) {
    collab::Xform x;
    if (!collab::DecodeXform(op.data, x))
      return;
    const int idx = CollabFindMesh(id);
    if (idx < 0) {
      collabPendingXform[id] = x; // its data comes later
      return;
    }
    r.SetMesh3DTransform((size_t)idx, x.p, x.r, x.s);
    collabBaseline[id].xform = MeshXform(r, (size_t)idx);
    return;
  }

  if (op.slot != collab::kSlotData)
    return;
  collab::EntityData ed;
  if (!collab::DecodeEntityData(op.data, ed)) {
    PushMessage("[WARNING] Realtime collab: could not decode object " + op.key);
    return;
  }
  ed.entity.objectId = id;

  int idx = CollabFindMesh(id);
  // Keep the transform we have; the xform slot owns it.
  collab::Xform x;
  if (idx >= 0)
    x = MeshXform(r, (size_t)idx);
  else if (auto p = collabPendingXform.find(id); p != collabPendingXform.end())
    x = p->second;
  collabPendingXform.erase(id);
  ed.entity.position = x.p;
  ed.entity.rotationEuler = x.r;
  ed.entity.scale = x.s;

  bool sameGeometry = false;
  if (idx >= 0) {
    const ilmeee::SceneEntity cur = ilmeee::CaptureSceneEntity(r, (size_t)idx, root);
    sameGeometry = cur.kind == ed.entity.kind &&
                   cur.externalPath == ed.entity.externalPath &&
                   ed.entity.kind != ilmeee::PrimitiveKind::WorldText;
    if (sameGeometry) {
      const bool texturesChanged =
          !SameTextures(cur.surfaceTextures, ed.entity.surfaceTextures);
      if (texturesChanged)
        for (uint32_t s = 0; s < r.meshes3d[(size_t)idx].submeshes.size(); ++s)
          r.ClearMesh3DSubmeshTexture((size_t)idx, s);
      ilmeee::ApplySceneEntityState(r, (size_t)idx, ed.entity, root, texturesChanged);
    }
  }

  if (!sameGeometry) {
    const int old = idx;
    if (old >= 0)
      r.meshes3d[(size_t)old].objectId = 0; // the replacement takes the id
    const int created = ilmeee::LoadSceneEntity(r, ed.entity, root);
    if (created < 0) {
      if (old >= 0)
        r.meshes3d[(size_t)old].objectId = id;
      PushMessage("[WARNING] Realtime collab: could not load '" + ed.entity.name +
                  "'" + (ed.entity.externalPath.empty() ? std::string()
                                                        : " (" + ed.entity.externalPath +
                                                              " missing in this project?)"));
      return;
    }
    idx = created;
    if (old >= 0) {
      // Hand the old object's children and place over, then drop it.
      for (size_t j = 0; j < r.GetMesh3DCount(); ++j)
        if (r.GetMesh3DParent(j) == old)
          r.SetMesh3DParent(j, idx, false);
      r.SetMesh3DParent((size_t)idx, r.GetMesh3DParent((size_t)old), false);
      r.RemoveMesh3D((size_t)old);
      idx = CollabFindMesh(id);
      if (idx < 0)
        return;
    }
  }

  // Parent link (by object id; the parent may not have arrived yet).
  const int curParent = r.GetMesh3DParent((size_t)idx);
  const uint64_t curParentId = curParent >= 0 ? r.meshes3d[(size_t)curParent].objectId : 0;
  if (curParentId != ed.parentId) {
    if (!ed.parentId) {
      r.SetMesh3DParent((size_t)idx, -1, false);
    } else if (const int p = CollabFindMesh(ed.parentId); p >= 0) {
      r.SetMesh3DParent((size_t)idx, p, false);
      collabOrphans.erase(id);
    } else {
      collabOrphans[id] = ed.parentId;
    }
  }
  r.meshes3d[(size_t)idx].movementRig.camera = CollabFindMesh(ed.rigCameraId);

  // Children that were waiting for this object.
  for (auto it = collabOrphans.begin(); it != collabOrphans.end();) {
    if (it->second != id) {
      ++it;
      continue;
    }
    if (const int child = CollabFindMesh(it->first); child >= 0) {
      r.SetMesh3DParent((size_t)child, CollabFindMesh(id), false);
      CollabRebaseline((size_t)child);
    }
    it = collabOrphans.erase(it);
  }
  if (const int now = CollabFindMesh(id); now >= 0)
    CollabRebaseline((size_t)now);
}

// ---- window -----------------------------------------------------------------

void MainWindow::RenderCollabWindow() {
  if (!collabSettingsLoaded)
    LoadCollabSettings();
  if (!Begin("Realtime Collab###RealtimeCollab", &showCollab)) {
    End();
    return;
  }
  const CollabState state = collab ? collab->State() : CollabState::Offline;
  const bool active = state == CollabState::Connecting || state == CollabState::Joining ||
                      state == CollabState::Joined;

  BeginDisabled(active);
  InputText("Server", collabUrl, sizeof collabUrl);
  InputText("Token", collabToken, sizeof collabToken, ImGuiInputTextFlags_Password);
  SetItemTooltip("ILMEEE_RT_TOKEN of the server (ask whoever runs it)");
  InputText("Room", collabRoom, sizeof collabRoom, ImGuiInputTextFlags_CharsNoBlank);
  SetItemTooltip("Everyone in the same room edits the same scene. A-Z a-z 0-9 _ - .");
  InputText("Name", collabName, sizeof collabName);
  ColorEdit3("Color", collabColor, ImGuiColorEditFlags_NoInputs);
  EndDisabled();

  const bool locked = SceneStructureLocked();
  if (!active) {
    BeginDisabled(locked || !sceneRenderer || collabRoom[0] == '\0');
    if (Button("Join room")) {
      const std::string room = SanitizeRoom(collabRoom);
      std::snprintf(collabRoom, sizeof collabRoom, "%s", room.c_str());
      StartCollab();
    }
    EndDisabled();
    if (locked)
      TextDisabled("Stop the game first.");
    TextWrapped("Joining a room that already has a scene replaces your current scene "
                "with it (save first if you want to keep it). An empty room takes yours.");
  } else if (Button("Leave room")) {
    StopCollab();
  }

  Separator();
  ImVec4 color(0.6f, 0.6f, 0.6f, 1.0f);
  if (state == CollabState::Joined)
    color = ImVec4(0.3f, 0.9f, 0.4f, 1.0f);
  else if (state == CollabState::Failed)
    color = ImVec4(1.0f, 0.4f, 0.3f, 1.0f);
  else if (active)
    color = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
  TextColored(color, "%s", ilmeee::net::ToString(state));
  if (collab && !collab->LastError().empty()) {
    SameLine();
    TextWrapped("%s", collab->LastError().c_str());
  }
  if (state == CollabState::Joined) {
    Text("Room seq %llu   sent %zu   applied %zu", (unsigned long long)collab->Seq(),
         collabOpsSent, collabOpsApplied);
    if (locked && !collabQueue.empty())
      TextDisabled("%zu remote change(s) wait for Stop", collabQueue.size());

    SeparatorText("People");
    ColorButton("##me", ImVec4(collabColor[0], collabColor[1], collabColor[2], 1.0f),
                ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));
    SameLine();
    Text("%s (you)", collabName);
    for (const auto &peer : collab->Peers()) {
      PushID((int)peer.id);
      ColorButton("##c", UnpackColor(peer.color), ImGuiColorEditFlags_NoTooltip,
                  ImVec2(12, 12));
      SameLine();
      std::string selected;
      if (peer.presence.is_object()) {
        const uint64_t sel =
            collab::ParseObjectKey(peer.presence.value("selection", std::string()));
        if (const int i = CollabFindMesh(sel); i >= 0)
          selected = sceneRenderer->GetMesh3DName((size_t)i);
      }
      if (selected.empty())
        Text("%s", peer.name.c_str());
      else
        Text("%s  -  editing %s", peer.name.c_str(), selected.c_str());
      PopID();
    }
  }
  End();
}

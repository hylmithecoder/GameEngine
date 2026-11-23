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
//
// Locks + presence: the selected object is locked for us (server FIFO queue,
// see server/realtime/src/locks.rs). Objects held by someone else are
// read-only here (inspector, gizmo, hierarchy rename/drag, delete). Presence
// carries our selection, editor camera, scene cursor and hovered panel; the
// Scene viewport draws everyone else's (DrawCollabViewportOverlay).
#include "../../../include/ui/MainWindow.hpp"
#include "../../../include/core_engine/SceneLoader.hpp"
#include "../../../include/core_engine/UserDataDir.hpp"

#include "imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>
#include <cstdlib>
#include <fstream>

using namespace ImGui;
using ilmeee::net::CollabOp;
using ilmeee::net::CollabPeer;
using ilmeee::net::CollabState;
namespace collab = ilmeee::collab;
namespace fs = std::filesystem;

namespace {

constexpr float kXformInterval = 0.05f;  // live transform rate (20 Hz)
constexpr float kFullInterval = 1.0f;    // safety full diff
constexpr float kPresenceInterval = 0.1f; // cursor/camera rate (10 Hz, only on change)
constexpr float kLockIdleYield = 30.0f;   // hand a held lock to the queue after this idle

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
  collab->onLockChanged = [this](const std::string &key) {
    if (key != collabWantLock || !collab)
      return;
    const bool mine = collab->LockHolder(key) == collab->Self();
    if (mine && !collabHeldLock) {
      collabLockIdle = 0.0f;
      if (const int i = CollabFindMesh(collab::ParseObjectKey(key)); i >= 0)
        PushMessage("[SUCCESS] Realtime collab: your turn, '" +
                    sceneRenderer->GetMesh3DName((size_t)i) + "' is editable now");
    }
    collabHeldLock = mine;
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
  collabWantLock.clear();
  collabHeldLock = false;
  collabPeerViews.clear();
  collabLastPresence.clear();
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
  // Opened from the Hub's "Online workspaces": join that room once the
  // project's scene is up (an empty room takes this scene, so wait for it).
  if (!collabAutoJoinDone && sceneRenderer && !SceneStructureLocked()) {
    const char *room = std::getenv("ILMEEE_COLLAB_ROOM");
    if (!room || !*room) {
      collabAutoJoinDone = true;
    } else if ((collabAutoJoinWait += deltaTime) >= 1.5f) {
      collabAutoJoinDone = true;
      if (!collabSettingsLoaded)
        LoadCollabSettings();
      const std::string clean = SanitizeRoom(room);
      std::snprintf(collabRoom, sizeof collabRoom, "%s", clean.c_str());
      showCollab = true;
      PushMessage("Realtime collab: joining workspace '" + clean + "' (from the Hub)");
      StartCollab();
    }
  }
  if (!collab)
    return;
  collab->Poll();
  if (!CollabJoined() || !sceneRenderer)
    return;

  UpdateCollabLock(deltaTime);
  collabPresenceTimer += deltaTime;
  if (collabPresenceTimer >= kPresenceInterval) {
    collabPresenceTimer = 0.0f;
    SendCollabPresence();
  }

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
  const bool full = collabDirty || collabFullTimer >= kFullInterval;
  if (full || collabXformTimer >= kXformInterval) {
    CollabSync(full);
    collabXformTimer = 0.0f;
    if (full) {
      collabFullTimer = 0.0f;
      collabDirty = false;
    }
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
          if (key == collabWantLock)
            collabLockIdle = 0.0f;
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
      if (key == collabWantLock)
        collabLockIdle = 0.0f;
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
      std::string selected, panel;
      bool held = false;
      float cam[6] = {};
      bool hasCam = false;
      if (peer.presence.is_object()) {
        const std::string key = peer.presence.value("selection", std::string());
        if (const int i = CollabFindMesh(collab::ParseObjectKey(key)); i >= 0) {
          selected = sceneRenderer->GetMesh3DName((size_t)i);
          held = collab->LockHolder(key) == peer.id;
        }
        panel = peer.presence.value("panel", std::string());
        if (auto c = peer.presence.find("cam"); c != peer.presence.end() && c->is_array() &&
                                                 c->size() >= 5) {
          hasCam = true;
          for (size_t n = 0; n < 5; ++n)
            cam[n] = (*c)[n].is_number() ? (*c)[n].get<float>() : 0.0f;
        }
      }
      if (selected.empty())
        Text("%s", peer.name.c_str());
      else
        Text("%s  -  %s %s", peer.name.c_str(), held ? "editing" : "waiting for",
             selected.c_str());
      if (hasCam) {
        SameLine();
        if (SmallButton("Go to view")) {
          sceneRenderer->camera3d.position = glm::vec3(cam[0], cam[1], cam[2]);
          sceneRenderer->camera3d.yaw = cam[3];
          sceneRenderer->camera3d.pitch = cam[4];
        }
        SetItemTooltip("Move your editor camera to where %s is looking from", peer.name.c_str());
      }
      if (!panel.empty())
        TextDisabled("    in %s", panel.c_str());
      PopID();
    }

    SeparatorText("Show in Scene view");
    Checkbox("Cameras", &collabShowCameras);
    SameLine();
    Checkbox("Cursors", &collabShowCursors);
    SameLine();
    Checkbox("Selections", &collabShowSelections);
    TextDisabled("Selecting an object locks it for you; others queue and see it\n"
                 "read-only until you select something else (or after %d s idle\n"
                 "while someone waits).", (int)kLockIdleYield);
  }
  End();
}

// ---- locks ------------------------------------------------------------------

std::string MainWindow::CollabKeyOf(size_t i) const {
  if (!sceneRenderer || i >= sceneRenderer->GetMesh3DCount() ||
      !sceneRenderer->meshes3d[i].objectId)
    return {};
  return collab::ObjectKey(sceneRenderer->meshes3d[i].objectId);
}

std::string MainWindow::CollabPeerName(uint32_t id) const {
  if (collab)
    if (const auto *p = collab->FindPeer(id))
      return p->name;
  return "someone";
}

bool MainWindow::CollabLockedByOther(size_t i) const {
  if (!CollabJoined())
    return false;
  const std::string key = CollabKeyOf(i);
  if (key.empty())
    return false;
  const uint32_t holder = collab->LockHolder(key);
  return holder != 0 && holder != collab->Self();
}

// Name of whoever holds `i` or one of its descendants ("" = nobody else).
std::string MainWindow::CollabSubtreeHeldBy(size_t i) const {
  if (!CollabJoined() || !sceneRenderer)
    return {};
  const SceneRenderer &r = *sceneRenderer;
  for (size_t j = 0; j < r.GetMesh3DCount(); ++j) {
    bool inside = false;
    for (int k = (int)j, guard = 0; k >= 0 && guard < 4096; k = r.GetMesh3DParent((size_t)k), ++guard)
      if ((size_t)k == i) {
        inside = true;
        break;
      }
    if (inside && CollabLockedByOther(j))
      return CollabPeerName(collab->LockHolder(CollabKeyOf(j)));
  }
  return {};
}

// Selection == the lock we want. Level-triggered: whenever we are neither the
// holder nor in the queue (first select, lock event lost, reconnect) ask again.
void MainWindow::UpdateCollabLock(float dt) {
  const int sel = SelectedMeshIndex();
  const std::string want = sel >= 0 ? CollabKeyOf((size_t)sel) : std::string();
  if (want != collabWantLock) {
    if (!collabWantLock.empty())
      collab->ReleaseLock(collabWantLock);
    collabWantLock = want;
    collabHeldLock = false;
    collabLockIdle = 0.0f;
    collabLockAsk = 1e9f; // ask right away
  }
  if (want.empty())
    return;
  const bool mine = collab->LockHolder(want) == collab->Self();
  collabLockAsk += dt;
  if (!mine && collab->QueuePosition(want) == 0 && collabLockAsk >= 1.0f) {
    collab->AcquireLock(want);
    collabLockAsk = 0.0f;
  }
  if (!mine) {
    collabHeldLock = false;
    return;
  }
  collabHeldLock = true;
  // Holding an object nobody waits for costs nothing; holding one others
  // queue for without touching it blocks them, so give it up after a while.
  const auto &locks = collab->Locks();
  const auto it = locks.find(want);
  if (it == locks.end() || it->second.queue.empty()) {
    collabLockIdle = 0.0f;
    return;
  }
  collabLockIdle += dt;
  if (collabLockIdle >= kLockIdleYield) {
    PushMessage("[WARNING] Realtime collab: handed '" + std::string(objectName) + "' to " +
                CollabPeerName(it->second.queue.front()) + " (idle " +
                std::to_string((int)kLockIdleYield) + " s); you are back in the queue");
    CollabYieldLock();
  }
}

// Give the lock to the next in line and queue up behind them.
void MainWindow::CollabYieldLock() {
  if (!CollabJoined() || collabWantLock.empty())
    return;
  collab->ReleaseLock(collabWantLock);
  collab->AcquireLock(collabWantLock); // processed in order: lands at the back
  collabHeldLock = false;
  collabLockIdle = 0.0f;
  collabLockAsk = 0.0f;
}

// ---- presence ---------------------------------------------------------------

namespace {
float Round2(float v) { return std::round(v * 100.0f) / 100.0f; }

// "<icon> Inspector###Inspector" -> "Inspector"
std::string PanelName(const ImGuiWindow *w) {
  if (!w)
    return {};
  while (w->ParentWindow && (w->Flags & ImGuiWindowFlags_ChildWindow))
    w = w->ParentWindow;
  std::string name = w->Name;
  if (const auto hash = name.find("###"); hash != std::string::npos)
    name = name.substr(hash + 3);
  else if (const auto hash2 = name.find("##"); hash2 != std::string::npos)
    name = name.substr(0, hash2);
  std::string out;
  for (unsigned char c : name) // drop icon-font glyphs
    if (c < 0x80)
      out += (char)c;
  while (!out.empty() && out.front() == ' ')
    out.erase(out.begin());
  if (out.size() > 40)
    out.resize(40);
  return out;
}
} // namespace

void MainWindow::SendCollabPresence() {
  const int sel = SelectedMeshIndex();
  const auto &cam = sceneRenderer->camera3d;
  ilmeee::net::json presence = {
      {"selection", sel >= 0 ? CollabKeyOf((size_t)sel) : std::string()},
      {"selectionName", sel >= 0 ? std::string(objectName) : std::string()},
      {"cam",
       {Round2(cam.position.x), Round2(cam.position.y), Round2(cam.position.z),
        Round2(cam.yaw), Round2(cam.pitch), Round2(cam.fovDeg)}},
      {"panel", PanelName(GImGui ? GImGui->HoveredWindow : nullptr)},
  };
  if (collabCursorValid)
    presence["cursor"] = {Round2(collabCursor.x), Round2(collabCursor.y),
                          Round2(collabCursor.z)};
  else
    presence["cursor"] = nullptr;
  std::string dumped = presence.dump();
  if (dumped != collabLastPresence && collab->SetPresence(presence))
    collabLastPresence = std::move(dumped);
}

// ---- inspector / hierarchy --------------------------------------------------

bool MainWindow::RenderCollabInspectorBanner(int meshIndex) {
  if (meshIndex < 0 || !CollabJoined())
    return false;
  const std::string key = CollabKeyOf((size_t)meshIndex);
  const auto &locks = collab->Locks();
  const auto it = locks.find(key);
  if (it == locks.end())
    return false;
  const auto &lock = it->second;
  const uint32_t self = collab->Self();

  if (lock.holder == self) {
    if (lock.queue.empty())
      return false;
    std::string waiting;
    for (uint32_t id : lock.queue)
      waiting += (waiting.empty() ? "" : ", ") + CollabPeerName(id);
    TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%zu waiting: %s", lock.queue.size(),
                waiting.c_str());
    SameLine();
    if (SmallButton("Hand over"))
      CollabYieldLock();
    SetItemTooltip("Let the next person edit this object; you queue behind them.\n"
                   "Happens on its own after %d s without changes.",
                   (int)kLockIdleYield);
    Separator();
    return false;
  }

  const CollabPeer *holder = collab->FindPeer(lock.holder);
  const ImVec4 color = holder ? UnpackColor(holder->color) : ImVec4(1, 0.5f, 0.3f, 1);
  ColorButton("##holder", color, ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));
  SameLine();
  TextColored(color, "Locked: %s is editing this", CollabPeerName(lock.holder).c_str());
  if (const size_t pos = collab->QueuePosition(key))
    TextDisabled("You are #%zu in the queue - it unlocks when it's your turn.", pos);
  else
    TextDisabled("Joining the queue...");
  Separator();
  return true;
}

void MainWindow::DrawCollabHierarchyTag(size_t i) {
  if (!CollabJoined())
    return;
  const std::string key = CollabKeyOf(i);
  const uint32_t holder = collab->LockHolder(key);
  if (holder == 0 || holder == collab->Self())
    return;
  const CollabPeer *p = collab->FindPeer(holder);
  const std::string tag = (p ? p->name : std::string("someone"));
  const ImU32 col = ColorConvertFloat4ToU32(p ? UnpackColor(p->color) : ImVec4(1, 0.5f, 0.3f, 1));
  const ImVec2 mn = GetItemRectMin(), mx = GetItemRectMax();
  const ImVec2 ts = CalcTextSize(tag.c_str());
  const float right = GetWindowPos().x + GetWindowContentRegionMax().x;
  const ImVec2 at(std::max(mn.x, right - ts.x - 6.0f), mn.y + (mx.y - mn.y - ts.y) * 0.5f);
  ImDrawList *dl = GetWindowDrawList();
  dl->AddRectFilled(ImVec2(at.x - 12, at.y - 1), ImVec2(at.x + ts.x + 4, at.y + ts.y + 1),
                    IM_COL32(20, 20, 24, 220), 4.0f);
  dl->AddCircleFilled(ImVec2(at.x - 6, at.y + ts.y * 0.5f), 3.5f, col);
  dl->AddText(at, col, tag.c_str());
}

// ---- scene viewport overlay -------------------------------------------------

namespace {
struct OverlayView {
  glm::mat4 viewProj;
  ImVec2 min, size;
  bool Project(const glm::vec3 &p, ImVec2 &out) const {
    const glm::vec4 c = viewProj * glm::vec4(p, 1.0f);
    if (c.w <= 1e-4f)
      return false;
    out.x = min.x + (c.x / c.w * 0.5f + 0.5f) * size.x;
    out.y = min.y + (0.5f - c.y / c.w * 0.5f) * size.y; // image is V-flipped
    return true;
  }
  void Line(ImDrawList *dl, const glm::vec3 &a, const glm::vec3 &b, ImU32 col, float t) const {
    ImVec2 pa, pb;
    if (Project(a, pa) && Project(b, pb))
      dl->AddLine(pa, pb, col, t);
  }
};

glm::vec3 YawPitchForward(float yawDeg, float pitchDeg) {
  const float y = glm::radians(yawDeg), p = glm::radians(pitchDeg);
  return glm::normalize(glm::vec3(std::cos(y) * std::cos(p), std::sin(p), std::sin(y) * std::cos(p)));
}

void NameTag(ImDrawList *dl, ImVec2 at, ImU32 col, const std::string &text) {
  const ImVec2 ts = CalcTextSize(text.c_str());
  dl->AddRectFilled(at, ImVec2(at.x + ts.x + 8, at.y + ts.y + 4), col, 4.0f);
  dl->AddText(ImVec2(at.x + 4, at.y + 2), IM_COL32(15, 15, 18, 255), text.c_str());
}

bool ReadVec(const ilmeee::net::json &j, size_t n, float *out) {
  if (!j.is_array() || j.size() < n)
    return false;
  for (size_t k = 0; k < n; ++k) {
    if (!j[k].is_number())
      return false;
    out[k] = j[k].get<float>();
  }
  return true;
}

// Nearest hit of the ray with any mesh's oriented bounding box.
bool RayHitBoxes(const SceneRenderer &r, const glm::vec3 &ro, const glm::vec3 &rd, float &bestT) {
  bool hit = false;
  for (size_t i = 0; i < r.GetMesh3DCount(); ++i) {
    if (r.IsMesh3DLight(i) || r.IsMesh3DCamera(i))
      continue;
    glm::vec3 bmin, bmax;
    if (!r.GetMesh3DLocalBounds(i, bmin, bmax))
      continue;
    const glm::mat4 inv = glm::inverse(r.GetMesh3DWorldModel(i));
    const glm::vec3 o = glm::vec3(inv * glm::vec4(ro, 1.0f));
    const glm::vec3 d = glm::vec3(inv * glm::vec4(rd, 0.0f));
    float t0 = 0.0f, t1 = 1e30f;
    bool ok = true;
    for (int a = 0; a < 3 && ok; ++a) {
      if (std::fabs(d[a]) < 1e-8f) {
        ok = o[a] >= bmin[a] && o[a] <= bmax[a];
        continue;
      }
      float ta = (bmin[a] - o[a]) / d[a], tb = (bmax[a] - o[a]) / d[a];
      if (ta > tb)
        std::swap(ta, tb);
      t0 = std::max(t0, ta);
      t1 = std::min(t1, tb);
      ok = t0 <= t1;
    }
    // t0 is measured in the box's (scaled) space: map the hit back to world.
    if (ok && t0 > 0.0f) {
      const glm::vec3 local = o + d * t0;
      const glm::vec3 world = glm::vec3(r.GetMesh3DWorldModel(i) * glm::vec4(local, 1.0f));
      const float t = glm::dot(world - ro, rd);
      if (t > 0.0f && t < bestT) {
        bestT = t;
        hit = true;
      }
    }
  }
  return hit;
}
} // namespace

void MainWindow::DrawCollabViewportOverlay(ImVec2 vpMin, ImVec2 vpSize, bool hovered) {
  if (!sceneRenderer || !CollabJoined()) {
    collabCursorValid = false;
    return;
  }
  SceneRenderer &r = *sceneRenderer;

  // Our own cursor, as a world point other editors can draw.
  collabCursorValid = false;
  if (hovered) {
    const ImVec2 m = GetIO().MousePos;
    glm::vec3 ro, rd;
    if (r.ScreenToRay(m.x - vpMin.x, m.y - vpMin.y, ro, rd)) {
      float t = 1e30f;
      RayHitBoxes(r, ro, rd, t);
      if (std::fabs(rd.y) > 1e-5f) { // ground plane y = 0
        const float tg = -ro.y / rd.y;
        if (tg > 0.0f && tg < t)
          t = tg;
      }
      if (t > 1e29f)
        t = 5.0f; // looking at the sky: a point ahead of the camera
      collabCursor = ro + rd * std::min(t, 500.0f);
      collabCursorValid = true;
    }
  }

  const OverlayView v{r.GetCamera3DProj() * r.GetCamera3DView(), vpMin, vpSize};
  ImDrawList *dl = GetWindowDrawList();
  dl->PushClipRect(vpMin, ImVec2(vpMin.x + vpSize.x, vpMin.y + vpSize.y), true);
  const float k = 1.0f - std::exp(-GetIO().DeltaTime * 14.0f); // smoothing

  for (auto it = collabPeerViews.begin(); it != collabPeerViews.end();)
    it = collab->FindPeer(it->first) ? std::next(it) : collabPeerViews.erase(it);

  for (const auto &peer : collab->Peers()) {
    if (!peer.presence.is_object())
      continue;
    const ImVec4 c4 = UnpackColor(peer.color);
    const ImU32 col = ColorConvertFloat4ToU32(c4);
    const ImU32 colSoft = ColorConvertFloat4ToU32(ImVec4(c4.x, c4.y, c4.z, 0.35f));
    auto &view = collabPeerViews[peer.id];

    // targets from presence, smoothed so 10 Hz updates move fluidly
    float cam[6], cur[3];
    const bool hasCam = ReadVec(peer.presence.value("cam", ilmeee::net::json()), 6, cam);
    const bool hasCur = ReadVec(peer.presence.value("cursor", ilmeee::net::json()), 3, cur);
    if (hasCam) {
      const glm::vec3 p(cam[0], cam[1], cam[2]);
      if (!view.primed || !view.hasCam) {
        view.camPos = p;
        view.yaw = cam[3];
        view.pitch = cam[4];
      }
      view.camPos += (p - view.camPos) * k;
      float dy = std::fmod(cam[3] - view.yaw + 540.0f, 360.0f) - 180.0f;
      view.yaw += dy * k;
      view.pitch += (cam[4] - view.pitch) * k;
    }
    if (hasCur) {
      const glm::vec3 p(cur[0], cur[1], cur[2]);
      if (!view.hasCursor)
        view.cursor = p;
      view.cursor += (p - view.cursor) * k;
    }
    view.hasCam = hasCam;
    view.hasCursor = hasCur;
    view.primed = true;

    // their selection: box around the object
    if (collabShowSelections) {
      const uint64_t selId =
          collab::ParseObjectKey(peer.presence.value("selection", std::string()));
      if (const int si = CollabFindMesh(selId); si >= 0) {
        glm::vec3 bmin, bmax;
        const glm::mat4 model = r.GetMesh3DWorldModel((size_t)si);
        if (r.GetMesh3DLocalBounds((size_t)si, bmin, bmax)) {
          glm::vec3 c[8];
          for (int n = 0; n < 8; ++n)
            c[n] = glm::vec3(model * glm::vec4(n & 1 ? bmax.x : bmin.x, n & 2 ? bmax.y : bmin.y,
                                               n & 4 ? bmax.z : bmin.z, 1.0f));
          static const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                           {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
          for (const auto &e : edges)
            v.Line(dl, c[e[0]], c[e[1]], col, 2.0f);
          ImVec2 top(1e9f, 1e9f);
          for (const auto &p : c)
            if (ImVec2 s; v.Project(p, s) && s.y < top.y)
              top = s;
          if (top.y < 1e8f) {
            const bool held = collab->LockHolder(collab::ObjectKey(selId)) == peer.id;
            NameTag(dl, ImVec2(top.x - 4, top.y - 22), col,
                    peer.name + (held ? " (editing)" : " (waiting)"));
          }
        } else if (ImVec2 s; v.Project(r.GetMesh3DWorldPosition((size_t)si), s)) {
          dl->AddCircle(s, 14.0f, col, 0, 2.0f);
          NameTag(dl, ImVec2(s.x + 16, s.y - 10), col, peer.name);
        }
      }
    }

    // their editor camera: a small frustum where they are looking from
    if (collabShowCameras && view.hasCam &&
        glm::length(view.camPos - r.camera3d.position) > 0.3f) {
      const glm::vec3 f = YawPitchForward(view.yaw, view.pitch);
      glm::vec3 right = glm::cross(f, glm::vec3(0, 1, 0));
      right = glm::length(right) < 1e-4f ? glm::vec3(1, 0, 0) : glm::normalize(right);
      const glm::vec3 up = glm::cross(right, f);
      const float depth = 0.6f;
      const float hh = std::tan(glm::radians(std::clamp(cam[5], 10.0f, 120.0f)) * 0.5f) * depth;
      const float hw = hh * 16.0f / 9.0f;
      const glm::vec3 o = view.camPos, m = o + f * depth;
      const glm::vec3 q[4] = {m + right * hw + up * hh, m - right * hw + up * hh,
                              m - right * hw - up * hh, m + right * hw - up * hh};
      for (int n = 0; n < 4; ++n) {
        v.Line(dl, o, q[n], col, 1.5f);
        v.Line(dl, q[n], q[(n + 1) % 4], col, 1.5f);
      }
      v.Line(dl, m + up * hh, m + up * hh * 1.5f, col, 2.0f); // "up" tick
      if (ImVec2 s; v.Project(o, s)) {
        dl->AddCircleFilled(s, 4.0f, col);
        NameTag(dl, ImVec2(s.x + 6, s.y + 4), col, peer.name + " (camera)");
      }
    }

    // their cursor in the scene
    if (collabShowCursors && view.hasCursor) {
      if (ImVec2 s; v.Project(view.cursor, s)) {
        dl->AddCircleFilled(s, 10.0f, colSoft);
        const ImVec2 tip = s;
        dl->AddTriangleFilled(tip, ImVec2(tip.x + 4, tip.y + 15), ImVec2(tip.x + 11, tip.y + 10), col);
        dl->AddTriangle(tip, ImVec2(tip.x + 4, tip.y + 15), ImVec2(tip.x + 11, tip.y + 10),
                        IM_COL32(15, 15, 18, 255), 1.0f);
        NameTag(dl, ImVec2(tip.x + 12, tip.y + 12), col, peer.name);
      }
    }
  }
  dl->PopClipRect();
}

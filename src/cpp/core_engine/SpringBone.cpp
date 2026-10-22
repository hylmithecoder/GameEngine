#include "core_engine/SpringBone.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <initializer_list>

namespace ilmeee {
namespace {
constexpr int kMaxSubsteps = 4;

bool Finite(const glm::vec3 &v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool Finite(const glm::mat4 &m) {
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      if (!std::isfinite(m[c][r])) return false;
  return true;
}
bool Finite(const SpringBoneChain &c) {
  return std::isfinite(c.stiffness) && std::isfinite(c.drag) &&
      std::isfinite(c.gravity) && std::isfinite(c.hitRadius);
}

glm::quat FromTo(const glm::vec3 &from, const glm::vec3 &to) {
  const float d = glm::dot(from, to);
  if (d < -0.9999f) {
    glm::vec3 axis = glm::cross(glm::vec3(1, 0, 0), from);
    if (glm::dot(axis, axis) < 1e-6f) axis = glm::cross(glm::vec3(0, 1, 0), from);
    return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
  }
  const glm::vec3 c = glm::cross(from, to);
  return glm::normalize(glm::quat(1 + d, c.x, c.y, c.z));
}

glm::vec3 ClosestOnSegment(const glm::vec3 &p, const glm::vec3 &a, const glm::vec3 &b) {
  const glm::vec3 ab = b - a;
  const float len2 = glm::dot(ab, ab);
  if (len2 < 1e-12f) return a;
  return a + ab * std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f);
}

float AverageScale(const glm::mat4 &m) {
  return (glm::length(glm::vec3(m[0])) + glm::length(glm::vec3(m[1])) +
          glm::length(glm::vec3(m[2]))) / 3.0f;
}

int FindBone(const std::vector<SkeletonBone> &bones, const std::string &name) {
  if (name.empty()) return -1;
  for (size_t i = 0; i < bones.size(); ++i)
    if (bones[i].name == name) return int(i);
  return -1;
}

std::string Signature(const std::vector<SkeletonBone> &bones,
                      const SpringBoneComponent &component) {
  std::string s = std::to_string(bones.size());
  if (!bones.empty()) s += ':' + bones.front().name + ':' + bones.back().name;
  for (const auto &chain : component.chains) s += '|' + chain.rootBone;
  return s;
}

// Builds the bone order and the joint list. Returns false for bad hierarchies
// or when no chain bone resolves.
bool Setup(const std::vector<SkeletonBone> &bones,
           const SpringBoneComponent &component, SpringBoneRuntime &rt) {
  const std::string signature = Signature(bones, component);
  if (rt.signature == signature) return !rt.joints.empty();
  rt.Reset();
  rt.signature = signature;
  const int n = int(bones.size());
  std::vector<int> depth(n, 0);
  for (int i = 0; i < n; ++i) {
    int p = bones[i].parent, guard = n;
    while (p >= 0) {
      if (p >= n || guard-- <= 0) return false;
      ++depth[i]; p = bones[p].parent;
    }
  }
  rt.order.resize(n);
  for (int i = 0; i < n; ++i) rt.order[i] = i;
  std::stable_sort(rt.order.begin(), rt.order.end(),
                   [&](int a, int b) { return depth[a] < depth[b]; });

  // Each bone belongs to the first chain whose root is it or an ancestor.
  std::vector<int> chainOf(n, -1);
  for (size_t c = 0; c < component.chains.size(); ++c) {
    const int root = FindBone(bones, component.chains[c].rootBone);
    if (root >= 0 && chainOf[root] < 0) chainOf[root] = int(c);
  }
  for (int i : rt.order) {
    const int p = bones[i].parent;
    if (chainOf[i] < 0 && p >= 0) chainOf[i] = chainOf[p];
  }

  rt.jointOfBone.assign(n, -1);
  for (int i : rt.order) {
    if (chainOf[i] < 0) continue;
    glm::vec3 tail(0.0f);
    bool found = false;
    for (int c = 0; c < n && !found; ++c)
      if (bones[c].parent == i) { tail = bones[c].bindPosition - bones[i].bindPosition; found = true; }
    // Leaf: extend half the incoming bone, the usual virtual tip.
    if (!found && bones[i].parent >= 0)
      tail = (bones[i].bindPosition - bones[bones[i].parent].bindPosition) * 0.5f;
    if (!Finite(tail) || glm::length(tail) < 1e-5f) continue;
    rt.jointOfBone[i] = int(rt.joints.size());
    SpringBoneRuntime::Joint joint;
    joint.bone = i; joint.chain = chainOf[i]; joint.tailLocal = tail;
    rt.joints.push_back(joint);
  }
  return !rt.joints.empty();
}

struct WorldCollider { glm::vec3 a, b; float radius; };

// One pass over the skeleton. integrate == false only re-applies the stored
// tails (no time passes); the first pass after a reset rests every tail.
bool Solve(const std::vector<SkeletonBone> &bones, const SpringBoneComponent &component,
           SpringBoneRuntime &rt, const glm::mat4 &model, float dt, bool integrate,
           const std::vector<glm::mat4> &pose, std::vector<glm::mat4> &out) {
  if (std::abs(glm::determinant(glm::mat3(model))) < 1e-12f) return false;
  const glm::mat4 toModel = glm::inverse(model);
  const float scale = AverageScale(model);
  std::vector<WorldCollider> colliders;
  for (const auto &c : component.colliders) {
    const int bone = FindBone(bones, c.bone);
    if (bone < 0 || !Finite(c.offset) || !Finite(c.tailOffset) ||
        !std::isfinite(c.radius) || c.radius <= 0) continue;
    const glm::mat4 frame = model * pose[bone];
    colliders.push_back({glm::vec3(frame * glm::vec4(c.offset, 1)),
                         glm::vec3(frame * glm::vec4(c.tailOffset, 1)), c.radius * scale});
  }
  const glm::vec3 down(0, -1, 0);
  out = pose;
  std::vector<uint8_t> moved(bones.size(), 0);
  for (int b : rt.order) {
    const int p = bones[b].parent;
    if (p >= 0 && moved[p]) {
      out[b] = out[p] * glm::inverse(pose[p]) * pose[b];
      moved[b] = 1;
    }
    const int j = rt.jointOfBone[b];
    if (j < 0) continue;
    auto &joint = rt.joints[j];
    const auto &chain = component.chains[joint.chain];
    if (!Finite(chain)) continue;
    const glm::vec3 headModel(out[b][3]);
    const glm::vec3 tailModel(out[b] * glm::vec4(joint.tailLocal, 1));
    const glm::vec3 head(model * glm::vec4(headModel, 1));
    const glm::vec3 restTail(model * glm::vec4(tailModel, 1));
    const float length = glm::length(restTail - head);
    if (length < 1e-6f) continue;
    if (!rt.initialized) joint.previous = joint.current = restTail;

    glm::vec3 next = joint.current;
    if (integrate) {
      const float drag = std::clamp(chain.drag, 0.0f, 1.0f);
      const float pull = 1.0f - std::exp(-std::max(0.0f, chain.stiffness) * dt);
      next += (joint.current - joint.previous) * (1.0f - drag);
      next += (restTail - next) * pull;
      next += (down * chain.gravity + component.wind) * length * dt;
    }
    glm::vec3 dir = next - head;
    next = head + (glm::length(dir) > 1e-6f ? glm::normalize(dir) : (restTail - head) / length) * length;
    if (chain.useColliders) {
      const float hit = std::max(0.0f, chain.hitRadius) * scale;
      // Restoring the bone length can pull the tail back in; a few passes
      // settle it on the collider surface.
      for (int pass = 0; pass < 4; ++pass) {
        for (const auto &c : colliders) {
          // The animated pose itself sits inside (hair roots in a rough head
          // sphere): honour the model's shape instead of puffing it out.
          if (glm::length(restTail - ClosestOnSegment(restTail, c.a, c.b)) < c.radius + hit)
            continue;
          const glm::vec3 closest = ClosestOnSegment(next, c.a, c.b);
          const glm::vec3 away = next - closest;
          const float distance = glm::length(away), minimum = c.radius + hit;
          if (distance >= minimum) continue;
          next = closest + (distance > 1e-6f ? away / distance : (restTail - head) / length) * minimum;
          dir = next - head;
          if (glm::length(dir) > 1e-6f) next = head + glm::normalize(dir) * length;
        }
      }
    }
    if (!Finite(next)) return false;
    if (integrate) { joint.previous = joint.current; joint.current = next; }

    const glm::vec3 nextModel(toModel * glm::vec4(next, 1));
    const glm::vec3 from = tailModel - headModel, to = nextModel - headModel;
    if (glm::length(from) < 1e-6f || glm::length(to) < 1e-6f) continue;
    const glm::quat turn = FromTo(glm::normalize(from), glm::normalize(to));
    out[b] = glm::translate(glm::mat4(1), headModel) * glm::mat4_cast(turn) *
        glm::translate(glm::mat4(1), -headModel) * out[b];
    moved[b] = 1;
  }
  rt.initialized = true;
  return true;
}

std::string Lower(const std::string &s) {
  std::string r(s);
  for (auto &c : r) c = char(std::tolower(static_cast<unsigned char>(c)));
  return r;
}
bool Contains(const std::string &s, std::initializer_list<const char *> parts) {
  for (const auto *p : parts) if (s.find(p) != std::string::npos) return true;
  return false;
}

enum class Kind { None, Hair, Breast, Skirt, Accessory };
// Chinese rigs abbreviate pinyin: Fa_A_1 (发 hair), L_HF_3 (后发), L_X_2 (胸),
// qun_0_0 (裙), L_HDJ_1 (蝴蝶结 bow), L_FD_A_1 (发带 band).
Kind ClassifyPinyin(const std::string &low) {
  size_t start = 0;
  while (start <= low.size()) {
    size_t end = low.find_first_of("_- .", start);
    if (end == std::string::npos) end = low.size();
    std::string token = low.substr(start, end - start);
    while (!token.empty() && (std::isdigit(static_cast<unsigned char>(token.back())) || token.back() == '+'))
      token.pop_back();
    if (token == "fa" || token == "hf" || token == "qf" || token == "toufa") return Kind::Hair;
    if (token == "x" || token == "xiong") return Kind::Breast;
    if (token == "qun" || token == "qunzi") return Kind::Skirt;
    if (token == "hdj" || token == "fd" || token == "pd") return Kind::Accessory;
    start = end + 1;
  }
  return Kind::None;
}
Kind ClassifyName(const std::string &name) {
  const std::string low = Lower(name);
  if ((Contains(name, {"髪", "アホ毛", "もみあげ", "ツインテ", "ポニテ"}) &&
       !Contains(name, {"飾"})) || Contains(low, {"hair", "ahoge", "ponytail", "twintail"}))
    return Kind::Hair;
  if (Contains(name, {"胸", "おっぱい"}) || Contains(low, {"breast", "bust", "oppai", "boob"}))
    return Kind::Breast;
  if (Contains(name, {"スカート", "裾"}) || Contains(low, {"skirt"})) return Kind::Skirt;
  if (Contains(name, {"リボン", "尻尾", "しっぽ", "ネクタイ"}) ||
      Contains(low, {"ribbon", "necktie"}) || low.rfind("tail", 0) == 0)
    return Kind::Accessory;
  return ClassifyPinyin(low);
}
Kind Classify(const SkeletonBone &bone) {
  const Kind kind = ClassifyName(bone.name);
  return kind != Kind::None || bone.altName.empty() ? kind : ClassifyName(bone.altName);
}

int FindAny(const std::vector<SkeletonBone> &bones, std::initializer_list<const char *> names) {
  for (const auto *name : names)
    if (const int i = FindBone(bones, name); i >= 0) return i;
  return -1;
}
} // namespace

bool ApplySpringBones(const std::vector<SkeletonBone> &bones,
                      const SpringBoneComponent &component,
                      SpringBoneRuntime &rt, const glm::mat4 &model,
                      float deltaSeconds, std::vector<glm::mat4> &world) {
  if (!component.enabled || world.size() != bones.size() || !Finite(model) ||
      !Finite(component.wind) || !std::isfinite(deltaSeconds) || deltaSeconds < 0)
    return false;
  if (!Setup(bones, component, rt)) return false;

  // A teleport (respawn, scene jump) would fling every chain; rest instead.
  if (rt.initialized) {
    float reach = 0;
    for (const auto &joint : rt.joints) reach += glm::length(joint.tailLocal);
    const float moved = glm::length(glm::vec3(model[3]) - glm::vec3(rt.previousModel[3]));
    if (moved > std::max(reach * AverageScale(model) * 4.0f, 1e-4f)) {
      rt.initialized = false; rt.accumulator = 0;
    }
  }
  if (!rt.initialized) rt.previousModel = model;

  rt.accumulator += deltaSeconds;
  int steps = 0;
  while (rt.accumulator >= kSpringBoneStep && steps < kMaxSubsteps) {
    rt.accumulator -= kSpringBoneStep; ++steps;
  }
  // Long hitches drop time rather than spiral; the next frame catches up.
  if (rt.accumulator >= kSpringBoneStep) rt.accumulator = 0;

  std::vector<glm::mat4> out;
  bool ok = true;
  for (int k = 1; k <= steps && ok; ++k) {
    // Spread the object's motion over the substeps.
    const float t = float(k) / float(steps);
    glm::mat4 sub;
    for (int c = 0; c < 4; ++c) sub[c] = glm::mix(rt.previousModel[c], model[c], t);
    ok = Solve(bones, component, rt, sub, kSpringBoneStep, true, world, out);
  }
  if (ok && steps == 0) ok = Solve(bones, component, rt, model, 0, false, world, out);
  rt.previousModel = model;
  if (!ok) { rt.initialized = false; return false; }
  world = std::move(out);
  return true;
}

const char *SpringBoneKind(const SkeletonBone &bone) {
  switch (Classify(bone)) {
  case Kind::Hair: return "hair";
  case Kind::Breast: return "breast";
  case Kind::Skirt: return "skirt";
  case Kind::Accessory: return "accessory";
  default: return "";
  }
}

size_t CountSpringJoints(const std::vector<SkeletonBone> &bones,
                         const SpringBoneComponent &component) {
  SpringBoneRuntime rt;
  return Setup(bones, component, rt) ? rt.joints.size() : 0;
}

SpringBoneComponent AutoSetupSpringBones(const std::vector<SkeletonBone> &bones) {
  SpringBoneComponent result;
  float minY = 0, maxY = 0;
  for (size_t i = 0; i < bones.size(); ++i) {
    const float y = bones[i].bindPosition.y;
    if (!std::isfinite(y)) continue;
    minY = i ? std::min(minY, y) : y; maxY = i ? std::max(maxY, y) : y;
  }
  const float height = maxY - minY > 1e-4f ? maxY - minY : 1.0f;

  std::vector<std::vector<int>> children(bones.size());
  for (size_t i = 0; i < bones.size(); ++i)
    if (bones[i].parent >= 0 && bones[i].parent < int(bones.size()))
      children[bones[i].parent].push_back(int(i));
  // A hub that forks into several strands of its own kind (おっぱい調整 over
  // both breasts, a forked hair root) would swing them as one rigid block,
  // so each strand gets its own chain. Other children (weight/helper bones)
  // don't count. A bone without children has no tip to follow.
  std::vector<std::pair<int, Kind>> roots;
  const auto strand = [&](int i, Kind kind) {
    return !children[i].empty() && Classify(bones[i]) == kind;
  };
  std::function<void(int, Kind)> addRoot = [&](int i, Kind kind) {
    if (children[i].empty()) return;
    int strands = 0;
    for (int child : children[i]) strands += strand(child, kind);
    if (strands < 2) { roots.push_back({i, kind}); return; }
    for (int child : children[i])
      if (strand(child, kind)) addRoot(child, kind);
  };
  for (size_t i = 0; i < bones.size(); ++i) {
    const Kind kind = Classify(bones[i]);
    if (kind == Kind::None) continue;
    const int p = bones[i].parent;
    if (p >= 0 && p < int(bones.size()) && Classify(bones[p]) == kind) continue;
    addRoot(int(i), kind);
  }
  for (const auto &[root, kind] : roots) {
    SpringBoneChain chain;
    chain.rootBone = bones[root].name;
    switch (kind) {
    case Kind::Hair: chain.stiffness = 6; chain.drag = 0.35f; chain.gravity = 1.0f; break;
    case Kind::Breast:
      // Springy and lightly damped: a quick bounce that settles in ~0.5 s.
      // Body colliders sit inside the chest, so breasts skip them.
      chain.stiffness = 25; chain.drag = 0.15f; chain.gravity = 0.5f;
      chain.useColliders = false; break;
    case Kind::Skirt:
      chain.stiffness = 10; chain.drag = 0.4f; chain.gravity = 1.0f;
      chain.hitRadius = height * 0.01f; break;
    default: chain.stiffness = 5; chain.drag = 0.3f; chain.gravity = 1.5f; break;
    }
    result.chains.push_back(chain);
  }
  if (result.chains.empty()) return result;
  result.enabled = true;

  if (const int head = FindAny(bones, {"頭", "Head", "head", "J_Bip_C_Head"}); head >= 0) {
    SpringCollider c;
    c.bone = bones[head].name;
    c.offset = c.tailOffset = {0, height * 0.055f, 0};
    c.radius = height * 0.055f;
    result.colliders.push_back(c);
  }
  if (const int chest = FindAny(bones, {"上半身2", "上半身", "UpperChest", "Chest", "chest",
                                        "J_Bip_C_Chest", "Spine", "spine"}); chest >= 0) {
    SpringCollider c;
    c.bone = bones[chest].name;
    const int neck = FindAny(bones, {"首", "Neck", "neck", "J_Bip_C_Neck"});
    c.tailOffset = neck >= 0 ? bones[neck].bindPosition - bones[chest].bindPosition
                             : glm::vec3(0, height * 0.12f, 0);
    c.radius = height * 0.06f;
    result.colliders.push_back(c);
  }
  const std::pair<const char *, const char *> legs[] = {
      {"左足", "左ひざ"}, {"右足", "右ひざ"},
      {"LeftUpLeg", "LeftLeg"}, {"RightUpLeg", "RightLeg"},
      {"LeftUpperLeg", "LeftLowerLeg"}, {"RightUpperLeg", "RightLowerLeg"},
      {"J_Bip_L_UpperLeg", "J_Bip_L_LowerLeg"}, {"J_Bip_R_UpperLeg", "J_Bip_R_LowerLeg"}};
  for (const auto &[thighName, kneeName] : legs) {
    const int thigh = FindBone(bones, thighName), knee = FindBone(bones, kneeName);
    if (thigh < 0 || knee < 0) continue;
    SpringCollider c;
    c.bone = bones[thigh].name;
    c.tailOffset = bones[knee].bindPosition - bones[thigh].bindPosition;
    c.radius = height * 0.055f;
    result.colliders.push_back(c);
  }
  return result;
}

} // namespace ilmeee

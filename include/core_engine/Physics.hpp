#pragma once
// Collision physics: Physics Body + Collider components and a small
// in-house solver (no external engine, so it builds unchanged for Android).
//
// Object roles, decided per fixed step from the components:
//   Collider, no Physics Body          -> Static (walls, floors, props)
//   Physics Body + Is Kinematic         -> Kinematic (moved by scripts,
//                                          pushes others, never pushed)
//   Movement Script + Collider          -> Character (the script moves it,
//                                          physics only stops it at solids
//                                          and reports ground under it)
//   Physics Body (not kinematic)        -> Dynamic (gravity, drag, bounce,
//                                          friction, pushed by everything)
// A trigger collider never blocks; it only reports overlaps.
//
// Bodies do not rotate from collisions (no angular dynamics yet); shapes
// follow the object's rotation and scale. Roadmap: Jolt replaces the
// solver behind PhysicsSystem without changing these components.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <unordered_map>
#include <utility>
#include <vector>

class SceneRenderer;

namespace ilmeee {

class MovementSystem;

enum class ColliderShape : uint32_t { Box = 0, Sphere = 1, Capsule = 2 };
inline constexpr const char *kColliderShapeNames = "Box\0Sphere\0Capsule\0";

// Saved form of the Physics Body fields that SceneRenderer::Mesh3D keeps
// loose (hasPhysics, useGravity, ...) — scene format v1.18.
struct RigidBodyComponent {
  bool enabled = false;
  bool useGravity = true;
  bool isKinematic = false;
  float mass = 1.0f;
  float drag = 0.0f;
  float gravityY = -9.81f;
};

// Collider in the object's own space: it follows position, rotation and
// scale (non-uniform scale stretches a box; spheres/capsules take the
// largest relevant axis so they stay round).
struct ColliderComponent {
  bool enabled = false;
  ColliderShape shape = ColliderShape::Box;
  glm::vec3 center{0.0f};
  glm::vec3 size{1.0f}; // Box: full extents
  float radius = 0.5f;  // Sphere, Capsule
  float height = 2.0f;  // Capsule: total height along local Y, caps included
  bool isTrigger = false;
  float friction = 0.5f;   // 0..1, combined as sqrt(a * b)
  float bounciness = 0.0f; // 0..1, the larger of the pair wins

  // Repairs non-finite / out-of-range values (loading, MCP, inspector).
  // Inline: IlmeeeScene.hpp is header-only and calls it from LoadScene.
  void Clamp() {
    const ColliderComponent fallback;
    const auto fix = [](float &v, float def, float lo, float hi) {
      v = std::isfinite(v) ? std::clamp(v, lo, hi) : def;
    };
    if (static_cast<uint32_t>(shape) > 2) shape = ColliderShape::Box;
    for (int k = 0; k < 3; ++k) {
      fix(center[k], fallback.center[k], -100000.0f, 100000.0f);
      size[k] = std::fabs(size[k]);
      fix(size[k], fallback.size[k], 0.0f, 10000.0f);
    }
    fix(radius, fallback.radius, 0.0f, 10000.0f);
    fix(height, fallback.height, 0.0f, 10000.0f);
    fix(friction, fallback.friction, 0.0f, 1.0f);
    fix(bounciness, fallback.bounciness, 0.0f, 1.0f);
  }
};

// Sizes `collider` (keeping its shape) to object-space mesh bounds. Flat
// meshes (planes) get a thin box that extends downward from the surface.
void FitColliderToBounds(ColliderComponent &collider, const glm::vec3 &min,
                         const glm::vec3 &max);

// A collider placed in the world. Spheres and capsules are a segment
// (center ± axes[1] * halfSegment) swept by `radius`.
struct PhysicsShape {
  ColliderShape shape = ColliderShape::Box;
  glm::vec3 center{0.0f};
  glm::mat3 axes{1.0f};          // orthonormal columns
  glm::vec3 halfExtents{0.5f};   // Box
  float radius = 0.5f;           // Sphere, Capsule
  float halfSegment = 0.0f;      // Capsule
};

PhysicsShape MakeWorldShape(const ColliderComponent &collider,
                            const glm::mat4 &objectToWorld);
void ShapeBounds(const PhysicsShape &shape, glm::vec3 &min, glm::vec3 &max);

// Separating contact between two shapes. `normal` points from a to b;
// moving b by normal * depth separates them. depth may be slightly
// negative (down to -margin) for shapes that touch without overlapping.
struct Contact {
  glm::vec3 normal{0.0f, 1.0f, 0.0f};
  float depth = 0.0f;
};
bool Collide(const PhysicsShape &a, const PhysicsShape &b, Contact &out,
             float margin = 0.0f);

// Line-list wireframe of a shape (pairs of points), for the editor gizmo.
void AppendShapeWireframe(const PhysicsShape &shape,
                          std::vector<glm::vec3> &lines);

enum class BodyType { Static, Kinematic, Character, Dynamic };

struct PhysicsBody {
  size_t owner = 0; // caller's id (scene object index)
  BodyType type = BodyType::Static;
  bool hasCollider = true;
  bool trigger = false;
  PhysicsShape shape;
  float friction = 0.5f;
  float bounciness = 0.0f;
  // Dynamic only.
  float inverseMass = 1.0f;
  float drag = 0.0f;
  glm::vec3 gravity{0.0f};
  glm::vec3 velocity{0.0f};
  // Step results.
  glm::vec3 displacement{0.0f}; // total movement this step
  bool grounded = false;        // resting on something below it
};

class PhysicsWorld {
public:
  std::vector<PhysicsBody> bodies;
  // Pairs never tested (e.g. an object and its own children).
  std::function<bool(size_t ownerA, size_t ownerB)> ignorePair;

  // Integrates Dynamic bodies, then pushes overlapping bodies apart and
  // applies bounce/friction. Fills displacement/grounded and triggers().
  void Step(float deltaSeconds);

  // (trigger owner, other owner) overlapping after the last Step.
  const std::vector<std::pair<size_t, size_t>> &Triggers() const {
    return triggers_;
  }

private:
  std::vector<std::pair<size_t, size_t>> triggers_;
};

// Runs scene-object physics at a fixed 60 Hz during Play. Shared by the
// editor's Game tab and IlmeeePlayer. Call after MovementSystem::Update.
class PhysicsSystem {
public:
  void Reset();
  void Update(SceneRenderer &renderer, float deltaSeconds,
              MovementSystem *movement = nullptr);
  void Step(SceneRenderer &renderer, float deltaSeconds,
            MovementSystem *movement = nullptr);

  // Scene objects inside trigger colliders after the last step.
  const std::vector<std::pair<size_t, size_t>> &Triggers() const {
    return world_.Triggers();
  }

private:
  PhysicsWorld world_;
  std::unordered_map<size_t, glm::vec3> velocities_;
  float accumulator_ = 0.0f;
};

} // namespace ilmeee

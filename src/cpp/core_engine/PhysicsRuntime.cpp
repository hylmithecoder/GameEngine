// Feeds scene objects into PhysicsWorld each fixed step and writes the
// result back (positions, character ground). See Physics.hpp for roles.
#include "../../../include/core_engine/MovementScript.hpp"
#include "../../../include/core_engine/Physics.hpp"
#include "../../../include/core_engine/SceneRenderer.hpp"
#include <algorithm>
#include <cmath>

namespace ilmeee {
namespace {
constexpr float kStep = 1.0f / 60.0f;

bool Finite(const glm::vec3 &v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool IsAncestor(const SceneRenderer &renderer, size_t ancestor, size_t i) {
  const size_t count = renderer.GetMesh3DCount();
  int p = renderer.GetMesh3DParent(i);
  for (size_t steps = 0; p >= 0 && steps < count; ++steps) {
    if ((size_t)p == ancestor) return true;
    p = renderer.GetMesh3DParent((size_t)p);
  }
  return false;
}

bool DrivenByMovementScript(const SceneRenderer::Mesh3D &mesh) {
  return mesh.movable && !mesh.movementScript.empty();
}
} // namespace

void PhysicsSystem::Reset() {
  world_.bodies.clear();
  velocities_.clear();
  accumulator_ = 0.0f;
}

void PhysicsSystem::Update(SceneRenderer &renderer, float deltaSeconds,
                           MovementSystem *movement) {
  if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) return;
  accumulator_ = std::min(accumulator_ + deltaSeconds, 0.25f);
  for (int steps = 0; accumulator_ >= kStep && steps < 5; ++steps) {
    Step(renderer, kStep, movement);
    accumulator_ -= kStep;
  }
}

void PhysicsSystem::Step(SceneRenderer &renderer, float deltaSeconds,
                         MovementSystem *movement) {
  world_.bodies.clear();
  for (size_t i = 0; i < renderer.GetMesh3DCount(); ++i) {
    const auto &mesh = renderer.meshes3d[i];
    if (mesh.isLight || mesh.isCamera) continue;
    const bool collider = mesh.collider.enabled;
    if (!collider && !mesh.hasPhysics) continue;

    PhysicsBody body;
    body.owner = i;
    body.hasCollider = collider;
    body.trigger = collider && mesh.collider.isTrigger;
    body.friction = mesh.collider.friction;
    body.bounciness = mesh.collider.bounciness;
    if (DrivenByMovementScript(mesh))
      body.type = BodyType::Character;
    else if (mesh.hasPhysics)
      body.type = mesh.isKinematic ? BodyType::Kinematic : BodyType::Dynamic;
    else
      body.type = BodyType::Static;

    const glm::mat4 world = renderer.GetMesh3DWorldTRS(i);
    if (collider) {
      body.shape = MakeWorldShape(mesh.collider, world);
    } else {
      body.shape.center = glm::vec3(world[3]);
    }
    if (body.type == BodyType::Dynamic) {
      body.inverseMass = 1.0f / std::max(mesh.mass, 1e-4f);
      body.drag = mesh.drag;
      body.gravity = mesh.useGravity ? glm::vec3(0.0f, mesh.gravityY, 0.0f)
                                     : glm::vec3(0.0f);
      if (auto it = velocities_.find(i); it != velocities_.end())
        body.velocity = it->second;
    }
    world_.bodies.push_back(body);
  }
  // A child collider rides on its parent; they must not push each other.
  world_.ignorePair = [&renderer](size_t a, size_t b) {
    return IsAncestor(renderer, a, b) || IsAncestor(renderer, b, a);
  };
  world_.Step(deltaSeconds);
  world_.ignorePair = nullptr;

  for (const PhysicsBody &body : world_.bodies) {
    const size_t i = body.owner;
    if (body.type == BodyType::Dynamic)
      velocities_[i] = Finite(body.velocity) ? body.velocity : glm::vec3(0.0f);
    if (body.type != BodyType::Dynamic && body.type != BodyType::Character)
      continue;
    glm::vec3 local = renderer.GetMesh3DPosition(i);
    if (Finite(body.displacement) && glm::dot(body.displacement, body.displacement) > 0.0f) {
      const glm::vec3 worldPos = renderer.GetMesh3DWorldPosition(i) + body.displacement;
      local = worldPos;
      const int parent = renderer.GetMesh3DParent(i);
      if (parent >= 0)
        local = glm::vec3(glm::inverse(renderer.GetMesh3DWorldTRS((size_t)parent)) *
                          glm::vec4(worldPos, 1.0f));
      if (Finite(local))
        renderer.SetMesh3DTransform(i, local, renderer.GetMesh3DRotation(i),
                                    renderer.GetMesh3DScale(i));
    }
    if (body.type == BodyType::Character && movement)
      movement->SetGroundSupport(i, body.grounded, local.y);
  }
}

} // namespace ilmeee

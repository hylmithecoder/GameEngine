// Collision shapes, contact generation and the step solver. No renderer
// or ImGui dependency, so TestPhysics links it alone.
#include "../../../include/core_engine/Physics.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace ilmeee {
namespace {
constexpr float kPi = 3.14159265358979f;
// Contacts closer than this count as touching (grounded, triggers stay on).
constexpr float kSkin = 0.02f;
// Overlap left after correction so resting bodies keep their contact.
constexpr float kSlop = 0.002f;
constexpr int kIterations = 4;

bool Finite(float v) { return std::isfinite(v); }
bool Finite(const glm::vec3 &v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool IsRound(const PhysicsShape &s) { return s.shape != ColliderShape::Box; }

void Segment(const PhysicsShape &s, glm::vec3 &a, glm::vec3 &b) {
  const glm::vec3 half = s.axes[1] * s.halfSegment;
  a = s.center - half;
  b = s.center + half;
}

// Closest points between segments p1-q1 and p2-q2 (Ericson, RTCD 5.1.9).
void ClosestSegmentSegment(const glm::vec3 &p1, const glm::vec3 &q1,
                           const glm::vec3 &p2, const glm::vec3 &q2,
                           glm::vec3 &c1, glm::vec3 &c2) {
  const glm::vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
  const float a = glm::dot(d1, d1), e = glm::dot(d2, d2), f = glm::dot(d2, r);
  float s = 0.0f, t = 0.0f;
  if (a <= 1e-12f && e <= 1e-12f) {
    c1 = p1;
    c2 = p2;
    return;
  }
  if (a <= 1e-12f) {
    t = std::clamp(f / e, 0.0f, 1.0f);
  } else {
    const float c = glm::dot(d1, r);
    if (e <= 1e-12f) {
      s = std::clamp(-c / a, 0.0f, 1.0f);
    } else {
      const float b = glm::dot(d1, d2), denom = a * e - b * b;
      s = denom > 1e-12f ? std::clamp((b * f - c * e) / denom, 0.0f, 1.0f)
                         : 0.0f;
      t = (b * s + f) / e;
      if (t < 0.0f) {
        t = 0.0f;
        s = std::clamp(-c / a, 0.0f, 1.0f);
      } else if (t > 1.0f) {
        t = 1.0f;
        s = std::clamp((b - c) / a, 0.0f, 1.0f);
      }
    }
  }
  c1 = p1 + d1 * s;
  c2 = p2 + d2 * t;
}

// Any unit vector perpendicular-ish fallback when centers coincide.
glm::vec3 FallbackNormal() { return glm::vec3(0.0f, 1.0f, 0.0f); }

bool RoundRound(const PhysicsShape &a, const PhysicsShape &b, Contact &out,
                float margin) {
  glm::vec3 a0, a1, b0, b1, pa, pb;
  Segment(a, a0, a1);
  Segment(b, b0, b1);
  ClosestSegmentSegment(a0, a1, b0, b1, pa, pb);
  const glm::vec3 d = pb - pa;
  const float dist = glm::length(d);
  const float depth = a.radius + b.radius - dist;
  if (depth <= -margin) return false;
  out.normal = dist > 1e-6f ? d / dist : FallbackNormal();
  out.depth = depth;
  return true;
}

// Normal points from the box to the round shape.
bool BoxRound(const PhysicsShape &box, const PhysicsShape &round,
              Contact &out, float margin) {
  const glm::mat3 toLocal = glm::transpose(box.axes);
  glm::vec3 s0, s1;
  Segment(round, s0, s1);
  const glm::vec3 a = toLocal * (s0 - box.center);
  const glm::vec3 b = toLocal * (s1 - box.center);
  const glm::vec3 h = box.halfExtents;
  const glm::vec3 ab = b - a;
  const float len2 = glm::dot(ab, ab);
  const auto closestT = [&](const glm::vec3 &q) {
    return len2 > 1e-12f ? std::clamp(glm::dot(q - a, ab) / len2, 0.0f, 1.0f)
                         : 0.0f;
  };
  // Alternate between the closest box point and the closest segment point;
  // both sets are convex, so this settles on the closest pair.
  float t = closestT(glm::vec3(0.0f));
  for (int it = 0; it < 8; ++it) {
    const glm::vec3 q = glm::clamp(a + ab * t, -h, h);
    const float next = closestT(q);
    const bool settled = std::fabs(next - t) < 1e-6f;
    t = next;
    if (settled) break;
  }
  const glm::vec3 p = a + ab * t;
  const glm::vec3 q = glm::clamp(p, -h, h);
  const glm::vec3 d = p - q;
  const float dist = glm::length(d);
  if (dist > 1e-6f) {
    const float depth = round.radius - dist;
    if (depth <= -margin) return false;
    out.normal = box.axes * (d / dist);
    out.depth = depth;
    return true;
  }
  // The core segment reaches inside the box: push out along the box face
  // that needs the least travel for the whole swept shape.
  float best = std::numeric_limits<float>::max();
  glm::vec3 normal(0.0f, 1.0f, 0.0f);
  for (int k = 0; k < 3; ++k) {
    const float lo = std::min(a[k], b[k]) - round.radius;
    const float hi = std::max(a[k], b[k]) + round.radius;
    const float pushPos = h[k] - lo, pushNeg = hi + h[k];
    glm::vec3 axis(0.0f);
    if (pushPos < best) {
      best = pushPos;
      axis[k] = 1.0f;
      normal = axis;
    }
    if (pushNeg < best) {
      best = pushNeg;
      axis[k] = -1.0f;
      normal = axis;
    }
  }
  out.normal = box.axes * normal;
  out.depth = best;
  return true;
}

// Separating axis test over the 15 box/box axes.
bool BoxBox(const PhysicsShape &a, const PhysicsShape &b, Contact &out,
            float margin) {
  const glm::vec3 d = b.center - a.center;
  float bestScore = std::numeric_limits<float>::max();
  bool found = false;
  const auto test = [&](glm::vec3 axis, bool edge) {
    const float len = glm::length(axis);
    if (len < 1e-5f) return true; // parallel edges: covered by face axes
    axis /= len;
    float ra = 0.0f, rb = 0.0f;
    for (int i = 0; i < 3; ++i) {
      ra += a.halfExtents[i] * std::fabs(glm::dot(a.axes[i], axis));
      rb += b.halfExtents[i] * std::fabs(glm::dot(b.axes[i], axis));
    }
    const float dist = glm::dot(d, axis);
    const float overlap = ra + rb - std::fabs(dist);
    if (overlap <= -margin) return false;
    // Prefer face normals on near ties: edge normals make boxes slide off
    // each other's corners.
    const float score = overlap + (edge ? 1e-3f : 0.0f);
    if (score < bestScore) {
      bestScore = score;
      out.depth = overlap;
      out.normal = dist < 0.0f ? -axis : axis;
      found = true;
    }
    return true;
  };
  for (int i = 0; i < 3; ++i)
    if (!test(a.axes[i], false) || !test(b.axes[i], false)) return false;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      if (!test(glm::cross(a.axes[i], b.axes[j]), true)) return false;
  return found;
}

float Weight(const PhysicsBody &self, const PhysicsBody &other) {
  switch (self.type) {
  case BodyType::Static:
  case BodyType::Kinematic:
    return 0.0f;
  case BodyType::Character:
    return other.type == BodyType::Dynamic ? 0.0f : 1.0f;
  case BodyType::Dynamic:
    return self.inverseMass;
  }
  return 0.0f;
}

bool Moves(const PhysicsBody &b) {
  return b.type == BodyType::Character || b.type == BodyType::Dynamic;
}

void Move(PhysicsBody &b, const glm::vec3 &delta) {
  b.shape.center += delta;
  b.displacement += delta;
}
} // namespace

void FitColliderToBounds(ColliderComponent &collider, const glm::vec3 &min,
                         const glm::vec3 &max) {
  if (!Finite(min) || !Finite(max) || glm::any(glm::lessThan(max, min)))
    return;
  glm::vec3 center = (min + max) * 0.5f;
  glm::vec3 size = max - min;
  // A plane has no thickness; give it some below its surface so objects
  // falling fast still land on the top face.
  constexpr float kThin = 0.1f;
  for (int k = 0; k < 3; ++k) {
    if (size[k] >= 0.01f) continue;
    size[k] = kThin;
    if (k == 1) center.y = max.y - kThin * 0.5f;
  }
  collider.center = center;
  collider.size = size;
  switch (collider.shape) {
  case ColliderShape::Box:
    break;
  case ColliderShape::Sphere:
    collider.radius = 0.5f * std::max({size.x, size.y, size.z});
    break;
  case ColliderShape::Capsule:
    collider.radius = 0.5f * std::max(size.x, size.z);
    collider.height = std::max(size.y, 2.0f * collider.radius);
    break;
  }
}

PhysicsShape MakeWorldShape(const ColliderComponent &collider,
                            const glm::mat4 &objectToWorld) {
  PhysicsShape s;
  s.shape = collider.shape;
  glm::vec3 scale(1.0f);
  for (int i = 0; i < 3; ++i) {
    const glm::vec3 column(objectToWorld[i]);
    scale[i] = glm::length(column);
    s.axes[i] = scale[i] > 1e-6f ? column / scale[i] : glm::vec3(0.0f);
    if (scale[i] <= 1e-6f) s.axes[i][i] = 1.0f;
  }
  s.center = glm::vec3(objectToWorld * glm::vec4(collider.center, 1.0f));
  switch (collider.shape) {
  case ColliderShape::Box:
    s.halfExtents = glm::abs(collider.size) * 0.5f * scale;
    break;
  case ColliderShape::Sphere:
    s.radius = collider.radius * std::max({scale.x, scale.y, scale.z});
    break;
  case ColliderShape::Capsule:
    s.radius = collider.radius * std::max(scale.x, scale.z);
    s.halfSegment =
        std::max(0.0f, collider.height * 0.5f * scale.y - s.radius);
    break;
  }
  return s;
}

void ShapeBounds(const PhysicsShape &shape, glm::vec3 &min, glm::vec3 &max) {
  if (shape.shape == ColliderShape::Box) {
    glm::vec3 extent(0.0f);
    for (int i = 0; i < 3; ++i)
      extent += glm::abs(shape.axes[i]) * shape.halfExtents[i];
    min = shape.center - extent;
    max = shape.center + extent;
    return;
  }
  glm::vec3 a, b;
  Segment(shape, a, b);
  min = glm::min(a, b) - glm::vec3(shape.radius);
  max = glm::max(a, b) + glm::vec3(shape.radius);
}

bool Collide(const PhysicsShape &a, const PhysicsShape &b, Contact &out,
             float margin) {
  if (IsRound(a) && IsRound(b)) return RoundRound(a, b, out, margin);
  if (!IsRound(a) && !IsRound(b)) return BoxBox(a, b, out, margin);
  if (!IsRound(a)) return BoxRound(a, b, out, margin);
  if (!BoxRound(b, a, out, margin)) return false;
  out.normal = -out.normal;
  return true;
}

void AppendShapeWireframe(const PhysicsShape &shape,
                          std::vector<glm::vec3> &lines) {
  const glm::vec3 x = shape.axes[0], y = shape.axes[1], z = shape.axes[2];
  if (shape.shape == ColliderShape::Box) {
    const glm::vec3 h = shape.halfExtents;
    glm::vec3 corner[8];
    for (int i = 0; i < 8; ++i)
      corner[i] = shape.center + x * ((i & 1) ? h.x : -h.x) +
                  y * ((i & 2) ? h.y : -h.y) + z * ((i & 4) ? h.z : -h.z);
    for (int i = 0; i < 8; ++i)
      for (int bit = 1; bit < 8; bit <<= 1)
        if (!(i & bit)) {
          lines.push_back(corner[i]);
          lines.push_back(corner[i | bit]);
        }
    return;
  }
  constexpr int kSegments = 24;
  // Arc of radius r around `c` in the plane (u, v), from angle a0 to a1.
  const auto arc = [&](const glm::vec3 &c, const glm::vec3 &u,
                       const glm::vec3 &v, float a0, float a1) {
    const int n = std::max(2, (int)std::ceil(kSegments * (a1 - a0) / (2 * kPi)));
    for (int i = 0; i < n; ++i) {
      const float t0 = a0 + (a1 - a0) * i / n, t1 = a0 + (a1 - a0) * (i + 1) / n;
      lines.push_back(c + (u * std::cos(t0) + v * std::sin(t0)) * shape.radius);
      lines.push_back(c + (u * std::cos(t1) + v * std::sin(t1)) * shape.radius);
    }
  };
  if (shape.shape == ColliderShape::Sphere) {
    arc(shape.center, x, y, 0, 2 * kPi);
    arc(shape.center, y, z, 0, 2 * kPi);
    arc(shape.center, z, x, 0, 2 * kPi);
    return;
  }
  glm::vec3 bottom, top;
  Segment(shape, bottom, top);
  arc(top, x, z, 0, 2 * kPi);
  arc(bottom, x, z, 0, 2 * kPi);
  arc(top, x, y, 0, kPi);
  arc(top, z, y, 0, kPi);
  arc(bottom, x, y, kPi, 2 * kPi);
  arc(bottom, z, y, kPi, 2 * kPi);
  for (const glm::vec3 &side : {x, -x, z, -z}) {
    lines.push_back(bottom + side * shape.radius);
    lines.push_back(top + side * shape.radius);
  }
}

void PhysicsWorld::Step(float dt) {
  triggers_.clear();
  collisions_.clear();
  if (!Finite(dt) || dt <= 0.0f) return;
  for (PhysicsBody &b : bodies) {
    b.displacement = glm::vec3(0.0f);
    b.grounded = false;
    if (b.type != BodyType::Dynamic) continue;
    b.velocity += b.gravity * dt;
    b.velocity /= 1.0f + std::max(0.0f, b.drag) * dt;
    if (!Finite(b.velocity)) b.velocity = glm::vec3(0.0f);
    Move(b, b.velocity * dt);
  }

  // Candidate pairs from bounds, padded for the solver's own pushes.
  const size_t n = bodies.size();
  std::vector<glm::vec3> lo(n), hi(n);
  for (size_t i = 0; i < n; ++i) {
    ShapeBounds(bodies[i].shape, lo[i], hi[i]);
    lo[i] -= glm::vec3(kSkin + 0.1f);
    hi[i] += glm::vec3(kSkin + 0.1f);
  }
  std::vector<std::pair<size_t, size_t>> solid, sensors;
  for (size_t i = 0; i < n; ++i) {
    const PhysicsBody &a = bodies[i];
    if (!a.hasCollider) continue;
    for (size_t j = i + 1; j < n; ++j) {
      const PhysicsBody &b = bodies[j];
      if (!b.hasCollider) continue;
      if (glm::any(glm::lessThan(hi[i], lo[j])) ||
          glm::any(glm::lessThan(hi[j], lo[i])))
        continue;
      if (ignorePair && ignorePair(a.owner, b.owner)) continue;
      if (a.trigger || b.trigger) {
        if (a.type != BodyType::Static || b.type != BodyType::Static)
          sensors.emplace_back(i, j);
      } else if (Moves(a) || Moves(b)) {
        solid.emplace_back(i, j);
      }
    }
  }

  for (int iteration = 0; iteration < kIterations; ++iteration) {
    for (const auto &[i, j] : solid) {
      PhysicsBody &a = bodies[i], &b = bodies[j];
      Contact c;
      if (!Collide(a.shape, b.shape, c, kSkin)) continue;
      if (std::find(collisions_.begin(), collisions_.end(),
                    std::pair<size_t, size_t>{a.owner, b.owner}) ==
          collisions_.end())
        collisions_.emplace_back(a.owner, b.owner);
      const glm::vec3 n = c.normal;
      // Something below holds the body up (normal points up into it).
      if (n.y > 0.5f && Moves(b)) b.grounded = true;
      if (-n.y > 0.5f && Moves(a)) a.grounded = true;
      if (c.depth <= 0.0f) continue;

      const float wa = Weight(a, b), wb = Weight(b, a);
      if (wa + wb <= 0.0f) continue;
      const float correction = std::max(0.0f, c.depth - kSlop) / (wa + wb);
      Move(a, -n * correction * wa);
      Move(b, n * correction * wb);

      const float ima = a.type == BodyType::Dynamic ? a.inverseMass : 0.0f;
      const float imb = b.type == BodyType::Dynamic ? b.inverseMass : 0.0f;
      if (ima + imb <= 0.0f) continue;
      const glm::vec3 relative = b.velocity - a.velocity;
      const float vn = glm::dot(relative, n);
      if (vn >= 0.0f) continue; // already separating
      // Slow contacts don't bounce, or resting bodies would jitter.
      const float e = -vn > 1.0f ? std::max(a.bounciness, b.bounciness) : 0.0f;
      const float jn = -(1.0f + e) * vn / (ima + imb);
      a.velocity -= n * jn * ima;
      b.velocity += n * jn * imb;

      // Coulomb friction against the sliding direction.
      const glm::vec3 rel = b.velocity - a.velocity;
      const glm::vec3 tangent = rel - n * glm::dot(rel, n);
      const float slide = glm::length(tangent);
      if (slide < 1e-6f) continue;
      const float mu = std::sqrt(a.friction * b.friction);
      const float jt = std::min(slide / (ima + imb), mu * jn);
      const glm::vec3 t = tangent / slide;
      a.velocity += t * jt * ima;
      b.velocity -= t * jt * imb;
    }
  }

  for (const auto &[i, j] : sensors) {
    const PhysicsBody &a = bodies[i], &b = bodies[j];
    Contact c;
    if (!Collide(a.shape, b.shape, c, 0.0f) || c.depth <= 0.0f) continue;
    if (a.trigger) triggers_.emplace_back(a.owner, b.owner);
    if (b.trigger) triggers_.emplace_back(b.owner, a.owner);
  }
}

} // namespace ilmeee

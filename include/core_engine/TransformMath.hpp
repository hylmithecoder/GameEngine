#pragma once
// Translate/rotate/scale <-> matrix, in the one convention the scene uses:
//
//   M = T(position) * Rz(rot.z) * Ry(rot.y) * Rx(rot.x) * S(scale)
//
// with rotations as Euler degrees. Kept apart from SceneRenderer (and free of
// Vulkan) so the round trip can be unit-tested.

#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace ilmeee {

inline glm::mat4 ComposeTRS(const glm::vec3 &position,
                            const glm::vec3 &rotationDeg,
                            const glm::vec3 &scale) {
  glm::mat4 M(1.0f);
  M = glm::translate(M, position);
  M = glm::rotate(M, glm::radians(rotationDeg.z), glm::vec3(0, 0, 1));
  M = glm::rotate(M, glm::radians(rotationDeg.y), glm::vec3(0, 1, 0));
  M = glm::rotate(M, glm::radians(rotationDeg.x), glm::vec3(1, 0, 0));
  M = glm::scale(M, scale);
  return M;
}

// Inverse of ComposeTRS for matrices it can produce. A parent with
// non-uniform scale and a rotated child yields shear, which TRS cannot hold;
// the result is then the closest rotation + per-axis scale.
inline void DecomposeTRS(const glm::mat4 &M, glm::vec3 &position,
                         glm::vec3 &rotationDeg, glm::vec3 &scale) {
  position = glm::vec3(M[3]);

  glm::vec3 c0(M[0]), c1(M[1]), c2(M[2]);
  scale = glm::vec3(glm::length(c0), glm::length(c1), glm::length(c2));
  // A mirrored basis cannot be a rotation; fold the flip into scale.x.
  if (glm::dot(glm::cross(c0, c1), c2) < 0.0f)
    scale.x = -scale.x;

  const float eps = 1e-8f;
  glm::mat3 R(std::fabs(scale.x) > eps ? c0 / scale.x : glm::vec3(1, 0, 0),
              std::fabs(scale.y) > eps ? c1 / scale.y : glm::vec3(0, 1, 0),
              std::fabs(scale.z) > eps ? c2 / scale.z : glm::vec3(0, 0, 1));

  // R = Rz*Ry*Rx. glm is column-major, R[col][row]:
  //   R[0][2] = -sin(y)
  //   R[1][2] =  cos(y) sin(x),  R[2][2] = cos(y) cos(x)
  //   R[0][1] =  cos(y) sin(z),  R[0][0] = cos(y) cos(z)
  // cos(y) from the column length rather than asin(): stays accurate right
  // up to the singularity instead of losing precision near y = ±90.
  float x, y, z;
  float cy = std::sqrt(R[0][0] * R[0][0] + R[0][1] * R[0][1]);
  y = std::atan2(-R[0][2], cy);
  if (cy > 1e-6f) {
    x = std::atan2(R[1][2], R[2][2]);
    z = std::atan2(R[0][1], R[0][0]);
  } else {
    // Gimbal lock: x and z rotate about the same axis; put it all in x.
    z = 0.0f;
    x = std::atan2(-R[2][1], R[1][1]);
  }
  rotationDeg = glm::degrees(glm::vec3(x, y, z));
}

} // namespace ilmeee

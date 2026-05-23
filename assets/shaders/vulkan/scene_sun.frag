#version 450

layout(location = 0) in vec3 vNormal;

layout(push_constant) uniform PC {
  mat4 mvp;
  vec4 color; // xyz = sun colour
} pc;

layout(location = 0) out vec4 outColor;

void main() {
  // Slightly brighter near the top so the sphere has a little volume.
  float grad = 0.82 + 0.18 * (vNormal.y * 0.5 + 0.5);
  outColor = vec4(pc.color.rgb * grad, 1.0);
}

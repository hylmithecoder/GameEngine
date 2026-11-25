#version 450

// Decorative sun sphere. Unlit; the fragment shader only adds a faint
// gradient using the object-space normal so the disc reads as a sphere.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

layout(push_constant) uniform PC {
  mat4 mvp;
  vec4 color; // xyz = sun colour
} pc;

layout(location = 0) out vec3 vNormal;

void main() {
  vNormal = normalize(inNormal);
  gl_Position = pc.mvp * vec4(inPos, 1.0);
}

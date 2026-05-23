#version 450

// Editor line gizmos (dashed frustums / direction rays). Dashes are built
// on the CPU as separate short segments, so this is just a colored line.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;

layout(push_constant) uniform PC {
  mat4 mvp;
} pc;

layout(location = 0) out vec3 vColor;

void main() {
  vColor = inColor;
  gl_Position = pc.mvp * vec4(inPos, 1.0);
}

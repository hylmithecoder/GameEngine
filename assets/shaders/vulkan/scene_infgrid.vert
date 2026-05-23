#version 450

// Infinite ground grid. A single fullscreen triangle (no vertex buffer);
// each corner is unprojected to a near and far world-space point so the
// fragment shader can raycast the y=0 plane. This draws grid lines only
// where the ground is actually visible — constant memory, ~zero CPU.
layout(push_constant) uniform PC {
  mat4 view;
  mat4 proj;
} pc;

layout(location = 0) out vec3 vNear;
layout(location = 1) out vec3 vFar;

vec3 unproject(vec2 ndc, float z, mat4 invVP) {
  vec4 p = invVP * vec4(ndc, z, 1.0);
  return p.xyz / p.w;
}

void main() {
  // Fullscreen triangle: vertices (-1,-1), (3,-1), (-1,3).
  vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
  vec2 p = uv * 2.0 - 1.0;

  mat4 invVP = inverse(pc.proj * pc.view);
  vNear = unproject(p, 0.0, invVP);
  vFar = unproject(p, 1.0, invVP);

  gl_Position = vec4(p, 0.0, 1.0);
}

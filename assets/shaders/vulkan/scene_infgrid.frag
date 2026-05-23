#version 450

layout(location = 0) in vec3 vNear;
layout(location = 1) in vec3 vFar;

layout(push_constant) uniform PC {
  mat4 view;
  mat4 proj;
} pc;

layout(location = 0) out vec4 outColor;

// Anti-aliased line coverage for a grid of the given world-space spacing.
// Uses screen-space derivatives so lines stay ~1px regardless of distance.
float gridFactor(vec2 coordWorld, float spacing) {
  vec2 c = coordWorld / spacing;
  vec2 g = abs(fract(c - 0.5) - 0.5) / fwidth(c);
  float line = min(g.x, g.y);
  return 1.0 - min(line, 1.0);
}

void main() {
  // Raycast the y=0 plane along the view ray for this fragment.
  float denom = vFar.y - vNear.y;
  if (abs(denom) < 1e-6)
    discard;
  float t = -vNear.y / denom;
  vec3 P = vNear + t * (vFar - vNear);

  // Project the hit back to clip space: gives the depth (so meshes occlude
  // the grid) and the linear eye distance (perspective w = -z_eye) for fade.
  vec4 clip = pc.proj * pc.view * vec4(P, 1.0);
  if (clip.w <= 0.0)
    discard; // behind the camera
  gl_FragDepth = clip.z / clip.w;
  float eyeDepth = clip.w;

  vec2 coord = P.xz;
  float minor = gridFactor(coord, 1.0);
  float major = gridFactor(coord, 10.0);

  vec3 minorCol = vec3(0.32, 0.32, 0.34);
  vec3 majorCol = vec3(0.55, 0.55, 0.58);
  vec3 col = mix(minorCol, majorCol, major);
  float a = max(minor * 0.5, major * 0.85);

  // Coloured world axes (red = X axis at z=0, blue = Z axis at x=0),
  // matching the 2D overlay grid.
  vec2 fw = fwidth(coord);
  float xAxis = 1.0 - min(abs(P.x) / fw.x, 1.0); // x == 0  -> Z axis (blue)
  float zAxis = 1.0 - min(abs(P.z) / fw.y, 1.0); // z == 0  -> X axis (red)
  if (xAxis > 0.0) {
    col = vec3(0.30, 0.55, 0.95);
    a = max(a, xAxis);
  }
  if (zAxis > 0.0) {
    col = vec3(0.85, 0.25, 0.25);
    a = max(a, zAxis);
  }

  // Dissolve toward the horizon so the grid covers only the near, useful
  // area and never shows a hard square edge.
  float fade = 1.0 - clamp((eyeDepth - 25.0) / 125.0, 0.0, 1.0);
  a *= fade;

  if (a <= 0.001)
    discard;
  outColor = vec4(col, a);
}

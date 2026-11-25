#version 450

// Inverted-hull ink outline for NPR lighting: every mesh is drawn a second
// time, pushed out along its normals; scene_outline.frag keeps only the
// parts that face away from the camera, so the hull shows as a rim around
// the silhouette.

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 1, binding = 0) uniform Scene {
    mat4 viewProj;
    vec4 lightPosOrDir;
    vec4 lightColorType;
    vec4 lightDir;
    vec4 lightParams;
    vec4 cameraPos;
    vec4 npr;
    vec4 nprShadow;
    vec4 nprRim;
    vec4 nprOutline; // rgb = colour, w = width (about pixels at 1080p)
} scene;

layout(push_constant) uniform PC {
    mat4 model;
    vec4 material;
} pc;

layout(location = 0) out vec3 vNormalWorld;
layout(location = 1) out vec3 vPosWorld;

void main() {
    vec4 world = pc.model * vec4(inPos, 1.0);
    vec3 normal = mat3(pc.model) * inNormal;
    float len = length(normal);
    normal = len > 1e-6 ? normal / len : vec3(0.0);
    // Scale by distance so the line keeps the same on-screen width.
    float dist = length(scene.cameraPos.xyz - world.xyz);
    world.xyz += normal * scene.nprOutline.w * 0.001 * dist;
    vNormalWorld = normal;
    vPosWorld = world.xyz;
    gl_Position = scene.viewProj * world;
}

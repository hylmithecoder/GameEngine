#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

// Per pass (same for every mesh): camera + the scene light. Kept out of the
// push constants, which Vulkan only guarantees up to 128 bytes (Adreno 5xx
// has exactly that; going past it scrambled every vertex).
layout(set = 1, binding = 0) uniform Scene {
    mat4 viewProj;
    vec4 lightPosOrDir;  // xyz = position (or direction for dir-lights), w = intensity
    vec4 lightColorType; // xyz = color, w = type
    vec4 lightDir;       // xyz = spotlight forward vector, w = unused
    vec4 lightParams;    // x = range, y = spotAngleRad, z = gamma, w = spotCosOuter
    vec4 cameraPos;      // xyz = eye, w = 1 when NPR lighting is on
    vec4 npr;            // x = bands, y = softness, z = specular size, w = specular strength
    vec4 nprShadow;      // rgb = shadow colour, w = rim width
    vec4 nprRim;         // rgb = rim colour, w = rim strength
    vec4 nprOutline;     // rgb = outline colour, w = outline width (0 = off)
} scene;

// Per draw: 80 bytes.
layout(push_constant) uniform PC {
    mat4 model;
    vec4 material; // xyz = diffuse tint / colour; w = 0 untextured,
                   // 1 textured, 2 textured unlit, 3 solid unlit (markers)
} pc;

layout(location = 0) out vec3 vNormalWorld;
layout(location = 1) out vec3 vPosWorld;
layout(location = 2) out vec4 vLightPosOrDir;
layout(location = 3) out vec4 vLightColorType;
layout(location = 4) out vec4 vLightDir;
layout(location = 5) out vec4 vLightParams;
layout(location = 6) out vec2 vUV;
layout(location = 7) out vec4 vMaterial;

void main() {
    vec4 world = pc.model * vec4(inPos, 1.0);
    vPosWorld = world.xyz;
    vNormalWorld = mat3(pc.model) * inNormal;
    vLightPosOrDir = scene.lightPosOrDir;
    vLightColorType = scene.lightColorType;
    vLightDir = scene.lightDir;
    vLightParams = scene.lightParams;
    vUV = inUV;
    vMaterial = pc.material;
    if (pc.material.w > 2.5 && pc.material.w < 3.5) {
        // Solid unlit colour: the fragment shader's emissive path.
        vLightColorType = vec4(pc.material.rgb, -1.0);
        vMaterial.w = 0.0;
    }
    gl_Position = scene.viewProj * world;
}

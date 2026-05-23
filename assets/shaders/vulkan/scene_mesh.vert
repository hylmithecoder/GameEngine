#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

layout(push_constant) uniform PC {
    mat4 mvp;
    mat4 model;
    vec4 lightPosOrDir;  // xyz = position (or direction for dir-lights), w = intensity
    vec4 lightColorType; // xyz = color, w = type (special -1.0 = emissive/unlit)
    vec4 lightDir;       // xyz = spotlight forward vector, w = unused
    vec4 lightParams;    // x = range, y = spotAngleRad, z = gamma, w = spotCosOuter
} pc;

layout(location = 0) out vec3 vNormalWorld;
layout(location = 1) out vec3 vPosWorld;
layout(location = 2) out vec4 vLightPosOrDir;
layout(location = 3) out vec4 vLightColorType;
layout(location = 4) out vec4 vLightDir;
layout(location = 5) out vec4 vLightParams;

void main() {
    vec4 world = pc.model * vec4(inPos, 1.0);
    vPosWorld = world.xyz;
    vNormalWorld = mat3(pc.model) * inNormal;
    vLightPosOrDir = pc.lightPosOrDir;
    vLightColorType = pc.lightColorType;
    vLightDir = pc.lightDir;
    vLightParams = pc.lightParams;
    gl_Position = pc.mvp * vec4(inPos, 1.0);
}

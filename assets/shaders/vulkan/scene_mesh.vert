#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

layout(push_constant) uniform PC {
    mat4 mvp;
    mat4 model;
} pc;

layout(location = 0) out vec3 vNormalWorld;
layout(location = 1) out vec3 vPosWorld;

void main() {
    vec4 world = pc.model * vec4(inPos, 1.0);
    vPosWorld = world.xyz;
    vNormalWorld = mat3(pc.model) * inNormal;
    gl_Position = pc.mvp * vec4(inPos, 1.0);
}

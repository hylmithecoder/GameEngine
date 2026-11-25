#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 outUV;

layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

void main() {
    outUV = inUV;
    gl_Position = push.model * vec4(inPos, 0.0, 1.0);
}

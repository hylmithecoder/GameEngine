#version 450

layout(location = 0) in vec3 fragColor; // warna dari vertex shader
layout(location = 0) out vec4 outColor; // output warna ke framebuffer

void main() {
    outColor = vec4(fragColor, 1.0); // final warna
}

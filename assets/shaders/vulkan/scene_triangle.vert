#version 450

// Self-contained "hello triangle" — no vertex buffer, no UBO, no
// descriptor set. Three positions + colors are baked into the shader
// and selected by gl_VertexIndex. Used by the editor's Scene panel as
// a smoke test that the offscreen renderer is alive.

layout(location = 0) out vec3 fragColor;

vec2 positions[3] = vec2[](
    vec2( 0.0, -0.5),
    vec2( 0.5,  0.5),
    vec2(-0.5,  0.5)
);

vec3 colors[3] = vec3[](
    vec3(1.0, 0.2, 0.2),
    vec3(0.2, 1.0, 0.2),
    vec3(0.2, 0.5, 1.0)
);

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragColor   = colors[gl_VertexIndex];
}

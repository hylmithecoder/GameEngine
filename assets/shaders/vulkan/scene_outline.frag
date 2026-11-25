#version 450

layout(location = 0) in vec3 vNormalWorld;
layout(location = 1) in vec3 vPosWorld;

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
    vec4 nprOutline;
} scene;

layout(location = 0) out vec4 outColor;

void main() {
    // Cull by normal rather than winding: imported models mix CW and CCW
    // triangles, but their normals point outward.
    vec3 V = scene.cameraPos.xyz - vPosWorld;
    if (dot(vNormalWorld, V) > 0.0) discard;
    outColor = vec4(scene.nprOutline.rgb, 1.0);
}

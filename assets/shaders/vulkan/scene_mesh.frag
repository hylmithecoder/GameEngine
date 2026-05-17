#version 450

// 3-light setup: key light from upper-right, fill from opposite to
// soften the shadow side, ambient + Fresnel rim for silhouette pop.

layout(location = 0) in vec3 vNormalWorld;
layout(location = 1) in vec3 vPosWorld;

layout(location = 0) out vec4 outColor;

const vec3 keyDir   = normalize(vec3( 0.55,  0.80,  0.30));
const vec3 fillDir  = normalize(vec3(-0.55,  0.40, -0.30));
const vec3 backDir  = normalize(vec3( 0.10, -0.40, -0.80));

const vec3 keyColor   = vec3(1.00, 0.96, 0.88);
const vec3 fillColor  = vec3(0.55, 0.65, 0.85);
const vec3 backColor  = vec3(0.30, 0.30, 0.40);
const vec3 ambient    = vec3(0.16, 0.17, 0.20);
const vec3 baseColor  = vec3(0.84, 0.80, 0.74);
const vec3 rimColor   = vec3(0.85, 0.92, 1.00);

void main() {
    vec3 N = normalize(vNormalWorld);
    if (!gl_FrontFacing) N = -N;

    float key  = max(dot(N, keyDir),  0.0);
    float fill = max(dot(N, fillDir), 0.0);
    float back = max(dot(N, backDir), 0.0);

    // Hemispheric ambient gradient: sky vs ground.
    vec3 sky    = vec3(0.40, 0.55, 0.75);
    vec3 ground = vec3(0.18, 0.16, 0.14);
    float upDot = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 hemi   = mix(ground, sky, upDot) * 0.35;

    vec3 lit = ambient
             + keyColor  * key  * 0.90
             + fillColor * fill * 0.35
             + backColor * back * 0.20
             + hemi;

    // Fresnel rim — cheap silhouette accent, view direction approximated
    // as -normalize(world position) so the camera "looks toward origin".
    vec3 V = normalize(-vPosWorld);
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    vec3 rim = rimColor * fresnel * 0.45;

    vec3 col = baseColor * lit + rim;

    // Mild gamma so highlights don't blow out flat.
    col = pow(col, vec3(1.0 / 1.05));

    outColor = vec4(col, 1.0);
}

#version 450

// Unity-style lighting with Directional, Point, and Spotlight computations,
// plus a customizable lighting gamma.

layout(location = 0) in vec3 vNormalWorld;
layout(location = 1) in vec3 vPosWorld;
layout(location = 2) in vec4 vLightPosOrDir;  // xyz = position/direction, w = intensity
layout(location = 3) in vec4 vLightColorType; // xyz = color, w = type (special -1.0 = emissive/unlit)
layout(location = 4) in vec4 vLightDir;       // xyz = spotlight forward vector, w = unused
layout(location = 5) in vec4 vLightParams;    // x = range, y = spotAngleRad, z = gamma, w = spotCosOuter

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(vNormalWorld);
    if (!gl_FrontFacing) N = -N;

    // Check for special unlit emissive mode (drawn light source sphere itself)
    if (vLightColorType.w < -0.5) {
        outColor = vec4(vLightColorType.xyz, 1.0);
        return;
    }

    // Light inputs
    vec3 lightPosOrDir = vLightPosOrDir.xyz;
    float lightInt     = vLightPosOrDir.w;
    vec3 lightCol      = vLightColorType.xyz;
    int type           = int(vLightColorType.w + 0.5);
    vec3 spotDir       = normalize(vLightDir.xyz);
    float range        = vLightParams.x;
    float spotAngle    = vLightParams.y;
    float gamma        = vLightParams.z;
    float cosOuter     = vLightParams.w;

    // Direct lighting computation
    vec3 L = vec3(0.0);
    float attenuation = 1.0;
    float spotEffect  = 1.0;

    if (type == 0) { // Directional Light
        L = normalize(lightPosOrDir); // points toward light
    } else if (type == 1) { // Point Light
        vec3 lightVec = lightPosOrDir - vPosWorld;
        float dist = length(lightVec);
        L = normalize(lightVec);
        attenuation = clamp(1.0 - (dist / range), 0.0, 1.0);
    } else if (type == 2) { // Spotlight (Senter)
        vec3 lightVec = lightPosOrDir - vPosWorld;
        float dist = length(lightVec);
        L = normalize(lightVec);
        attenuation = clamp(1.0 - (dist / range), 0.0, 1.0);

        // Vector from light to vertex is -L. Direction of light cone is spotDir.
        float cosAngle = dot(-L, spotDir);
        
        // Spotlight cone smooth falloff (inner spot angle is roughly half of outer spot angle)
        float cosInner = mix(1.0, cosOuter, 0.4); 
        spotEffect = smoothstep(cosOuter, cosInner, cosAngle);
    }

    float key = max(dot(N, L), 0.0) * attenuation * spotEffect;

    // Complementary ambient and fill lights (derived from primary light direction L)
    vec3 fillDir  = normalize(vec3(-L.x, L.y * 0.5, -L.z));
    vec3 backDir  = normalize(vec3(L.x * 0.2, -L.y * 0.5, -L.z));

    vec3 fillColor = vec3(0.55, 0.65, 0.85);
    vec3 backColor = vec3(0.30, 0.30, 0.40);
    vec3 ambient   = vec3(0.16, 0.17, 0.20);
    vec3 baseColor = vec3(0.84, 0.80, 0.74);
    vec3 rimColor  = vec3(0.85, 0.92, 1.00);

    float fill = max(dot(N, fillDir), 0.0);
    float back = max(dot(N, backDir), 0.0);

    // Hemispheric ambient sky/ground gradient
    vec3 sky    = vec3(0.40, 0.55, 0.75);
    vec3 ground = vec3(0.18, 0.16, 0.14);
    float upDot = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 hemi   = mix(ground, sky, upDot) * 0.35;

    vec3 lit = ambient
             + lightCol * key * lightInt
             + fillColor * fill * 0.25 * attenuation * (type > 0 ? spotEffect : 1.0)
             + backColor * back * 0.15 * attenuation * (type > 0 ? spotEffect : 1.0)
             + hemi;

    // Fresnel rim silhouette accent
    vec3 V = normalize(-vPosWorld);
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    vec3 rim = rimColor * fresnel * 0.45;

    vec3 col = baseColor * lit + rim;

    // Customizable Gamma correction
    col = pow(col, vec3(1.0 / gamma));

    outColor = vec4(col, 1.0);
}

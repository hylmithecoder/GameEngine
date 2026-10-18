#version 450

// Unity-style lighting with Directional, Point, and Spotlight computations,
// plus a customizable lighting gamma.

layout(location = 0) in vec3 vNormalWorld;
layout(location = 1) in vec3 vPosWorld;
layout(location = 2) in vec4 vLightPosOrDir;  // xyz = position/direction, w = intensity
layout(location = 3) in vec4 vLightColorType; // xyz = color, w = type (special -1.0 = emissive/unlit)
layout(location = 4) in vec4 vLightDir;       // xyz = spotlight forward vector, w = unused
layout(location = 5) in vec4 vLightParams;    // x = range, y = spotAngleRad, z = gamma, w = spotCosOuter
layout(location = 6) in vec2 vUV;
layout(location = 7) in vec4 vMaterial;       // xyz = diffuse tint, w = 0 untextured / 1 textured / 2 textured unlit

layout(set = 0, binding = 0) uniform sampler2D albedoTex;

// Same block as scene_mesh.vert; the fragment stage reads the camera and the
// NPR (toon) settings from it.
layout(set = 1, binding = 0) uniform Scene {
    mat4 viewProj;
    vec4 lightPosOrDir;
    vec4 lightColorType;
    vec4 lightDir;
    vec4 lightParams;
    vec4 cameraPos;      // xyz = eye, w = 1 when NPR lighting is on
    vec4 npr;            // x = bands, y = softness, z = specular size, w = specular strength
    vec4 nprShadow;      // rgb = shadow colour, w = rim width
    vec4 nprRim;         // rgb = rim colour, w = rim strength
    vec4 nprOutline;
} scene;

// Quantize 0..1 light into `bands` flat steps with softened edges.
float ToonStep(float x, float bands, float softness) {
    float scaled = clamp(x, 0.0, 1.0) * bands;
    float band = floor(scaled);
    float edge = smoothstep(0.5 - softness, 0.5 + softness, scaled - band);
    return clamp((band + edge) / bands, 0.0, 1.0);
}

layout(location = 0) out vec4 outColor;

void main() {
    // Font-atlas glyph coverage. Unlike Canvas text this is a depth-tested,
    // transformed world surface. Empty texels must not occlude the world.
    if (vMaterial.w > 3.5) {
        float alpha = texture(albedoTex, vUV).a * (vMaterial.w - 4.0);
        if (alpha < 0.01) discard;
        outColor = vec4(vMaterial.rgb, alpha);
        return;
    }
    vec3 N = normalize(vNormalWorld);
    if (!gl_FrontFacing) N = -N;

    // Unlit textured surface (video screens): the frame as-is.
    if (vMaterial.w > 1.5) {
        outColor = vec4(texture(albedoTex, vUV).rgb * vMaterial.xyz, 1.0);
        return;
    }

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
    vec3 V = normalize(scene.cameraPos.xyz - vPosWorld);

    vec3 baseColor = vMaterial.xyz;
    if (vMaterial.w >= 0.5)
        baseColor *= texture(albedoTex, vUV).rgb;

    if (scene.cameraPos.w > 0.5) {
        // Non-photorealistic (cel / toon) lighting: flat light bands between
        // a tinted shadow colour and the light colour, a hard specular spot
        // and a rim on the silhouette. Ink outlines are a separate pass.
        //
        // Two-sided by view direction, not gl_FrontFacing: the offscreen
        // target is Y-flipped and imported models mix windings, so the
        // facing bit cannot be trusted here.
        N = normalize(vNormalWorld);
        if (dot(N, V) < 0.0) N = -N;
        key = max(dot(N, L), 0.0) * attenuation * spotEffect;
        float lit = ToonStep(key, scene.npr.x, scene.npr.y);
        vec3 shade = mix(scene.nprShadow.rgb, lightCol * max(lightInt, 0.0), lit);
        vec3 col = baseColor * shade;

        vec3 H = normalize(L + V);
        float spec = smoothstep(1.0 - scene.npr.z - 0.005, 1.0 - scene.npr.z + 0.005,
                                max(dot(N, H), 0.0));
        col += lightCol * spec * scene.npr.w * step(0.001, scene.npr.z) * step(0.5, lit);

        float facing = 1.0 - max(dot(N, V), 0.0);
        float rimWidth = scene.nprShadow.w;
        float rim = smoothstep(1.0 - rimWidth, 1.0 - rimWidth + 0.04, facing);
        // Grazing floors and ceilings are not silhouettes; keep the rim on
        // side-facing surfaces (character edges, walls).
        rim *= smoothstep(0.9, 0.6, abs(N.y));
        col += scene.nprRim.rgb * rim * scene.nprRim.w * step(0.001, rimWidth);

        outColor = vec4(pow(max(col, 0.0), vec3(1.0 / gamma)), 1.0);
        return;
    }

    // Complementary ambient and fill lights (derived from primary light direction L)
    vec3 fillDir  = normalize(vec3(-L.x, L.y * 0.5, -L.z));
    vec3 backDir  = normalize(vec3(L.x * 0.2, -L.y * 0.5, -L.z));

    vec3 fillColor = vec3(0.55, 0.65, 0.85);
    vec3 backColor = vec3(0.30, 0.30, 0.40);
    vec3 ambient   = vec3(0.16, 0.17, 0.20);
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
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    vec3 rim = rimColor * fresnel * 0.45;

    vec3 col = baseColor * lit + rim;

    // Customizable Gamma correction
    col = pow(col, vec3(1.0 / gamma));

    outColor = vec4(col, 1.0);
}

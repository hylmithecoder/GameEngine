#version 450
// Bloom downsample (13-tap, Jimenez 2014). The first pass (prefilter = 1)
// also keeps only what is brighter than the threshold (soft knee) and uses a
// Karis average so single bright pixels do not flicker.
layout(set = 0, binding = 0) uniform sampler2D src;
layout(push_constant) uniform PC {
  vec4 texel;  // xy = 1 / source size, z = threshold, w = knee
  vec4 params; // x = prefilter (0/1)
  vec4 tint;
} pc;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

float Brightness(vec3 c) { return max(c.r, max(c.g, c.b)); }
vec3 Threshold(vec3 c) {
  float br = Brightness(c);
  float knee = pc.texel.w;
  float soft = clamp(br - pc.texel.z + knee, 0.0, 2.0 * knee);
  soft = soft * soft / (4.0 * knee + 1e-5);
  float contribution = max(soft, br - pc.texel.z) / max(br, 1e-5);
  return c * contribution;
}
float KarisWeight(vec3 c) { return 1.0 / (1.0 + Brightness(c)); }

void main() {
  vec2 t = pc.texel.xy;
  vec3 a = texture(src, vUv + t * vec2(-2, -2)).rgb;
  vec3 b = texture(src, vUv + t * vec2( 0, -2)).rgb;
  vec3 c = texture(src, vUv + t * vec2( 2, -2)).rgb;
  vec3 d = texture(src, vUv + t * vec2(-2,  0)).rgb;
  vec3 e = texture(src, vUv).rgb;
  vec3 f = texture(src, vUv + t * vec2( 2,  0)).rgb;
  vec3 g = texture(src, vUv + t * vec2(-2,  2)).rgb;
  vec3 h = texture(src, vUv + t * vec2( 0,  2)).rgb;
  vec3 i = texture(src, vUv + t * vec2( 2,  2)).rgb;
  vec3 j = texture(src, vUv + t * vec2(-1, -1)).rgb;
  vec3 k = texture(src, vUv + t * vec2( 1, -1)).rgb;
  vec3 l = texture(src, vUv + t * vec2(-1,  1)).rgb;
  vec3 m = texture(src, vUv + t * vec2( 1,  1)).rgb;

  vec3 g0 = (j + k + l + m) * 0.25;
  vec3 g1 = (a + b + d + e) * 0.25;
  vec3 g2 = (b + c + e + f) * 0.25;
  vec3 g3 = (d + e + g + h) * 0.25;
  vec3 g4 = (e + f + h + i) * 0.25;
  vec3 color;
  if (pc.params.x > 0.5) {
    g0 = Threshold(g0); g1 = Threshold(g1); g2 = Threshold(g2);
    g3 = Threshold(g3); g4 = Threshold(g4);
    float w0 = KarisWeight(g0) * 0.5, w1 = KarisWeight(g1) * 0.125,
          w2 = KarisWeight(g2) * 0.125, w3 = KarisWeight(g3) * 0.125,
          w4 = KarisWeight(g4) * 0.125;
    color = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) /
            max(w0 + w1 + w2 + w3 + w4, 1e-5);
  } else {
    color = g0 * 0.5 + (g1 + g2 + g3 + g4) * 0.125;
  }
  outColor = vec4(max(color, vec3(0.0)), 1.0);
}

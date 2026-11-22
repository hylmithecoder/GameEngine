#version 450
// Bloom upsample (3x3 tent) added onto the next larger level, and the final
// composite added onto the game image. Blending (ONE, ONE) does the adding.
layout(set = 0, binding = 0) uniform sampler2D src;
layout(push_constant) uniform PC {
  vec4 texel;  // xy = 1 / source size
  vec4 params; // x = weight
  vec4 tint;   // rgb
} pc;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

void main() {
  vec2 t = pc.texel.xy;
  vec3 s = texture(src, vUv + t * vec2(-1, -1)).rgb;
  s += texture(src, vUv + t * vec2( 0, -1)).rgb * 2.0;
  s += texture(src, vUv + t * vec2( 1, -1)).rgb;
  s += texture(src, vUv + t * vec2(-1,  0)).rgb * 2.0;
  s += texture(src, vUv).rgb * 4.0;
  s += texture(src, vUv + t * vec2( 1,  0)).rgb * 2.0;
  s += texture(src, vUv + t * vec2(-1,  1)).rgb;
  s += texture(src, vUv + t * vec2( 0,  1)).rgb * 2.0;
  s += texture(src, vUv + t * vec2( 1,  1)).rgb;
  outColor = vec4(s * (1.0 / 16.0) * pc.params.x * pc.tint.rgb, 0.0);
}

import * as THREE from 'three';

// APEX background: chalky animated ocean. Replaces the outer world when
// the player reaches TIER_COUNT — "you are the biggest thing in the sea."
// Cream base (matching the board paper) with slow drifting chalky wave
// strokes layered on top via a low-freq sin-sum + subtle fbm, tinted
// toward a cold blue-green. Fullscreen NDC quad, GLSL ES 3.00.

const vert = /* glsl */ `
  out vec2 v_uv;
  void main() {
    v_uv = uv;
    gl_Position = vec4(position.xy, 0.0, 1.0);
  }
`;

const frag = /* glsl */ `
  precision highp float;
  in vec2 v_uv;
  out vec4 pc_fragColor;

  uniform vec2  u_resolution;
  uniform float u_time;

  float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
  }
  float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash12(i);
    float b = hash12(i + vec2(1,0));
    float c = hash12(i + vec2(0,1));
    float d = hash12(i + vec2(1,1));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(a,b,u.x), mix(c,d,u.x), u.y);
  }
  float fbm(vec2 p) {
    float v = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 4; ++i) {
      v += amp * vnoise(p);
      p = p * 2.03 + vec2(9.1, 4.7);
      amp *= 0.55;
    }
    return v;
  }

  void main() {
    vec2 pix = gl_FragCoord.xy;
    vec2 uv  = pix / u_resolution.y;

    // Cream paper base tinted slightly toward cold sea.
    vec3 cream = vec3(0.95, 0.93, 0.85);
    vec3 sea   = vec3(0.78, 0.88, 0.90);
    float tint = 0.22;
    vec3 base  = mix(cream, sea, tint);

    // Slow drifting wave strokes — sum of sins at low frequency + fbm
    // warp. The abs() produces chalky crests; smoothstep sharpens them.
    vec2 q = vec2(fbm(uv * 1.2 + vec2(u_time * 0.015, 0.0)),
                  fbm(uv * 1.2 + vec2(0.0, u_time * 0.02)));
    float wave = 0.0;
    wave += sin(uv.y * 5.0 + u_time * 0.25 + q.x * 2.5);
    wave += 0.7 * sin(uv.y * 9.0 - u_time * 0.18 + q.y * 3.0);
    wave += 0.5 * sin((uv.x + uv.y) * 7.5 + u_time * 0.12 + q.x * q.y * 4.0);
    float crest = smoothstep(0.55, 1.0, abs(wave) * 0.45);

    // Chalky stroke color — slight blue-green, feathered by a hash
    // "grit" so it reads as hand-drawn.
    vec3 stroke = vec3(0.56, 0.74, 0.78);
    float grit  = hash12(floor(pix * 0.6));
    float strokeA = crest * mix(0.55, 0.95, grit);
    base = mix(base, stroke, strokeA * 0.55);

    // Gentle vignette to match board.
    vec2 ndc  = (pix / u_resolution) * 2.0 - 1.0;
    float rad = length(ndc * vec2(u_resolution.x / u_resolution.y, 1.0));
    float vigs = smoothstep(0.8, 1.6, rad);
    base = mix(base, vec3(0.78, 0.82, 0.85), vigs * 0.30);

    // Paper tooth flecks.
    float tooth = hash12(floor(pix));
    base += (tooth - 0.5) * 0.012;

    pc_fragColor = vec4(clamp(base, vec3(0.0), vec3(1.0)), 1.0);
  }
`;

export function createOceanMaterial(): THREE.ShaderMaterial {
  return new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3,
    vertexShader: vert,
    fragmentShader: frag,
    uniforms: {
      u_resolution: { value: new THREE.Vector2(1, 1) },
      u_time: { value: 0 }
    },
    depthTest: false,
    depthWrite: false
  });
}

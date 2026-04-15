import * as THREE from 'three';

// Port of src/CellCraft/shaders/cell_body.frag. Membrane-to-cytoplasm
// gradient (driven by per-vertex inset distance), value-noise speckle,
// diet tint. Consumed by the cell fan-triangulated mesh in renderer.ts.
//
// Divergences from 4.10 → ES 3.00:
// - Explicit `out vec4 pc_fragColor`.
// - Attribute inputs `a_inset`, `a_uv` declared as `in` (ES 3.00 syntax,
//   same as the core profile source).

const vert = /* glsl */ `
  in float a_inset;
  in vec2  a_uv;
  out float v_inset;
  out vec2  v_uv;

  void main() {
    v_inset = a_inset;
    v_uv    = a_uv;
    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
  }
`;

const frag = /* glsl */ `
  precision highp float;
  in float v_inset;
  in vec2  v_uv;
  out vec4 pc_fragColor;

  uniform vec3  u_base_color;
  uniform vec3  u_diet_color;
  uniform float u_noise_seed;
  uniform float u_time;
  uniform float u_diet_mix;
  uniform float u_alpha_scale;

  float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
  }
  float valueNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(a,b,u.x), mix(c,d,u.x), u.y);
  }

  void main() {
    vec3 organism = mix(u_base_color, u_diet_color, u_diet_mix);
    vec3 cytoplasm = organism * 1.15;
    vec3 membrane  = organism * 0.55;
    float t = clamp(v_inset, 0.0, 1.0);
    float membrane_width = 0.22;
    float w = smoothstep(0.0, membrane_width, t);
    vec3 color = mix(membrane, cytoplasm, w);

    float n1 = valueNoise(v_uv * 3.0 + vec2(u_noise_seed));
    float n2 = valueNoise(v_uv * 8.0 + vec2(u_noise_seed * 2.3)) * 0.5;
    float noise = (n1 + n2) / 1.5 - 0.5;
    color *= (1.0 + noise * 0.15);

    float shimmer = 0.04 * sin(u_time * 1.5 + v_uv.x * 6.0 + u_noise_seed);
    color += vec3(shimmer) * w;

    float alpha = smoothstep(0.0, 0.1, t) * 0.92 * u_alpha_scale;
    pc_fragColor = vec4(color, alpha);
  }
`;

export interface CellFillParams {
  baseColor: THREE.ColorRepresentation;
  dietColor: THREE.ColorRepresentation;
  noiseSeed: number;
  dietMix?: number;
  alphaScale?: number;
}

export function createCellFillMaterial(p: CellFillParams): THREE.ShaderMaterial {
  return new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3,
    vertexShader: vert,
    fragmentShader: frag,
    uniforms: {
      u_base_color: { value: new THREE.Color(p.baseColor) },
      u_diet_color: { value: new THREE.Color(p.dietColor) },
      u_noise_seed: { value: p.noiseSeed },
      u_time: { value: 0 },
      u_diet_mix: { value: p.dietMix ?? 0.7 },
      u_alpha_scale: { value: p.alphaScale ?? 1.0 }
    },
    transparent: true,
    depthTest: false,
    depthWrite: false
  });
}

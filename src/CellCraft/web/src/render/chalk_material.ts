import * as THREE from 'three';

// Port of src/CellCraft/shaders/chalk.frag — feathered, grit-noised chalk
// ribbon. We build the ribbon geometry on the CPU (see renderer.ts); the
// shader consumes per-vertex `a_across` (-1..+1 across width) and
// `a_along` (pixels from stroke start).
//
// Divergences from 4.10 → ES 3.00:
// - `in` on attributes is valid in both; no change.
// - Explicit `out vec4 pc_fragColor`.

const vert = /* glsl */ `
  in float a_across;
  in float a_along;
  out float v_across;
  out float v_along;

  void main() {
    v_across = a_across;
    v_along  = a_along;
    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
  }
`;

const frag = /* glsl */ `
  precision highp float;
  in float v_across;
  in float v_along;
  out vec4 pc_fragColor;

  uniform vec3  u_color;
  uniform float u_alpha;

  float hash11(float x) {
    return fract(sin(x * 12.9898) * 43758.5453);
  }
  float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
  }

  void main() {
    float a_edge = 1.0 - smoothstep(0.45, 1.0, abs(v_across));
    if (a_edge <= 0.0) discard;

    float n1 = hash11(floor(v_along * 0.8));
    float n2 = hash21(vec2(floor(v_along * 0.22), floor(v_across * 3.0 + 2.0)));
    float grit = mix(n1, n2, 0.45);

    float a_grit = mix(0.75, 1.0, smoothstep(0.18, 0.65, grit));
    vec3 col = u_color + (grit - 0.5) * 0.06;
    col = mix(col * 0.85, col, a_grit);

    pc_fragColor = vec4(col, a_edge * a_grit * u_alpha);
  }
`;

export function createChalkMaterial(color: THREE.ColorRepresentation = 0x2b2b2b): THREE.ShaderMaterial {
  const mat = new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3,
    vertexShader: vert,
    fragmentShader: frag,
    uniforms: {
      u_color: { value: new THREE.Color(color) },
      u_alpha: { value: 1.0 }
    },
    transparent: true,
    depthTest: false,
    depthWrite: false
  });
  return mat;
}

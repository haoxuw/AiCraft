import * as THREE from 'three';
import { EffectComposer } from 'three/examples/jsm/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/examples/jsm/postprocessing/RenderPass.js';
import { ShaderPass } from 'three/examples/jsm/postprocessing/ShaderPass.js';
import { UnrealBloomPass } from 'three/examples/jsm/postprocessing/UnrealBloomPass.js';

// Post-FX chain for CellCraft web:
//   RenderPass → UnrealBloom (half-res, subtle) → [Vignette + Low-HP pulse] merged
// Bloom is already blurred, so rendering it at half resolution saves ~75%
// of its fill-rate cost with no perceptible visual hit.
// Vignette and the low-HP red pulse have been merged into a single final
// ShaderPass — one draw, same effect.

// ---- Merged final pass: vignette + low-HP ring pulse ----
const FINAL_SHADER = {
  uniforms: {
    tDiffuse: { value: null as THREE.Texture | null },
    u_vignette_strength: { value: 0.25 },
    u_vignette_softness: { value: 0.55 },
    u_low_hp: { value: 0.0 },
    u_time: { value: 0.0 }
  },
  vertexShader: /* glsl */ `
    varying vec2 vUv;
    void main() {
      vUv = uv;
      gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
    }
  `,
  fragmentShader: /* glsl */ `
    precision highp float;
    varying vec2 vUv;
    uniform sampler2D tDiffuse;
    uniform float u_vignette_strength;
    uniform float u_vignette_softness;
    uniform float u_low_hp;
    uniform float u_time;

    void main() {
      vec4 col = texture2D(tDiffuse, vUv);
      vec2 p = vUv - 0.5;
      float d2 = dot(p, p);

      // Vignette — darken corners.
      float vig = smoothstep(u_vignette_softness, 0.0, 0.5 - d2);
      vig = clamp(vig, 0.0, 1.0);
      col.rgb *= mix(1.0, 1.0 - u_vignette_strength, vig);

      // Low-HP red ring pulse (skipped when u_low_hp == 0).
      if (u_low_hp > 0.0001) {
        float d = sqrt(d2);
        float ring = smoothstep(0.25, 0.55, d);
        float pulse = 0.55 + 0.45 * (0.5 + 0.5 * sin(u_time * 6.2831 * 2.0));
        float a = ring * pulse * u_low_hp;
        vec3 red = vec3(0.95, 0.18, 0.22);
        col.rgb = mix(col.rgb, red, a * 0.65);
      }
      gl_FragColor = col;
    }
  `
};

export interface PostFX {
  composer: EffectComposer;
  setSize(w: number, h: number, pixelRatio: number): void;
  setLowHp(v: number): void;
  setTime(t: number): void;
  render(): void;
  renderPass: RenderPass;
}

export function createPostFX(
  gl: THREE.WebGLRenderer,
  scene: THREE.Scene,
  camera: THREE.Camera
): PostFX {
  const size = gl.getSize(new THREE.Vector2());
  const composer = new EffectComposer(gl);
  composer.setPixelRatio(gl.getPixelRatio());
  composer.setSize(size.x, size.y);

  const renderPass = new RenderPass(scene, camera);
  renderPass.clear = true;
  composer.addPass(renderPass);

  // Half-res bloom: compute on a half-size target and let UnrealBloomPass's
  // internal upsample blend it back. UnrealBloomPass already builds a
  // mip chain of blurred targets, so driving the "resolution" vector at
  // half gives a ~4× fill-rate win on its most expensive passes.
  const bloomRes = new THREE.Vector2(Math.max(1, size.x >> 1), Math.max(1, size.y >> 1));
  const bloom = new UnrealBloomPass(bloomRes, 0.35, 0.5, 0.85);
  composer.addPass(bloom);

  const finalPass = new ShaderPass(FINAL_SHADER);
  finalPass.renderToScreen = true;
  composer.addPass(finalPass);

  return {
    composer,
    renderPass,
    setSize(w, h, pixelRatio) {
      composer.setPixelRatio(pixelRatio);
      composer.setSize(w, h);
      bloom.setSize(Math.max(1, w >> 1), Math.max(1, h >> 1));
    },
    setLowHp(v) {
      finalPass.uniforms.u_low_hp.value = Math.max(0, Math.min(1, v));
    },
    setTime(t) {
      finalPass.uniforms.u_time.value = t;
    },
    render() {
      composer.render();
    }
  };
}

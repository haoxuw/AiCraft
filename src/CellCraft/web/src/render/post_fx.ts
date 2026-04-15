import * as THREE from 'three';
import { EffectComposer } from 'three/examples/jsm/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/examples/jsm/postprocessing/RenderPass.js';
import { ShaderPass } from 'three/examples/jsm/postprocessing/ShaderPass.js';
import { UnrealBloomPass } from 'three/examples/jsm/postprocessing/UnrealBloomPass.js';

// Post-FX chain for CellCraft web:
//   RenderPass → UnrealBloom (subtle) → Vignette → Low-HP red pulse
// Everything composited on top of the base renderer output. The bloom
// picks up chalk highlights + snowflake tips; vignette anchors the
// cream paper feel; low-HP pulse is driven by u_low_hp from gameplay.

// ---- Vignette: darken the corners toward cream ----
const VIGNETTE_SHADER = {
  uniforms: {
    tDiffuse: { value: null as THREE.Texture | null },
    u_strength: { value: 0.25 },
    u_softness: { value: 0.55 }
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
    uniform float u_strength;
    uniform float u_softness;

    void main() {
      vec4 col = texture2D(tDiffuse, vUv);
      vec2 p = vUv - 0.5;
      float d = dot(p, p);
      // Smoothly falls off toward edges. d ranges ~0 (center) to ~0.5 (corner).
      float vig = smoothstep(u_softness, 0.0, 0.5 - d);
      vig = clamp(vig, 0.0, 1.0);
      // Multiply luminance down by u_strength at the corners.
      col.rgb *= mix(1.0, 1.0 - u_strength, vig);
      gl_FragColor = col;
    }
  `
};

// ---- Low-HP red ring pulse ----
// u_low_hp: 0 = off, 1 = full intensity. Pulses at ~2Hz.
const LOW_HP_SHADER = {
  uniforms: {
    tDiffuse: { value: null as THREE.Texture | null },
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
    uniform float u_low_hp;
    uniform float u_time;

    void main() {
      vec4 col = texture2D(tDiffuse, vUv);
      if (u_low_hp <= 0.0001) {
        gl_FragColor = col;
        return;
      }
      vec2 p = vUv - 0.5;
      float d = length(p);
      // Ring: bright near edges, fades toward center.
      float ring = smoothstep(0.25, 0.55, d);
      // Pulse at 2Hz, amplitude in [0.55, 1.0].
      float pulse = 0.55 + 0.45 * (0.5 + 0.5 * sin(u_time * 6.2831 * 2.0));
      float a = ring * pulse * u_low_hp;
      vec3 red = vec3(0.95, 0.18, 0.22);
      // Screen-blend-ish overlay.
      col.rgb = mix(col.rgb, red, a * 0.65);
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

  const bloom = new UnrealBloomPass(new THREE.Vector2(size.x, size.y), 0.35, 0.5, 0.85);
  composer.addPass(bloom);

  const vignette = new ShaderPass(VIGNETTE_SHADER);
  composer.addPass(vignette);

  const lowHp = new ShaderPass(LOW_HP_SHADER);
  lowHp.renderToScreen = true;
  composer.addPass(lowHp);

  return {
    composer,
    renderPass,
    setSize(w, h, pixelRatio) {
      composer.setPixelRatio(pixelRatio);
      composer.setSize(w, h);
      bloom.setSize(w, h);
    },
    setLowHp(v) {
      lowHp.uniforms.u_low_hp.value = Math.max(0, Math.min(1, v));
    },
    setTime(t) {
      lowHp.uniforms.u_time.value = t;
    },
    render() {
      composer.render();
    }
  };
}

import * as THREE from 'three';

// Fullscreen cream-colored fade with an animated chalk-smudge noise mask.
// Used between major scenes (Main Menu ↔ Lab ↔ Match). Independent of
// the post-FX composer: drawn as its own overlay on top of everything
// via a dedicated scene + ortho camera.

const FADE_VERT = /* glsl */ `
  out vec2 v_uv;
  void main() {
    v_uv = uv;
    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
  }
`;

const FADE_FRAG = /* glsl */ `
  precision highp float;
  in vec2 v_uv;
  out vec4 pc_fragColor;

  uniform vec3  u_color;
  uniform float u_progress; // 0 = fully transparent, 1 = fully opaque
  uniform float u_time;
  uniform vec2  u_resolution;

  // Value noise — cheap and enough for chalk smudges.
  float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
  }
  float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
  }
  float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; ++i) {
      v += a * vnoise(p);
      p *= 2.0;
      a *= 0.5;
    }
    return v;
  }

  void main() {
    vec2 p = v_uv * (u_resolution / 160.0);
    // Drifting smudge: two fbm layers moving in opposite directions.
    float n = fbm(p + vec2(u_time * 0.05, -u_time * 0.03));
    n = mix(n, fbm(p * 1.7 - vec2(u_time * 0.02, u_time * 0.04)), 0.5);

    // Mask: progress gates reveal from noisy edges inward.
    // When u_progress < 1, only areas where noise is below a threshold
    // are opaque. At u_progress=1 everything is opaque.
    float threshold = u_progress * 1.25;
    float mask = smoothstep(threshold - 0.15, threshold + 0.05, 1.0 - n * 0.6 + u_progress * 0.4);
    mask = clamp(mask, 0.0, 1.0);
    float alpha = mask * u_progress;

    // Subtle tonal variation — faint chalk streaks in the cream.
    vec3 col = u_color + (n - 0.5) * 0.025;
    pc_fragColor = vec4(col, alpha);
  }
`;

export interface SceneFaderOpts {
  color?: THREE.ColorRepresentation;
}

// The fader owns a plain Three.js scene that callers must render after
// their composer. See render() for the typical hookup.
export class SceneFader {
  readonly scene = new THREE.Scene();
  readonly camera: THREE.OrthographicCamera;
  private mesh: THREE.Mesh;
  private mat: THREE.ShaderMaterial;
  private progress = 0;
  private animStart = 0;
  private animDuration = 0;
  private animFrom = 0;
  private animTo = 0;
  private animResolve: (() => void) | null = null;

  constructor(opts: SceneFaderOpts = {}) {
    this.camera = new THREE.OrthographicCamera(-1, 1, 1, -1, -100, 100);
    this.camera.position.set(0, 0, 10);

    const geom = new THREE.PlaneGeometry(2, 2);
    this.mat = new THREE.ShaderMaterial({
      glslVersion: THREE.GLSL3,
      vertexShader: FADE_VERT,
      fragmentShader: FADE_FRAG,
      uniforms: {
        u_color: { value: new THREE.Color(opts.color ?? 0xf2ecd8) },
        u_progress: { value: 0.0 },
        u_time: { value: 0.0 },
        u_resolution: { value: new THREE.Vector2(1, 1) }
      },
      transparent: true,
      depthTest: false,
      depthWrite: false
    });
    this.mesh = new THREE.Mesh(geom, this.mat);
    this.mesh.frustumCulled = false;
    this.scene.add(this.mesh);
  }

  setResolution(w: number, h: number): void {
    this.mat.uniforms.u_resolution.value.set(w, h);
  }

  // Call once per frame with current time (seconds). Ticks any active
  // animation and updates u_progress.
  tick(time: number): void {
    this.mat.uniforms.u_time.value = time;
    if (this.animResolve) {
      const t = (time - this.animStart) / Math.max(1e-6, this.animDuration);
      if (t >= 1.0) {
        this.progress = this.animTo;
        this.mat.uniforms.u_progress.value = this.progress;
        const done = this.animResolve;
        this.animResolve = null;
        done();
      } else {
        // Smoothstep easing.
        const s = t * t * (3.0 - 2.0 * t);
        this.progress = this.animFrom + (this.animTo - this.animFrom) * s;
        this.mat.uniforms.u_progress.value = this.progress;
      }
    }
  }

  // Render the overlay. Caller is responsible for invoking this AFTER
  // their main render/compositor so the fade draws on top.
  render(gl: THREE.WebGLRenderer): void {
    if (this.progress <= 0.0001 && !this.animResolve) return;
    gl.autoClear = false;
    gl.render(this.scene, this.camera);
  }

  isAnimating(): boolean {
    return this.animResolve !== null;
  }

  private animateTo(target: number, durationMs: number, nowSec: number): Promise<void> {
    // Cancel any in-flight tween by resolving it immediately.
    if (this.animResolve) {
      const r = this.animResolve;
      this.animResolve = null;
      r();
    }
    this.animFrom = this.progress;
    this.animTo = target;
    this.animStart = nowSec;
    this.animDuration = durationMs / 1000;
    return new Promise<void>((resolve) => {
      this.animResolve = resolve;
    });
  }

  // Progress 0 → 1: paper sweeps in and covers the screen.
  fadeOut(durationMs: number, nowSec: number): Promise<void> {
    return this.animateTo(1.0, durationMs, nowSec);
  }
  // Progress 1 → 0: paper sweeps out to reveal the new scene.
  fadeIn(durationMs: number, nowSec: number): Promise<void> {
    return this.animateTo(0.0, durationMs, nowSec);
  }
}

import * as THREE from 'three';
import { Input } from '../input/input';
import { Renderer } from '../render/renderer';

// Scene contract for the web client. Scenes own all their own Object3Ds
// and attach/detach them to ctx.renderer's main scene and/or HUD scene.
// The SceneManager drives lifecycle + rendering; scenes only express
// update/draw intent.
//
// Scenes render in two layers:
//   - world layer: renderer.scene (orthographic world camera, post-FX)
//   - HUD layer: renderer.hudScene (pixel-space overlay, no post-FX)
// A scene can use one, both, or neither.

export interface SceneCtx {
  renderer: Renderer;
  input: Input;
  now: number; // seconds, wall time
  // Requested by the active scene when it wants to transition; the
  // manager owns the actual transition animation + exit/enter calls.
  requestGoto(factory: SceneFactory, opts?: { fade?: boolean; fadeOutMs?: number; fadeInMs?: number }): void;
}

export interface Scene {
  // One-shot attach: add groups to renderer scenes, wire listeners.
  enter(ctx: SceneCtx): void;
  // Detach everything added in enter(); remove listeners.
  exit(ctx: SceneCtx): void;
  // Per-frame logic.
  update(dt: number, ctx: SceneCtx): void;
  // Per-frame render hook. Most scenes can be a no-op (renderer.render()
  // handles the world; hudScene is drawn automatically). Use this only
  // when a scene needs custom passes.
  render?(ctx: SceneCtx): void;
  // Optional input hooks for scene-specific handling (menus, pause).
  onKey?(e: KeyboardEvent, ctx: SceneCtx): void;
  onPointer?(e: PointerEvent, ctx: SceneCtx): void;
}

export type SceneFactory = () => Scene;

// Per-scene slide+fade in. Replaces the old fullscreen SceneFader —
// instead of washing the screen with cream between scenes, each scene
// lightly animates its own top-level UI group. Keeps transitions snappy
// and less jarring on a chalkboard.
//
// Usage: keep a number `enterT` initialized to 0 in your scene closure;
// call tickEnter(dt, state) every update(); then lerp the group position
// and a shared opacity from enterT.
export const ENTER_DURATION_S = 0.18;

// Advance a scene's enter cursor from 0→1 over ENTER_DURATION_S.
export function advanceEnter(prev: number, dt: number): number {
  return Math.min(1, prev + dt / ENTER_DURATION_S);
}

// Walk a group and set opacity on any transparent material we find.
// Opaque materials (e.g. the chalkboard) are left alone.
export function applyEnterOpacity(group: THREE.Object3D, t: number): void {
  group.traverse((obj) => {
    const mesh = obj as THREE.Mesh;
    const mat = mesh.material as THREE.Material | THREE.Material[] | undefined;
    if (!mat) return;
    const apply = (mm: THREE.Material): void => {
      if (!mm.transparent) return;
      const anyMat = mm as unknown as { _enterBaseAlpha?: number };
      const uni = (mm as THREE.ShaderMaterial).uniforms;
      if (uni && uni.u_alpha) {
        if (anyMat._enterBaseAlpha === undefined) {
          anyMat._enterBaseAlpha = uni.u_alpha.value as number;
        }
        uni.u_alpha.value = (anyMat._enterBaseAlpha as number) * t;
      } else if ('opacity' in mm) {
        if (anyMat._enterBaseAlpha === undefined) {
          anyMat._enterBaseAlpha = (mm as THREE.MeshBasicMaterial).opacity;
        }
        (mm as THREE.MeshBasicMaterial).opacity = (anyMat._enterBaseAlpha as number) * t;
      }
    };
    if (Array.isArray(mat)) mat.forEach(apply); else apply(mat);
  });
}

// Utility: dispose every Mesh under a group (geometry + materials) and
// clear it. Scenes use this in exit() to avoid GPU leaks.
export function disposeGroup(group: THREE.Object3D): void {
  group.traverse((obj) => {
    const mesh = obj as THREE.Mesh;
    if (mesh.geometry) mesh.geometry.dispose();
    const mat = mesh.material as THREE.Material | THREE.Material[] | undefined;
    if (Array.isArray(mat)) mat.forEach((m) => m.dispose());
    else if (mat) mat.dispose();
    // troika Text has its own .dispose
    const trk = obj as unknown as { dispose?: () => void };
    if (typeof trk.dispose === 'function' && !(mesh.geometry || mat)) trk.dispose();
  });
  if (group.parent) group.parent.remove(group);
}

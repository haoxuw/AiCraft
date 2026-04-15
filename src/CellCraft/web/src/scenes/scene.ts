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

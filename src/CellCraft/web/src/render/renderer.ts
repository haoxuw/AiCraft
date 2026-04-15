import * as THREE from 'three';
import { Diet, Monster, Vec2 } from '../sim/monster';
import { PartKind } from '../sim/part';
import { Food, FoodKind, World } from '../sim/world';
import { createBoardMaterial } from './board_material';
import { createChalkMaterial } from './chalk_material';
import { createCellFillMaterial } from './cell_fill_material';
import { createOceanMaterial } from './ocean_material';
import { createPostFX, PostFX } from './post_fx';
import { RenderPass } from 'three/examples/jsm/postprocessing/RenderPass.js';
import { SceneFader } from './transitions';
import * as perf from '../perf';

// Rendering lives on a plain XY plane. World units = screen pixels at zoom
// 1. The orthographic camera is sized to cover the arena with padding.

const CHALK_INK = new THREE.Color(0x2b2b2b);
const ARENA_RING = new THREE.Color(0x3b4a52);

// Dual-sim background tunables — match native app.cpp exactly.
export const BG_ZOOM = 0.32;
export const BG_PARALLAX = 0.12;
export const BG_DIM = 0.42;
export const BG_CREAM_MIX = 0.40;
const BG_CREAM = new THREE.Color(0.97, 0.95, 0.88);

export interface DualRenderOpts {
  zoom?: number;
  parallax?: number;
  dim?: number;
  creamMix?: number;
  alphaScale?: number;
}

interface ShadeOpts {
  dim: number;      // 1.0 = no dim
  creamMix: number; // 0 = no cream blend
  alphaScale: number;
  noRing: boolean;
  noPlayerEmphasis: boolean;
}
const NO_SHADE: ShadeOpts = { dim: 1.0, creamMix: 0.0, alphaScale: 1.0, noRing: false, noPlayerEmphasis: false };

function shadeColor(c: THREE.Color, shade: ShadeOpts): THREE.Color {
  if (shade.dim === 1.0 && shade.creamMix === 0.0) return c;
  const out = c.clone().multiplyScalar(shade.dim);
  return out.lerp(BG_CREAM, shade.creamMix);
}

const DIET_COLOR: Record<Diet, THREE.Color> = {
  [Diet.CARNIVORE]: new THREE.Color(0xef5a6f),
  [Diet.HERBIVORE]: new THREE.Color(0x70c96f),
  [Diet.OMNIVORE]: new THREE.Color(0xb876e8)
};

// Food passes were previously drawn as three tinted strokes (shadow /
// body / highlight). After merging all strokes per food into a single
// mesh we keep only the body tint — the shadow/highlight offsets are
// still visible inside buildFoodGeometry() as positional passes.
const PLANT_GREEN_BODY = new THREE.Color(0x3a8f3a);
const MEAT_GREEN_BODY = new THREE.Color(0x1e6f2e);

// -----------------------------------------------------------------
// Cached mesh pool — monsters and food meshes persist across frames.
// Only rebuilt when shape signature changes (body_scale / parts / tier).
// -----------------------------------------------------------------
interface MonsterCacheEntry {
  root: THREE.Group;
  fillMesh: THREE.Mesh;
  outlineMesh: THREE.Mesh;
  partMeshes: THREE.Mesh[];
  shapeKey: string;   // re-rebuild geometry when this changes
  lastFrame: number;  // prune when stale
}

interface FoodCacheEntry {
  mesh: THREE.Mesh;         // single merged geometry for all strokes
  radiusKey: number;        // bucketed radius — rebuild on change
  lastFrame: number;
}

function monsterShapeKey(m: Monster): string {
  // Geometry depends on shape array (body scale, tier size) and mouth part
  // positions. The shape array is deterministic per (tier, parts). We
  // capture a cheap digest: shape.length, body_scale, part kinds+scales.
  let k = `${m.shape.length}|${m.body_scale.toFixed(3)}`;
  for (const p of m.parts) k += `|${p.kind}:${p.scale.toFixed(2)}:${p.anchor[0].toFixed(1)},${p.anchor[1].toFixed(1)}`;
  return k;
}

export class Renderer {
  readonly scene = new THREE.Scene();
  readonly camera: THREE.OrthographicCamera;
  readonly gl: THREE.WebGLRenderer;

  private boardMat = createBoardMaterial();
  private boardMesh: THREE.Mesh;
  private oceanMat = createOceanMaterial();
  private oceanMesh: THREE.Mesh;
  private dynamicGroup = new THREE.Group();
  // Outer (background) world group — rendered beneath the foreground
  // dynamicGroup with zoom + parallax + cream-mix/dim shading, matching
  // the native dual-sim look. Empty unless a scene calls renderDual().
  private backgroundGroup = new THREE.Group();
  private postFX: PostFX;
  private boardPass: RenderPass;
  // HUD is drawn in screen-space ON TOP of the composer output so text
  // stays pixel-sharp and not affected by bloom/vignette.
  readonly hudScene = new THREE.Scene();
  readonly hudCamera: THREE.OrthographicCamera;
  readonly fader = new SceneFader();

  // Per-world caches (inner, outer).
  private innerMonsterCache = new Map<number, MonsterCacheEntry>();
  private innerFoodCache = new Map<number, FoodCacheEntry>();
  private arenaRingMesh: THREE.Mesh | null = null;
  // Outer world: cheap silhouette layer. One InstancedMesh of unit circles.
  private outerSilhouetteMat: THREE.ShaderMaterial | null = null;
  private outerSilhouette: THREE.InstancedMesh | null = null;
  private outerCapacity = 0;
  private frameNum = 0;

  get domElement(): HTMLCanvasElement { return this.canvas; }

  constructor(private canvas: HTMLCanvasElement) {
    this.gl = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: false });
    // Cap pixel ratio at 1.5 — at 2x on hi-dpi displays postfx fill rate
    // dominates frame time with marginal visual gain on chalk art.
    this.gl.setPixelRatio(Math.min(window.devicePixelRatio, 1.5));
    this.gl.setClearColor(0xf7f2de, 1);

    // Fullscreen board quad (NDC, driven by the material).
    const boardGeom = new THREE.PlaneGeometry(2, 2);
    this.boardMesh = new THREE.Mesh(boardGeom, this.boardMat);
    this.boardMesh.frustumCulled = false;
    this.boardMesh.renderOrder = -1000;
    // Board renders in its own pass (we draw it first, ignoring the main camera).
    const boardScene = new THREE.Scene();
    boardScene.add(this.boardMesh);
    // Ocean is a separate fullscreen quad that, when active, is drawn on
    // TOP of the board in the same pass — fully opaque so it replaces
    // the paper with a chalky animated sea. Used at APEX tier.
    this.oceanMesh = new THREE.Mesh(boardGeom.clone(), this.oceanMat);
    this.oceanMesh.frustumCulled = false;
    this.oceanMesh.renderOrder = -999;
    this.oceanMesh.visible = false;
    boardScene.add(this.oceanMesh);
    this.boardScene = boardScene;

    this.camera = new THREE.OrthographicCamera(-1, 1, 1, -1, -100, 100);
    this.camera.position.set(0, 0, 10);
    this.camera.lookAt(0, 0, 0);

    // HUD camera: units = pixels, origin at screen center, +y up.
    this.hudCamera = new THREE.OrthographicCamera(-1, 1, 1, -1, -100, 100);
    this.hudCamera.position.set(0, 0, 10);
    this.hudCamera.lookAt(0, 0, 0);

    this.scene.add(this.backgroundGroup);
    this.scene.add(this.dynamicGroup);

    // Post-FX chain. The main render pass (scene+camera) is attached by
    // createPostFX; we prepend a board pass that clears first, so the
    // board acts as the background.
    this.postFX = createPostFX(this.gl, this.scene, this.camera);
    this.boardPass = new RenderPass(this.boardScene, this.camera);
    this.boardPass.clear = true;
    // Insert board pass at index 0, ahead of the main render pass.
    this.postFX.composer.insertPass(this.boardPass, 0);
    // Main render pass must not clear (it would wipe the board).
    this.postFX.renderPass.clear = false;

    this.resize();
    window.addEventListener('resize', this.resize);
  }

  setLowHp(v: number): void {
    this.postFX.setLowHp(v);
  }

  // Enable the chalky ocean background (replaces the paper board). Used
  // at APEX tier when there is no outer world anymore.
  setOcean(on: boolean): void {
    this.oceanMesh.visible = on;
  }

  private boardScene: THREE.Scene;

  dispose(): void {
    window.removeEventListener('resize', this.resize);
    this.gl.dispose();
  }

  private resize = (): void => {
    const w = this.canvas.clientWidth || window.innerWidth;
    const h = this.canvas.clientHeight || window.innerHeight;
    this.gl.setSize(w, h, false);
    this.boardMat.uniforms.u_resolution.value.set(w, h);
    this.oceanMat.uniforms.u_resolution.value.set(w, h);
    if (this.postFX) {
      this.postFX.setSize(w, h, this.gl.getPixelRatio());
    }
    this.fader.setResolution(w, h);

    // Fit arena radius * 1.1 into the shorter axis.
    const arena = 1500 * 1.1;
    const aspect = w / h;
    let halfW: number, halfH: number;
    if (aspect > 1) {
      halfH = arena;
      halfW = arena * aspect;
    } else {
      halfW = arena;
      halfH = arena / aspect;
    }
    this.camera.left = -halfW;
    this.camera.right = halfW;
    this.camera.top = halfH;
    this.camera.bottom = -halfH;
    this.camera.updateProjectionMatrix();

    // HUD: 1 world unit = 1 screen pixel, origin at center.
    this.hudCamera.left = -w / 2;
    this.hudCamera.right = w / 2;
    this.hudCamera.top = h / 2;
    this.hudCamera.bottom = -h / 2;
    this.hudCamera.updateProjectionMatrix();
  };

  // Screen (client) coords → world XY.
  screenToWorld(clientX: number, clientY: number): [number, number] {
    const rect = this.canvas.getBoundingClientRect();
    const nx = ((clientX - rect.left) / rect.width) * 2 - 1;
    const ny = -(((clientY - rect.top) / rect.height) * 2 - 1);
    const wx = nx * (this.camera.right - this.camera.left) * 0.5;
    const wy = ny * (this.camera.top - this.camera.bottom) * 0.5;
    return [wx, wy];
  }

  render(world: World, time: number): void {
    this.boardMat.uniforms.u_time.value = time;
    this.frameNum++;
    this.backgroundGroup.visible = false;
    this.syncInnerWorld(world, time, NO_SHADE);
    this.finishFrame(time);
  }

  // Dual-world render: inner (foreground, normal shading) + outer
  // (background, zoomed + parallax + dimmed + cream-mixed). Matches the
  // native client's dual-sim layout (src/CellCraft/client/app.cpp).
  renderDual(
    inner: World,
    outer: World | null,
    cameraWorld: [number, number],
    time: number,
    opts: DualRenderOpts = {}
  ): void {
    const zoom = opts.zoom ?? BG_ZOOM;
    const parallax = opts.parallax ?? BG_PARALLAX;
    const dim = opts.dim ?? BG_DIM;
    const creamMix = opts.creamMix ?? BG_CREAM_MIX;
    const alphaScale = opts.alphaScale ?? 0.35;

    this.boardMat.uniforms.u_time.value = time;
    this.frameNum++;

    if (outer) {
      this.backgroundGroup.scale.set(zoom, zoom, 1);
      this.backgroundGroup.position.set(
        -cameraWorld[0] * parallax,
        -cameraWorld[1] * parallax,
        0
      );
      this.backgroundGroup.visible = true;
      const tOuter = perf.start('r.outer');
      this.syncOuterSilhouette(outer, time, { dim, creamMix, alphaScale });
      perf.end('r.outer', tOuter);
    } else {
      this.backgroundGroup.visible = false;
    }

    const tInner = perf.start('r.inner');
    this.syncInnerWorld(inner, time, NO_SHADE);
    perf.end('r.inner', tInner);
    this.finishFrame(time);
  }

  // Sync the inner (foreground) world into the cached dynamicGroup.
  // Adds missing monsters/food, updates transforms/uniforms, rebuilds
  // geometry only on shape changes, removes entries no longer present.
  private syncInnerWorld(world: World, time: number, shade: ShadeOpts): void {
    // Arena ring (radius is stable per-tier; rebuild only if missing).
    if (!shade.noRing) {
      const needed = world.map_radius;
      if (!this.arenaRingMesh) {
        const ribbon = buildRingRibbon(needed, 128, 4.0);
        const mat = createChalkMaterial(ARENA_RING);
        mat.uniforms.u_alpha.value = 0.6;
        this.arenaRingMesh = new THREE.Mesh(ribbon, mat);
        this.arenaRingMesh.renderOrder = -10;
        this.arenaRingMesh.userData.ringRadius = needed;
        this.dynamicGroup.add(this.arenaRingMesh);
      } else if (this.arenaRingMesh.userData.ringRadius !== needed) {
        this.arenaRingMesh.geometry.dispose();
        this.arenaRingMesh.geometry = buildRingRibbon(needed, 128, 4.0);
        this.arenaRingMesh.userData.ringRadius = needed;
      }
    } else if (this.arenaRingMesh) {
      this.dynamicGroup.remove(this.arenaRingMesh);
      this.arenaRingMesh.geometry.dispose();
      (this.arenaRingMesh.material as THREE.Material).dispose();
      this.arenaRingMesh = null;
    }

    // Monsters.
    for (const m of world.monsters.values()) {
      let e = this.innerMonsterCache.get(m.id);
      const key = monsterShapeKey(m);
      if (!e) {
        e = this.makeMonsterCacheEntry(m, shade);
        this.innerMonsterCache.set(m.id, e);
        this.dynamicGroup.add(e.root);
      } else if (e.shapeKey !== key) {
        this.rebuildMonsterGeometry(e, m);
        e.shapeKey = key;
      }
      // Cheap per-frame: transform + time uniform.
      e.root.position.set(m.core_pos[0], m.core_pos[1], 0);
      e.root.rotation.z = m.heading;
      const fillMat = e.fillMesh.material as THREE.ShaderMaterial;
      fillMat.uniforms.u_time.value = time;
      e.lastFrame = this.frameNum;
    }
    // Prune monsters that weren't seen this frame.
    for (const [id, e] of this.innerMonsterCache) {
      if (e.lastFrame !== this.frameNum) {
        this.dynamicGroup.remove(e.root);
        disposeGroupDeep(e.root);
        this.innerMonsterCache.delete(id);
      }
    }

    // Food.
    for (const f of world.food) {
      let fe = this.innerFoodCache.get(f.id);
      if (!fe) {
        fe = this.makeFoodCacheEntry(f, shade);
        this.innerFoodCache.set(f.id, fe);
        this.dynamicGroup.add(fe.mesh);
      } else if (fe.radiusKey !== Math.round(f.radius)) {
        fe.mesh.geometry.dispose();
        fe.mesh.geometry = buildFoodGeometry(f.radius);
        fe.radiusKey = Math.round(f.radius);
      }
      fe.mesh.position.set(f.pos[0], f.pos[1], 0);
      fe.lastFrame = this.frameNum;
    }
    for (const [id, fe] of this.innerFoodCache) {
      if (fe.lastFrame !== this.frameNum) {
        this.dynamicGroup.remove(fe.mesh);
        fe.mesh.geometry.dispose();
        (fe.mesh.material as THREE.Material).dispose();
        this.innerFoodCache.delete(id);
      }
    }
    perf.countMesh(this.dynamicGroup.children.length);
  }

  // Cheap outer-world silhouette: each monster drawn as a single dim
  // circular blob via InstancedMesh. Food is dropped entirely — the
  // outer world reads as ambient "other cells drifting in the distance."
  private syncOuterSilhouette(
    outer: World,
    _time: number,
    shade: { dim: number; creamMix: number; alphaScale: number }
  ): void {
    const monsters = Array.from(outer.monsters.values()).filter((m) => m.alive);
    const count = monsters.length;

    if (!this.outerSilhouetteMat) {
      this.outerSilhouetteMat = new THREE.ShaderMaterial({
        transparent: true,
        depthWrite: false,
        uniforms: {
          u_tint: { value: new THREE.Color(0.6, 0.6, 0.55) },
          u_alpha: { value: 0.55 }
        },
        vertexShader: /* glsl */ `
          varying vec2 vUv;
          void main() {
            vUv = uv;
            gl_Position = projectionMatrix * modelViewMatrix * instanceMatrix * vec4(position, 1.0);
          }
        `,
        fragmentShader: /* glsl */ `
          precision mediump float;
          varying vec2 vUv;
          uniform vec3 u_tint;
          uniform float u_alpha;
          void main() {
            vec2 p = vUv - 0.5;
            float r = length(p) * 2.0;
            // Soft blob — fade edges to transparent.
            float a = smoothstep(1.0, 0.4, r) * u_alpha;
            if (a < 0.01) discard;
            gl_FragColor = vec4(u_tint, a);
          }
        `
      });
    }
    // Update tint / alpha per shade.
    const tinted = shadeColor(new THREE.Color(0xb0aa96), {
      dim: shade.dim,
      creamMix: shade.creamMix,
      alphaScale: shade.alphaScale,
      noRing: true,
      noPlayerEmphasis: true
    });
    (this.outerSilhouetteMat.uniforms.u_tint.value as THREE.Color).copy(tinted);
    this.outerSilhouetteMat.uniforms.u_alpha.value = 0.55 * shade.alphaScale;

    if (!this.outerSilhouette || count > this.outerCapacity) {
      if (this.outerSilhouette) {
        this.backgroundGroup.remove(this.outerSilhouette);
        this.outerSilhouette.geometry.dispose();
      }
      // Unit quad (2×2 centered) — instance matrix scales to radius.
      const geom = new THREE.PlaneGeometry(2, 2);
      this.outerCapacity = Math.max(16, count * 2);
      this.outerSilhouette = new THREE.InstancedMesh(geom, this.outerSilhouetteMat, this.outerCapacity);
      this.outerSilhouette.frustumCulled = false;
      this.outerSilhouette.renderOrder = -50;
      this.backgroundGroup.add(this.outerSilhouette);
    }

    const tmp = new THREE.Matrix4();
    for (let i = 0; i < count; ++i) {
      const m = monsters[i];
      const r = Math.max(20, m.body_scale * 46);
      tmp.makeScale(r, r, 1);
      tmp.setPosition(m.core_pos[0], m.core_pos[1], 0);
      this.outerSilhouette!.setMatrixAt(i, tmp);
    }
    this.outerSilhouette!.count = count;
    this.outerSilhouette!.instanceMatrix.needsUpdate = true;
  }

  // Renders just the board + HUD + fader (no world content). Scenes
  // that don't own a World (main menu, starter select, end screen)
  // call this each frame. Flushes any stale cache so next world build
  // starts clean.
  renderEmpty(time: number): void {
    this.boardMat.uniforms.u_time.value = time;
    this.frameNum++;
    this.flushWorldCaches();
    this.backgroundGroup.visible = false;
    this.finishFrame(time);
  }

  private flushWorldCaches(): void {
    for (const e of this.innerMonsterCache.values()) {
      this.dynamicGroup.remove(e.root);
      disposeGroupDeep(e.root);
    }
    this.innerMonsterCache.clear();
    for (const fe of this.innerFoodCache.values()) {
      this.dynamicGroup.remove(fe.mesh);
      fe.mesh.geometry.dispose();
      (fe.mesh.material as THREE.Material).dispose();
    }
    this.innerFoodCache.clear();
    if (this.arenaRingMesh) {
      this.dynamicGroup.remove(this.arenaRingMesh);
      this.arenaRingMesh.geometry.dispose();
      (this.arenaRingMesh.material as THREE.Material).dispose();
      this.arenaRingMesh = null;
    }
  }

  private finishFrame(time: number): void {
    // Post-FX: board pass (clear) → main scene → bloom → vignette + low-HP.
    this.oceanMat.uniforms.u_time.value = time;
    this.postFX.setTime(time);
    const tFx = perf.start('r.postfx');
    this.postFX.render();
    perf.end('r.postfx', tFx);

    // HUD overlay renders after all FX so it stays crisp.
    if (this.hudScene.children.length > 0) {
      this.gl.autoClear = false;
      this.gl.clearDepth();
      this.gl.render(this.hudScene, this.hudCamera);
    }

    // Scene fader always last so it overlays everything.
    this.fader.tick(time);
    this.fader.render(this.gl);
  }

  // Build meshes for a Monster positioned at its core_pos/heading.
  // Caller owns the returned group and MUST dispose via disposeGroup()
  // when done. Used by preview scenes (starter select, end screen).
  buildMonsterPreview(m: Monster, time: number): THREE.Group {
    const group = new THREE.Group();

    const fill = buildCellFillLocal(m);
    const fillMat = createCellFillMaterial({
      baseColor: new THREE.Color(m.color[0], m.color[1], m.color[2]),
      dietColor: DIET_COLOR[m.part_effect.diet],
      noiseSeed: m.noise_seed * 10
    });
    fillMat.uniforms.u_time.value = time;
    const fillMesh = new THREE.Mesh(fill, fillMat);
    // Preview is used standalone (starter select pedestal). Geometry is
    // already in the monster's local frame; the caller drives heading
    // by setting group.rotation.z before parenting.
    group.add(fillMesh);
    group.rotation.z = m.heading;

    const outline = buildClosedRibbon(shapeLocal(m), 6.0);
    const outlineMat = createChalkMaterial(CHALK_INK);
    group.add(new THREE.Mesh(outline, outlineMat));

    for (const p of m.parts) {
      if (p.kind !== PartKind.MOUTH) continue;
      const arc = buildArc([p.anchor[0], p.anchor[1]], 14 * p.scale, -0.9, 0.9, 12, 5.0);
      const arcMat = createChalkMaterial(CHALK_INK);
      group.add(new THREE.Mesh(arc, arcMat));
    }
    return group;
  }

  // ----- cache builders ----------------------------------------------

  private makeMonsterCacheEntry(m: Monster, shade: ShadeOpts): MonsterCacheEntry {
    const root = new THREE.Group();
    const baseCol = shadeColor(new THREE.Color(m.color[0], m.color[1], m.color[2]), shade);
    const dietCol = shadeColor(DIET_COLOR[m.part_effect.diet].clone(), shade);
    const inkCol = shadeColor(CHALK_INK.clone(), shade);

    const fillMat = createCellFillMaterial({
      baseColor: baseCol,
      dietColor: dietCol,
      noiseSeed: m.noise_seed * 10,
      alphaScale: shade.alphaScale
    });
    const fillMesh = new THREE.Mesh(buildCellFillLocal(m), fillMat);
    fillMesh.renderOrder = 0;
    root.add(fillMesh);

    const outlineMat = createChalkMaterial(inkCol);
    outlineMat.uniforms.u_alpha.value = shade.alphaScale;
    const outlineMesh = new THREE.Mesh(buildClosedRibbon(shapeLocal(m), 6.0), outlineMat);
    outlineMesh.renderOrder = 5;
    root.add(outlineMesh);

    const partMeshes: THREE.Mesh[] = [];
    for (const p of m.parts) {
      if (p.kind !== PartKind.MOUTH) continue;
      const arc = buildArc([p.anchor[0], p.anchor[1]], 14 * p.scale, -0.9, 0.9, 12, 5.0);
      const arcMat = createChalkMaterial(inkCol);
      arcMat.uniforms.u_alpha.value = shade.alphaScale;
      const arcMesh = new THREE.Mesh(arc, arcMat);
      arcMesh.renderOrder = 6;
      root.add(arcMesh);
      partMeshes.push(arcMesh);
    }

    return {
      root,
      fillMesh,
      outlineMesh,
      partMeshes,
      shapeKey: monsterShapeKey(m),
      lastFrame: this.frameNum
    };
  }

  private rebuildMonsterGeometry(e: MonsterCacheEntry, m: Monster): void {
    e.fillMesh.geometry.dispose();
    e.fillMesh.geometry = buildCellFillLocal(m);
    e.outlineMesh.geometry.dispose();
    e.outlineMesh.geometry = buildClosedRibbon(shapeLocal(m), 6.0);
    // Rebuild mouth arc geometries to match new scales/anchors. Simple:
    // dispose existing part meshes, re-add.
    for (const pm of e.partMeshes) {
      e.root.remove(pm);
      pm.geometry.dispose();
      (pm.material as THREE.Material).dispose();
    }
    e.partMeshes.length = 0;
    const inkCol = CHALK_INK.clone();
    for (const p of m.parts) {
      if (p.kind !== PartKind.MOUTH) continue;
      const arc = buildArc([p.anchor[0], p.anchor[1]], 14 * p.scale, -0.9, 0.9, 12, 5.0);
      const arcMat = createChalkMaterial(inkCol);
      const arcMesh = new THREE.Mesh(arc, arcMat);
      arcMesh.renderOrder = 6;
      e.root.add(arcMesh);
      e.partMeshes.push(arcMesh);
    }
  }

  private makeFoodCacheEntry(f: Food, shade: ShadeOpts): FoodCacheEntry {
    const body = f.kind === FoodKind.PLANT ? PLANT_GREEN_BODY : MEAT_GREEN_BODY;
    // One merged geometry per food — every stroke of all three color
    // passes folded into a single BufferGeometry. Per-vertex color
    // attribute drives the final tint (+ alpha attribute for pass
    // contrast). This collapses ~66 meshes/food to exactly 1.
    const geom = buildFoodGeometry(f.radius);
    const mat = createChalkMaterial(shadeColor(body.clone(), shade));
    mat.uniforms.u_alpha.value = 0.9 * shade.alphaScale;
    const mesh = new THREE.Mesh(geom, mat);
    mesh.renderOrder = -2;
    mesh.position.set(f.pos[0], f.pos[1], 0);
    return {
      mesh,
      radiusKey: Math.round(f.radius),
      lastFrame: this.frameNum
    };
  }
}

// Free a cached Three.Group and all child geometries/materials.
function disposeGroupDeep(g: THREE.Group): void {
  g.traverse((o) => {
    const m = o as THREE.Mesh;
    if (m.geometry) m.geometry.dispose();
    const mat = m.material as THREE.Material | THREE.Material[] | undefined;
    if (Array.isArray(mat)) mat.forEach((mm) => mm.dispose());
    else if (mat) mat.dispose();
  });
}

// ----- geometry builders -----------------------------------------------

// Monster shape in local space (heading=0, origin=core_pos). The root
// group transform handles position + rotation per frame.
function shapeLocal(m: Monster): Vec2[] {
  const out: Vec2[] = [];
  for (const [x, y] of m.shape) out.push([x, y]);
  return out;
}

// Fan-triangulation of the shape + per-vertex inset (0 at rim, 1 at
// centroid). Feeds cell_body.frag. Coords are local (un-rotated).
function buildCellFillLocal(m: Monster): THREE.BufferGeometry {
  const shape = m.shape;
  const n = shape.length;

  let cx = 0;
  let cy = 0;
  for (const [x, y] of shape) {
    cx += x;
    cy += y;
  }
  cx /= n;
  cy /= n;

  let minX = Infinity,
    minY = Infinity,
    maxX = -Infinity,
    maxY = -Infinity;
  for (const [x, y] of shape) {
    if (x < minX) minX = x;
    if (y < minY) minY = y;
    if (x > maxX) maxX = x;
    if (y > maxY) maxY = y;
  }
  const w = Math.max(1e-4, maxX - minX);
  const h = Math.max(1e-4, maxY - minY);

  const verts: number[] = [];
  const insets: number[] = [];
  const uvs: number[] = [];

  verts.push(cx, cy, 0);
  insets.push(1);
  uvs.push((cx - minX) / w, (cy - minY) / h);

  for (let i = 0; i < n; ++i) {
    const [x, y] = shape[i];
    verts.push(x, y, 0);
    insets.push(0);
    uvs.push((x - minX) / w, (y - minY) / h);
  }

  const indices: number[] = [];
  for (let i = 0; i < n; ++i) {
    indices.push(0, 1 + i, 1 + ((i + 1) % n));
  }

  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(verts, 3));
  g.setAttribute('a_inset', new THREE.Float32BufferAttribute(insets, 1));
  g.setAttribute('a_uv', new THREE.Float32BufferAttribute(uvs, 2));
  g.setIndex(indices);
  return g;
}

// Build a thickened polyline. Each segment emits two triangles; corners
// are mitered by averaging adjacent perpendiculars.
function buildOpenRibbon(points: Vec2[], halfWidth: number): THREE.BufferGeometry {
  return buildRibbon(points, halfWidth, false);
}
function buildClosedRibbon(points: Vec2[], halfWidth: number): THREE.BufferGeometry {
  return buildRibbon(points, halfWidth, true);
}

function buildRibbon(points: Vec2[], halfWidth: number, closed: boolean): THREE.BufferGeometry {
  const n = points.length;
  const verts: number[] = [];
  const across: number[] = [];
  const along: number[] = [];
  const indices: number[] = [];

  const alongAt: number[] = new Array(n).fill(0);
  for (let i = 1; i < n; ++i) {
    const dx = points[i][0] - points[i - 1][0];
    const dy = points[i][1] - points[i - 1][1];
    alongAt[i] = alongAt[i - 1] + Math.hypot(dx, dy);
  }

  function tangent(i: number): [number, number] {
    const prev = closed ? (i - 1 + n) % n : Math.max(0, i - 1);
    const next = closed ? (i + 1) % n : Math.min(n - 1, i + 1);
    const ax = points[next][0] - points[prev][0];
    const ay = points[next][1] - points[prev][1];
    const L = Math.hypot(ax, ay) || 1;
    return [ax / L, ay / L];
  }

  for (let i = 0; i < n; ++i) {
    const [tx, ty] = tangent(i);
    const nx = -ty;
    const ny = tx;
    const [px, py] = points[i];
    verts.push(px + nx * halfWidth, py + ny * halfWidth, 0);
    across.push(+1);
    along.push(alongAt[i]);
    verts.push(px - nx * halfWidth, py - ny * halfWidth, 0);
    across.push(-1);
    along.push(alongAt[i]);
  }

  const segs = closed ? n : n - 1;
  for (let i = 0; i < segs; ++i) {
    const a = (i * 2) % (n * 2);
    const b = a + 1;
    const c = ((i + 1) * 2) % (n * 2);
    const d = c + 1;
    indices.push(a, b, d);
    indices.push(a, d, c);
  }

  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(verts, 3));
  g.setAttribute('a_across', new THREE.Float32BufferAttribute(across, 1));
  g.setAttribute('a_along', new THREE.Float32BufferAttribute(along, 1));
  g.setIndex(indices);
  return g;
}

function buildRingRibbon(radius: number, samples: number, halfWidth: number): THREE.BufferGeometry {
  const pts: Vec2[] = [];
  for (let i = 0; i < samples; ++i) {
    const a = (i / samples) * Math.PI * 2;
    pts.push([Math.cos(a) * radius, Math.sin(a) * radius]);
  }
  return buildClosedRibbon(pts, halfWidth);
}

function buildArc(
  center: Vec2,
  radius: number,
  a0: number,
  a1: number,
  samples: number,
  halfWidth: number
): THREE.BufferGeometry {
  const pts: Vec2[] = [];
  for (let i = 0; i <= samples; ++i) {
    const t = i / samples;
    const a = a0 + (a1 - a0) * t;
    pts.push([center[0] + Math.cos(a) * radius, center[1] + Math.sin(a) * radius]);
  }
  return buildOpenRibbon(pts, halfWidth);
}

// Emit polyline segments for a 6-fold snowflake glyph at the origin.
// Local-space (the mesh position places it in the world). Simplified
// from native drawFood(): hex core + 6 trident branches.
function buildSnowflakeStrokes(radius: number, offset: [number, number]): Vec2[][] {
  const strokes: Vec2[][] = [];
  const [cx, cy] = [offset[0], offset[1]];

  const hex: Vec2[] = [];
  const coreR = radius * 0.28;
  for (let i = 0; i <= 6; ++i) {
    const a = (i / 6) * Math.PI * 2;
    hex.push([cx + Math.cos(a) * coreR, cy + Math.sin(a) * coreR]);
  }
  strokes.push(hex);

  for (let k = 0; k < 6; ++k) {
    const a = (k / 6) * Math.PI * 2;
    const dx = Math.cos(a);
    const dy = Math.sin(a);
    const px = -dy;
    const py = dx;

    const base: Vec2 = [cx + dx * coreR * 0.9, cy + dy * coreR * 0.9];
    const tip: Vec2 = [cx + dx * radius, cy + dy * radius];
    strokes.push([base, tip]);

    const midT = 0.55;
    const mid: Vec2 = [base[0] + (tip[0] - base[0]) * midT, base[1] + (tip[1] - base[1]) * midT];
    const fork = radius * 0.25;
    strokes.push([mid, [mid[0] + dx * fork * 0.4 + px * fork, mid[1] + dy * fork * 0.4 + py * fork]]);
    strokes.push([mid, [mid[0] + dx * fork * 0.4 - px * fork, mid[1] + dy * fork * 0.4 - py * fork]]);

    const prong = radius * 0.22;
    strokes.push([tip, [tip[0] + dx * prong, tip[1] + dy * prong]]);
    strokes.push([tip, [tip[0] + dx * prong * 0.4 + px * prong, tip[1] + dy * prong * 0.4 + py * prong]]);
    strokes.push([tip, [tip[0] + dx * prong * 0.4 - px * prong, tip[1] + dy * prong * 0.4 - py * prong]]);
  }
  return strokes;
}

// Merge all three color-pass ribbons of a snowflake into a single
// BufferGeometry (no vertex-color variation — the fragment shader uses
// a single tint, we accept a minor visual simplification for ~66×
// draw-call reduction). Drawn at local origin; mesh position places it.
function buildFoodGeometry(radius: number): THREE.BufferGeometry {
  const passes: Array<[number, number]> = [
    [1.2, -1.2],
    [0, 0],
    [-0.8, 0.8]
  ];
  const geoms: THREE.BufferGeometry[] = [];
  for (const off of passes) {
    for (const seg of buildSnowflakeStrokes(radius, off)) {
      geoms.push(buildOpenRibbon(seg, 3.0));
    }
  }
  return mergeBufferGeometries(geoms);
}

// Minimal merge — concat position/a_across/a_along/index across inputs.
// Sufficient for our chalk ribbons; avoids pulling in three's
// BufferGeometryUtils (which has a runtime mismatch API surface we
// don't want to depend on).
function mergeBufferGeometries(geoms: THREE.BufferGeometry[]): THREE.BufferGeometry {
  let posTotal = 0;
  let idxTotal = 0;
  for (const g of geoms) {
    posTotal += g.getAttribute('position').count;
    const idx = g.getIndex();
    idxTotal += idx ? idx.count : g.getAttribute('position').count;
  }
  const pos = new Float32Array(posTotal * 3);
  const across = new Float32Array(posTotal);
  const along = new Float32Array(posTotal);
  const indices = new Uint32Array(idxTotal);
  let posOff = 0;   // element count (verts)
  let idxOff = 0;   // index count
  for (const g of geoms) {
    const p = g.getAttribute('position') as THREE.BufferAttribute;
    const a = g.getAttribute('a_across') as THREE.BufferAttribute;
    const l = g.getAttribute('a_along') as THREE.BufferAttribute;
    pos.set(p.array as Float32Array, posOff * 3);
    across.set(a.array as Float32Array, posOff);
    along.set(l.array as Float32Array, posOff);
    const idx = g.getIndex();
    if (idx) {
      const src = idx.array as ArrayLike<number>;
      for (let i = 0; i < src.length; ++i) indices[idxOff + i] = src[i] + posOff;
      idxOff += src.length;
    }
    posOff += p.count;
    g.dispose();
  }
  const out = new THREE.BufferGeometry();
  out.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  out.setAttribute('a_across', new THREE.BufferAttribute(across, 1));
  out.setAttribute('a_along', new THREE.BufferAttribute(along, 1));
  out.setIndex(new THREE.BufferAttribute(indices, 1));
  return out;
}

import * as THREE from 'three';
import { Diet, Monster, Vec2 } from '../sim/monster';
import { PartKind } from '../sim/part';
import { Food, FoodKind, World } from '../sim/world';
import { createBoardMaterial } from './board_material';
import { createChalkMaterial } from './chalk_material';
import { createCellFillMaterial } from './cell_fill_material';
import { createPostFX, PostFX } from './post_fx';
import { RenderPass } from 'three/examples/jsm/postprocessing/RenderPass.js';
import { SceneFader } from './transitions';

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

const PLANT_GREEN_BODY = new THREE.Color(0x3a8f3a);
const PLANT_GREEN_HI = new THREE.Color(0x7dd67d);
const PLANT_GREEN_SHADOW = new THREE.Color(0x25601f);
const MEAT_GREEN_BODY = new THREE.Color(0x1e6f2e);
const MEAT_GREEN_HI = new THREE.Color(0x4fa55a);
const MEAT_GREEN_SHADOW = new THREE.Color(0x0f4019);

export class Renderer {
  readonly scene = new THREE.Scene();
  readonly camera: THREE.OrthographicCamera;
  readonly gl: THREE.WebGLRenderer;

  private boardMat = createBoardMaterial();
  private boardMesh: THREE.Mesh;
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

  get domElement(): HTMLCanvasElement { return this.canvas; }

  constructor(private canvas: HTMLCanvasElement) {
    this.gl = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: false });
    this.gl.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.gl.setClearColor(0xf7f2de, 1);

    // Fullscreen board quad (NDC, driven by the material).
    const boardGeom = new THREE.PlaneGeometry(2, 2);
    this.boardMesh = new THREE.Mesh(boardGeom, this.boardMat);
    this.boardMesh.frustumCulled = false;
    this.boardMesh.renderOrder = -1000;
    // Board renders in its own pass (we draw it first, ignoring the main camera).
    const boardScene = new THREE.Scene();
    boardScene.add(this.boardMesh);
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

    // Rebuild dynamic geometry each frame. Cheap at our scale (~20 cells
    // × 32 verts); if it ever matters we can cache per-monster meshes.
    this.clearGroup(this.dynamicGroup);
    this.clearGroup(this.backgroundGroup);
    this.backgroundGroup.visible = false;
    this.drawWorld(world, time, this.dynamicGroup, NO_SHADE);

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
    this.clearGroup(this.dynamicGroup);
    this.clearGroup(this.backgroundGroup);

    if (outer) {
      // Transform the whole background group: scale by zoom, translate by
      // parallax offset — outer creatures drift slowly as the camera moves.
      this.backgroundGroup.scale.set(zoom, zoom, 1);
      this.backgroundGroup.position.set(
        -cameraWorld[0] * parallax,
        -cameraWorld[1] * parallax,
        0
      );
      this.backgroundGroup.visible = true;
      const shade: ShadeOpts = { dim, creamMix, alphaScale, noRing: true, noPlayerEmphasis: true };
      this.drawWorld(outer, time, this.backgroundGroup, shade);
    } else {
      this.backgroundGroup.visible = false;
    }

    this.drawWorld(inner, time, this.dynamicGroup, NO_SHADE);
    this.finishFrame(time);
  }

  private drawWorld(world: World, time: number, group: THREE.Group, shade: ShadeOpts): void {
    if (!shade.noRing) this.drawArenaRing(world.map_radius, group);
    for (const food of world.food) this.drawFood(food, time, group, shade);
    for (const m of world.monsters.values()) this.drawMonster(m, time, group, shade);
  }

  // Renders just the board + HUD + fader (no world content). Scenes
  // that don't own a World (main menu, starter select, end screen)
  // call this each frame.
  renderEmpty(time: number): void {
    this.boardMat.uniforms.u_time.value = time;
    this.clearGroup(this.dynamicGroup);
    this.finishFrame(time);
  }

  private finishFrame(time: number): void {
    // Post-FX: board pass (clear) → main scene → bloom → vignette → low-HP.
    this.postFX.setTime(time);
    this.postFX.render();

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

    const fill = buildCellFill(m);
    const fillMat = createCellFillMaterial({
      baseColor: new THREE.Color(m.color[0], m.color[1], m.color[2]),
      dietColor: DIET_COLOR[m.part_effect.diet],
      noiseSeed: m.noise_seed * 10
    });
    fillMat.uniforms.u_time.value = time;
    group.add(new THREE.Mesh(fill, fillMat));

    const worldPoly = shapeInWorld(m);
    const outline = buildClosedRibbon(worldPoly, 6.0);
    const outlineMat = createChalkMaterial(CHALK_INK);
    group.add(new THREE.Mesh(outline, outlineMat));

    for (const p of m.parts) {
      if (p.kind !== PartKind.MOUTH) continue;
      const cosH = Math.cos(m.heading);
      const sinH = Math.sin(m.heading);
      const wx = m.core_pos[0] + p.anchor[0] * cosH - p.anchor[1] * sinH;
      const wy = m.core_pos[1] + p.anchor[0] * sinH + p.anchor[1] * cosH;
      const arc = buildArc([wx, wy], 14 * p.scale, m.heading - 0.9, m.heading + 0.9, 12, 5.0);
      const arcMat = createChalkMaterial(CHALK_INK);
      group.add(new THREE.Mesh(arc, arcMat));
    }
    return group;
  }

  // ----- drawing helpers ---------------------------------------------

  private clearGroup(g: THREE.Group): void {
    while (g.children.length) {
      const c = g.children.pop()!;
      // Dispose geometry/material so we don't leak per-frame.
      if ((c as THREE.Mesh).geometry) (c as THREE.Mesh).geometry.dispose();
      const mat = (c as THREE.Mesh).material as THREE.Material | THREE.Material[];
      if (Array.isArray(mat)) mat.forEach((m) => m.dispose());
      else if (mat) mat.dispose();
    }
  }

  private drawArenaRing(radius: number, group: THREE.Group): void {
    const ribbon = buildRingRibbon(radius, 128, 4.0);
    const mat = createChalkMaterial(ARENA_RING);
    mat.uniforms.u_alpha.value = 0.6;
    const mesh = new THREE.Mesh(ribbon, mat);
    mesh.renderOrder = -10;
    group.add(mesh);
  }

  private drawMonster(m: Monster, time: number, group: THREE.Group, shade: ShadeOpts): void {
    const baseCol = shadeColor(new THREE.Color(m.color[0], m.color[1], m.color[2]), shade);
    const dietCol = shadeColor(DIET_COLOR[m.part_effect.diet].clone(), shade);
    const inkCol = shadeColor(CHALK_INK.clone(), shade);
    // 1. Fill pass (under the chalk outline).
    const fill = buildCellFill(m);
    const fillMat = createCellFillMaterial({
      baseColor: baseCol,
      dietColor: dietCol,
      noiseSeed: m.noise_seed * 10,
      alphaScale: shade.alphaScale
    });
    fillMat.uniforms.u_time.value = time;
    const fillMesh = new THREE.Mesh(fill, fillMat);
    fillMesh.renderOrder = 0;
    group.add(fillMesh);

    // 2. Chalk outline ribbon — closed polygon in world space.
    const worldPoly = shapeInWorld(m);
    const outline = buildClosedRibbon(worldPoly, 6.0);
    const outlineMat = createChalkMaterial(inkCol);
    outlineMat.uniforms.u_alpha.value = shade.alphaScale;
    const outlineMesh = new THREE.Mesh(outline, outlineMat);
    outlineMesh.renderOrder = 5;
    group.add(outlineMesh);

    // 3. Parts — just render MOUTH as a small chalk arc for v1.
    for (const p of m.parts) {
      if (p.kind !== PartKind.MOUTH) continue;
      const cosH = Math.cos(m.heading);
      const sinH = Math.sin(m.heading);
      const wx = m.core_pos[0] + p.anchor[0] * cosH - p.anchor[1] * sinH;
      const wy = m.core_pos[1] + p.anchor[0] * sinH + p.anchor[1] * cosH;
      const arc = buildArc([wx, wy], 14 * p.scale, m.heading - 0.9, m.heading + 0.9, 12, 5.0);
      const arcMat = createChalkMaterial(inkCol);
      arcMat.uniforms.u_alpha.value = shade.alphaScale;
      const arcMesh = new THREE.Mesh(arc, arcMat);
      arcMesh.renderOrder = 6;
      group.add(arcMesh);
    }
  }

  private drawFood(f: Food, _time: number, group: THREE.Group, shade: ShadeOpts): void {
    // Snowflake glyph: six V-branches with trident tips + hex core.
    // Three color passes: shadow (offset), body, highlight (top).
    const body = f.kind === FoodKind.PLANT ? PLANT_GREEN_BODY : MEAT_GREEN_BODY;
    const hi = f.kind === FoodKind.PLANT ? PLANT_GREEN_HI : MEAT_GREEN_HI;
    const sh = f.kind === FoodKind.PLANT ? PLANT_GREEN_SHADOW : MEAT_GREEN_SHADOW;

    const passes: Array<{ color: THREE.Color; offset: [number, number]; alpha: number }> = [
      { color: sh, offset: [1.2, -1.2], alpha: 0.85 },
      { color: body, offset: [0, 0], alpha: 1.0 },
      { color: hi, offset: [-0.8, 0.8], alpha: 0.7 }
    ];

    for (const pass of passes) {
      const strokes = buildSnowflakeStrokes(f.pos, f.radius, pass.offset);
      for (const seg of strokes) {
        const geom = buildOpenRibbon(seg, 3.0);
        const mat = createChalkMaterial(shadeColor(pass.color.clone(), shade));
        mat.uniforms.u_alpha.value = pass.alpha * shade.alphaScale;
        const mesh = new THREE.Mesh(geom, mat);
        mesh.renderOrder = -2;
        group.add(mesh);
      }
    }
  }
}

// ----- geometry builders -----------------------------------------------

// Convert a monster's local shape + pose into world-space polygon.
function shapeInWorld(m: Monster): Vec2[] {
  const cosH = Math.cos(m.heading);
  const sinH = Math.sin(m.heading);
  const out: Vec2[] = [];
  for (const [x, y] of m.shape) {
    out.push([m.core_pos[0] + x * cosH - y * sinH, m.core_pos[1] + x * sinH + y * cosH]);
  }
  return out;
}

// Fan-triangulation of the shape + per-vertex inset (0 at rim, 1 at
// centroid). Feeds cell_body.frag.
function buildCellFill(m: Monster): THREE.BufferGeometry {
  const world = shapeInWorld(m);
  const n = world.length;

  // Centroid.
  let cx = 0;
  let cy = 0;
  for (const [x, y] of world) {
    cx += x;
    cy += y;
  }
  cx /= n;
  cy /= n;

  // bbox for uv.
  let minX = Infinity,
    minY = Infinity,
    maxX = -Infinity,
    maxY = -Infinity;
  for (const [x, y] of world) {
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

  // Vertex 0 = centroid, inset=1.
  verts.push(cx, cy, 0);
  insets.push(1);
  uvs.push((cx - minX) / w, (cy - minY) / h);

  // Rim verts 1..n, inset=0.
  for (let i = 0; i < n; ++i) {
    const [x, y] = world[i];
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

  // Precompute segment normals and cumulative along-length.
  const alongAt: number[] = new Array(n).fill(0);
  for (let i = 1; i < n; ++i) {
    const dx = points[i][0] - points[i - 1][0];
    const dy = points[i][1] - points[i - 1][1];
    alongAt[i] = alongAt[i - 1] + Math.hypot(dx, dy);
  }

  function tangent(i: number): [number, number] {
    let ax: number, ay: number;
    const prev = closed ? (i - 1 + n) % n : Math.max(0, i - 1);
    const next = closed ? (i + 1) % n : Math.min(n - 1, i + 1);
    ax = points[next][0] - points[prev][0];
    ay = points[next][1] - points[prev][1];
    const L = Math.hypot(ax, ay) || 1;
    return [ax / L, ay / L];
  }

  for (let i = 0; i < n; ++i) {
    const [tx, ty] = tangent(i);
    // Perpendicular (rotated +90°).
    const nx = -ty;
    const ny = tx;
    const [px, py] = points[i];
    // +width side
    verts.push(px + nx * halfWidth, py + ny * halfWidth, 0);
    across.push(+1);
    along.push(alongAt[i]);
    // -width side
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

// Emit polyline segments for a 6-fold snowflake glyph.
// Simplified version of native drawFood(): hex core + 6 trident branches.
function buildSnowflakeStrokes(center: Vec2, radius: number, offset: [number, number]): Vec2[][] {
  const strokes: Vec2[][] = [];
  const [cx, cy] = [center[0] + offset[0], center[1] + offset[1]];

  // Hex core (small closed hex as a polyline).
  const hex: Vec2[] = [];
  const coreR = radius * 0.28;
  for (let i = 0; i <= 6; ++i) {
    const a = (i / 6) * Math.PI * 2;
    hex.push([cx + Math.cos(a) * coreR, cy + Math.sin(a) * coreR]);
  }
  strokes.push(hex);

  // Six main branches with V-fork midway and trident tip.
  for (let k = 0; k < 6; ++k) {
    const a = (k / 6) * Math.PI * 2;
    const dx = Math.cos(a);
    const dy = Math.sin(a);
    const px = -dy;
    const py = dx;

    const base: Vec2 = [cx + dx * coreR * 0.9, cy + dy * coreR * 0.9];
    const tip: Vec2 = [cx + dx * radius, cy + dy * radius];
    strokes.push([base, tip]);

    // Mid V-fork.
    const midT = 0.55;
    const mid: Vec2 = [base[0] + (tip[0] - base[0]) * midT, base[1] + (tip[1] - base[1]) * midT];
    const fork = radius * 0.25;
    strokes.push([mid, [mid[0] + dx * fork * 0.4 + px * fork, mid[1] + dy * fork * 0.4 + py * fork]]);
    strokes.push([mid, [mid[0] + dx * fork * 0.4 - px * fork, mid[1] + dy * fork * 0.4 - py * fork]]);

    // Trident tip (three short prongs at the end).
    const prong = radius * 0.22;
    strokes.push([tip, [tip[0] + dx * prong, tip[1] + dy * prong]]);
    strokes.push([tip, [tip[0] + dx * prong * 0.4 + px * prong, tip[1] + dy * prong * 0.4 + py * prong]]);
    strokes.push([tip, [tip[0] + dx * prong * 0.4 - px * prong, tip[1] + dy * prong * 0.4 - py * prong]]);
  }

  return strokes;
}

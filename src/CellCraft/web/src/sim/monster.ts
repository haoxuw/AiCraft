// Mirrors src/CellCraft/sim/monster.h. RadialCell-style lobed polygon
// with 64 samples; refresh_stats recomputes base speed/turn and hp_max.

import { computePartEffects, Diet, Part, PartEffect, PartKind, VenomStack } from './part';
import {
  DENSITY,
  HP_PER_BIOMASS,
  MOVE_K,
  MOVE_MAX,
  MOVE_MIN,
  TIER_SIZE_MULTS,
  TURN_K,
  TURN_MAX,
  TURN_MIN
} from './tuning';
import { clamp, Vec2 } from './vec2';

export type Shape = Vec2[];
export type { Vec2 };

export interface Monster {
  id: number;
  owner: number;
  core_pos: Vec2;
  heading: number;
  vel: Vec2;

  shape: Shape;
  body_scale: number;

  biomass: number;
  lifetime_biomass: number;
  tier: number;

  hp: number;
  hp_max: number;
  alive: boolean;

  color: [number, number, number];
  parts: Part[];
  part_effect: PartEffect;
  venom_stacks: VenomStack[];

  // Cached derived stats.
  max_core_radius: number;
  max_width: number;
  area: number;
  mass: number;
  move_speed: number;
  turn_speed: number;

  // Compatibility / visual.
  noise_seed: number;
  is_player: boolean;
  material_banked: number; // unused for now; reserved for split/grow costs
  // Per-frame visual heading — = heading + small wobble. Purely cosmetic;
  // sim logic only reads `heading`. Updated by the renderer each frame.
  visual_heading: number;

  // Optional behavior artifact id (e.g. "base:hunt"). Pure AI metadata —
  // engine never reads this; the client-side AI dispatcher resolves it
  // to a BehaviorDef.
  behavior_id?: string;
}

// --- Shape helpers --------------------------------------------------

// Lobed circle with N samples — legacy helper (kept for tests / previews
// that want a round shape). Most callers should use makeTeardropShape.
export function makeLobedShape(
  baseRadius: number,
  samples: number,
  seed: number,
  lobeAmp = 0.12,
  lobeCount = 5
): Shape {
  const out: Shape = [];
  let s = seed | 0;
  const rand = () => {
    s = (s * 1664525 + 1013904223) | 0;
    return ((s >>> 0) & 0xffff) / 0xffff;
  };
  for (let i = 0; i < samples; ++i) {
    const a = (i / samples) * Math.PI * 2;
    const lobe = 1 + lobeAmp * Math.sin(a * lobeCount + seed * 0.37);
    const jitter = 1 + (rand() - 0.5) * 0.05;
    const r = baseRadius * lobe * jitter;
    out.push([Math.cos(a) * r, Math.sin(a) * r]);
  }
  return out;
}

// Directional teardrop / ovoid. +x is forward (nose tip), -x is tail.
// Front/back ratio ~1.5:1. The tail has a shallow concave notch where
// flagella attach. Kept in local coords; heading rotation happens in
// worldPolygon()/shapeInWorld().
export function makeTeardropShape(
  baseRadius: number,
  samples: number,
  seed: number
): Shape {
  const out: Shape = [];
  let s = seed | 0;
  const rand = (): number => {
    s = (s * 1664525 + 1013904223) | 0;
    return ((s >>> 0) & 0xffff) / 0xffff;
  };
  const frontR = baseRadius * 1.15; // reach in +x
  const tailR = baseRadius * 0.75;  // reach in -x
  const sideR = baseRadius * 0.85;
  for (let i = 0; i < samples; ++i) {
    const a = (i / samples) * Math.PI * 2;
    const cx = Math.cos(a);
    const sy = Math.sin(a);
    // Blend front/back radii along +x/-x; side radius on |y|.
    // Use a smooth ellipse with a tail-pinch: narrow more as we approach
    // the extreme -x, and add a tiny concave notch right at a==π.
    const lenR = cx >= 0 ? frontR : tailR;
    // Teardrop: forward half rounds fat, back half narrows toward tail.
    // Compute an ellipse (cx*lenR, sy*sideR) then pinch the tail by scaling
    // y toward zero as cx -> -1.
    let x = cx * lenR;
    let y = sy * sideR;
    if (cx < 0) {
      // narrow the back half: scale y by how close we are to the tail tip.
      const tailT = -cx; // 0 at equator, 1 at tail tip
      y *= 1.0 - 0.55 * tailT * tailT;
      // Pull the tail tip in further for a pointier rear.
      x *= 1.0 - 0.12 * tailT;
    }
    // Shallow concave notch at the very back for flagella attachment.
    const notch = Math.exp(-Math.pow((a - Math.PI) * 3.5, 2)) * baseRadius * 0.10;
    x += notch;
    // Small deterministic jitter for chalky feel.
    const jitter = 1 + (rand() - 0.5) * 0.04;
    out.push([x * jitter, y * jitter]);
  }
  return out;
}

export function shapeMaxRadius(shape: Shape): number {
  let m = 0;
  for (const [x, y] of shape) {
    const r = Math.hypot(x, y);
    if (r > m) m = r;
  }
  return m;
}

export function shapeHalfExtents(shape: Shape): Vec2 {
  let hx = 0, hy = 0;
  for (const [x, y] of shape) {
    if (Math.abs(x) > hx) hx = Math.abs(x);
    if (Math.abs(y) > hy) hy = Math.abs(y);
  }
  return [hx, hy];
}

export function shapeArea(shape: Shape): number {
  let sum = 0;
  const n = shape.length;
  for (let i = 0; i < n; ++i) {
    const [x0, y0] = shape[i];
    const [x1, y1] = shape[(i + 1) % n];
    sum += x0 * y1 - x1 * y0;
  }
  return Math.abs(sum) * 0.5;
}

export function refreshStats(m: Monster): void {
  m.max_core_radius = shapeMaxRadius(m.shape);
  const [hx, hy] = shapeHalfExtents(m.shape);
  m.max_width = Math.max(hx, hy);
  m.area = shapeArea(m.shape);
  m.mass = m.area * DENSITY;

  const r = Math.max(m.max_core_radius, 1e-3);
  const w = Math.max(m.max_width, 1e-3);

  const baseTurn = clamp(TURN_K / r, TURN_MIN, TURN_MAX);
  const baseMove = clamp(MOVE_K / w, MOVE_MIN, MOVE_MAX);

  m.part_effect = computePartEffects(m.parts);
  m.turn_speed = baseTurn * m.part_effect.turn_mult;
  m.move_speed = baseMove * m.part_effect.speed_mult;
  m.hp_max = Math.max(1, m.biomass * HP_PER_BIOMASS * m.part_effect.hp_mult);
  if (m.hp > m.hp_max) m.hp = m.hp_max;
}

export function scaleShape(m: Monster, factor: number): void {
  for (const v of m.shape) {
    v[0] *= factor;
    v[1] *= factor;
  }
  refreshStats(m);
}

// World-space polygon (rotate + translate local shape by heading + core_pos).
export function worldPolygon(m: Monster): Vec2[] {
  const c = Math.cos(m.heading);
  const s = Math.sin(m.heading);
  const out: Vec2[] = [];
  for (const [x, y] of m.shape) {
    out.push([m.core_pos[0] + x * c - y * s, m.core_pos[1] + x * s + y * c]);
  }
  return out;
}

export function partWorldAnchor(m: Monster, anchor: Vec2): Vec2 {
  const c = Math.cos(m.heading);
  const s = Math.sin(m.heading);
  return [m.core_pos[0] + anchor[0] * c - anchor[1] * s,
          m.core_pos[1] + anchor[0] * s + anchor[1] * c];
}

// Back-compat for existing renderer.
export function mouthWorldPos(m: Monster): Vec2 | null {
  const mouth = m.parts.find((p) => p.kind === PartKind.MOUTH);
  if (!mouth) return null;
  return partWorldAnchor(m, mouth.anchor);
}

export interface MonsterOptions {
  id: number;
  pos: Vec2;
  baseRadius: number;
  color: [number, number, number];
  parts: Part[];
  seed: number;
  isPlayer?: boolean;
  owner?: number;
  tier?: number;
  behaviorId?: string;
}

export function makeMonster(opts: MonsterOptions): Monster {
  // Teardrop body: +x = forward (nose), -x = tail. 18 samples around the
  // perimeter is enough for a smooth chalk silhouette and keeps the SAT
  // contact test cheap. The old 64-sample lobed circle was symmetrical,
  // which made cells visually "spin" rather than "swim" when the heading
  // changed.
  const shape = makeTeardropShape(opts.baseRadius, 18, opts.seed);
  const tier = opts.tier ?? 1;
  const area = shapeArea(shape);
  const biomass = Math.max(1, area * DENSITY);
  const m: Monster = {
    id: opts.id,
    owner: opts.owner ?? 0,
    core_pos: [opts.pos[0], opts.pos[1]],
    heading: 0,
    vel: [0, 0],
    shape,
    body_scale: TIER_SIZE_MULTS[tier] ?? 1,
    biomass,
    lifetime_biomass: biomass,
    tier,
    hp: 1,
    hp_max: 1,
    alive: true,
    color: opts.color,
    parts: opts.parts,
    part_effect: computePartEffects(opts.parts),
    venom_stacks: [],
    max_core_radius: 0,
    max_width: 0,
    area: 0,
    mass: 0,
    move_speed: 0,
    turn_speed: 0,
    noise_seed: (opts.seed % 997) / 997.0,
    is_player: opts.isPlayer === true,
    material_banked: 0,
    visual_heading: 0,
    behavior_id: opts.behaviorId
  };
  refreshStats(m);
  m.hp = m.hp_max;
  return m;
}

export { Diet };

// Bounding-radius proxy used in broad-phase collision checks.
export function boundingRadius(m: Monster): number {
  return m.max_core_radius;
}

// Back-compat alias for legacy renderer accessor.
export function monsterMaxRadius(m: Monster): number {
  return m.max_core_radius;
}

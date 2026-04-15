// CellCraft sim — TypeScript port of src/CellCraft/sim/sim.cpp.
//
// Tick order (matches native):
//  1. Apply MOVE actions (+ coast / damp).
//  2. Integrate positions, clamp to arena.
//  3. Food pickup (MOUTH-gated, diet-multiplied).
//  4. Monster-monster contacts (damage + venom + soft/hard separation).
//  5. Poison auras.
//  6. Venom DoT + REGEN.
//  7. Finalize deaths (biomass transfer, corpse pellets, tier-up).

import { Action, ActionType } from './action';
import {
  Monster,
  Vec2,
  refreshStats,
  scaleShape,
  worldPolygon
} from './monster';
import { Diet, PartKind, VenomStack } from './part';
import {
  addFood,
  FoodKind,
  getMonster,
  removeFood,
  removeMonster,
  World
} from './world';
import {
  BOUNDARY_K,
  DEATH_BIOMASS_FRAC,
  HP_PER_BIOMASS,
  OVERLAP_MAX_PUSH,
  OVERLAP_PUSH_PAD,
  PART_CONTACT_RADIUS_K,
  PART_DMG_K_MIN,
  PART_DMG_K_SPEED,
  PART_HORN_CONE_COS,
  PART_HORN_DMG_MULT,
  PART_HORN_DMG_SIDE,
  PART_VENOM_DPS,
  PART_VENOM_DURATION,
  SEPARATION_IMPULSE,
  TIER_COUNT,
  TIER_SIZE_MULTS,
  TIER_THRESHOLDS
} from './tuning';
import { clamp } from './vec2';

export type { Action } from './action';
export { ActionType } from './action';

// --- Events --------------------------------------------------------

export type SimEvent =
  | { type: 'PICKUP'; monster: number; food: number; gained: number; pos: Vec2 }
  | { type: 'BITE'; attacker: number; victim: number; amount: number; pos: Vec2 }
  | { type: 'VENOM_HIT'; source: number; victim: number; amount: number; pos: Vec2 }
  | { type: 'POISON_HIT'; source: number; victim: number; amount: number; pos: Vec2 }
  | { type: 'KILL'; killer: number; victim: number; biomass: number; pos: Vec2 }
  | { type: 'DEATH'; victim: number; biomass: number; pos: Vec2 }
  | { type: 'TIER_UP'; monster: number; tier: number; pos: Vec2 };

// --- Helpers -------------------------------------------------------

function yieldMultiplier(diet: Diet, food: FoodKind): number {
  if (diet === Diet.OMNIVORE) return 1.0;
  const matched =
    (diet === Diet.CARNIVORE && food === FoodKind.MEAT) ||
    (diet === Diet.HERBIVORE && food === FoodKind.PLANT);
  // matches native: carn×meat/herb×plant = 1.5, mismatch = 0.4.
  return matched ? 1.5 : 0.4;
}

function pointInPolygon(p: Vec2, poly: Vec2[]): boolean {
  let inside = false;
  const n = poly.length;
  for (let i = 0, j = n - 1; i < n; j = i++) {
    const [xi, yi] = poly[i];
    const [xj, yj] = poly[j];
    if (yi > p[1] !== yj > p[1]) {
      const t = ((p[1] - yi) * (xj - xi)) / (yj - yi + 1e-12) + xi;
      if (p[0] < t) inside = !inside;
    }
  }
  return inside;
}

// SAT overlap test between two convex polygons (polygons here are lobed
// circles — close enough to convex for contact detection).
function polygonsOverlap(a: Vec2[], b: Vec2[]): boolean {
  return satAxisTest(a, b) && satAxisTest(b, a);
}
function satAxisTest(a: Vec2[], b: Vec2[]): boolean {
  const n = a.length;
  for (let i = 0; i < n; ++i) {
    const [x0, y0] = a[i];
    const [x1, y1] = a[(i + 1) % n];
    const ex = x1 - x0;
    const ey = y1 - y0;
    const nx = -ey;
    const ny = ex;
    let aMin = Infinity, aMax = -Infinity, bMin = Infinity, bMax = -Infinity;
    for (const [x, y] of a) {
      const p = x * nx + y * ny;
      if (p < aMin) aMin = p;
      if (p > aMax) aMax = p;
    }
    for (const [x, y] of b) {
      const p = x * nx + y * ny;
      if (p < bMin) bMin = p;
      if (p > bMax) bMax = p;
    }
    if (aMax < bMin || bMax < aMin) return false;
  }
  return true;
}

function wrapAngleDiff(from: number, to: number): number {
  let d = to - from;
  while (d > Math.PI) d -= Math.PI * 2;
  while (d < -Math.PI) d += Math.PI * 2;
  return d;
}

// --- Tier helpers --------------------------------------------------

function computeTier(lifetime: number): number {
  let t = 1;
  for (let i = 2; i <= TIER_COUNT; ++i) {
    if (lifetime >= TIER_THRESHOLDS[i]) t = i;
    else break;
  }
  return t;
}

function tierSizeMult(tier: number): number {
  const t = clamp(tier, 1, TIER_COUNT) | 0;
  return TIER_SIZE_MULTS[t];
}

// --- Public API ----------------------------------------------------

export function tick(world: World, actions: Action[], dt: number): SimEvent[] {
  world.time += dt;
  const events: SimEvent[] = [];
  const lastDamager = new Map<number, number>();

  // Index MOVE actions (last one wins); RELOCATE is a pickup hint.
  const moveBy = new Map<number, Vec2>();
  for (const a of actions) {
    if (a.type === ActionType.MOVE) moveBy.set(a.monster_id, a.vel);
  }

  // 1 + 2: movement integration
  for (const m of world.monsters.values()) {
    if (!m.alive) continue;
    const desired = moveBy.get(m.id);
    if (desired) {
      const dmag = Math.hypot(desired[0], desired[1]);
      if (dmag > 1e-4) {
        const s = Math.min(dmag, m.move_speed);
        m.vel = [(desired[0] / dmag) * s, (desired[1] / dmag) * s];
      } else {
        m.vel = [m.vel[0] * 0.9, m.vel[1] * 0.9];
      }
    } else {
      m.vel = [m.vel[0] * 0.9, m.vel[1] * 0.9];
    }

    // Heading slew: turn toward velocity direction at a bounded rate so
    // cells *swim* rather than snapping. If the cell is effectively idle
    // (|v| below a small threshold), keep the last heading — no more
    // spin-in-place when there's no input.
    const vmag = Math.hypot(m.vel[0], m.vel[1]);
    const IDLE_V = 5.0;
    if (vmag > IDLE_V) {
      const targetHeading = Math.atan2(m.vel[1], m.vel[0]);
      const diff = wrapAngleDiff(m.heading, targetHeading);
      // turn_speed comes from refreshStats(); clamp floor so large cells
      // still turn visibly. 8 rad/s is the Spore-ish ceiling.
      const rate = Math.min(Math.max(m.turn_speed, 3.0), 8.0);
      const maxStep = rate * dt;
      m.heading += clamp(diff, -maxStep, maxStep);
    }

    // Integrate.
    m.core_pos[0] += m.vel[0] * dt;
    m.core_pos[1] += m.vel[1] * dt;

    // Boundary nudge + hard clamp.
    const r = Math.hypot(m.core_pos[0], m.core_pos[1]);
    if (r > world.map_radius && r > 1e-3) {
      const over = r - world.map_radius;
      const ix = -m.core_pos[0] / r;
      const iy = -m.core_pos[1] / r;
      m.vel[0] += ix * (BOUNDARY_K * over / world.map_radius) * dt;
      m.vel[1] += iy * (BOUNDARY_K * over / world.map_radius) * dt;
      m.core_pos[0] = (m.core_pos[0] / r) * world.map_radius;
      m.core_pos[1] = (m.core_pos[1] / r) * world.map_radius;
    }
  }

  // 3: food pickup
  for (const m of world.monsters.values()) {
    if (!m.alive || !m.part_effect.has_mouth) continue;
    const poly = worldPolygon(m);
    const pickupR = m.part_effect.pickup_radius_mult > 1
      ? m.max_core_radius * m.part_effect.pickup_radius_mult
      : 0;
    const pickupR2 = pickupR * pickupR;
    for (let i = world.food.length - 1; i >= 0; --i) {
      const f = world.food[i];
      let hit = pointInPolygon(f.pos, poly);
      if (!hit && pickupR2 > 0) {
        const dx = f.pos[0] - m.core_pos[0];
        const dy = f.pos[1] - m.core_pos[1];
        if (dx * dx + dy * dy <= pickupR2) hit = true;
      }
      if (hit) {
        const gained = f.biomass * yieldMultiplier(m.part_effect.diet, f.kind);
        m.biomass += gained;
        if (gained > 0) m.lifetime_biomass += gained;
        m.hp_max = Math.max(1, m.biomass * HP_PER_BIOMASS * m.part_effect.hp_mult);
        m.hp = Math.min(m.hp_max, m.hp + gained * 0.25);
        events.push({ type: 'PICKUP', monster: m.id, food: f.id, gained, pos: [f.pos[0], f.pos[1]] });
        const tu = maybeTierUp(m);
        if (tu) events.push(tu);
        removeFood(world, f.id);
      }
    }
  }

  // 4: monster contacts
  const ids: number[] = [];
  for (const m of world.monsters.values()) if (m.alive) ids.push(m.id);
  // Cache polygons per monster
  const polys = new Map<number, Vec2[]>();
  for (const id of ids) {
    const m = getMonster(world, id)!;
    polys.set(id, worldPolygon(m));
  }

  for (let i = 0; i < ids.length; ++i) {
    for (let j = i + 1; j < ids.length; ++j) {
      const A = getMonster(world, ids[i]);
      const B = getMonster(world, ids[j]);
      if (!A || !B || !A.alive || !B.alive) continue;
      // Broad phase: bounding-circle
      const dx0 = B.core_pos[0] - A.core_pos[0];
      const dy0 = B.core_pos[1] - A.core_pos[1];
      const d0 = Math.hypot(dx0, dy0);
      const sumR = A.max_core_radius + B.max_core_radius;
      if (d0 > sumR) continue;
      const pa = polys.get(A.id)!;
      const pb = polys.get(B.id)!;
      if (!polygonsOverlap(pa, pb)) continue;

      // Contact point — simple midpoint of cores (TODO: full SAT contact
      // extraction; circle-center proxy is adequate for small lobed cells).
      const contact: Vec2 = [(A.core_pos[0] + B.core_pos[0]) * 0.5,
                             (A.core_pos[1] + B.core_pos[1]) * 0.5];

      const relV: Vec2 = [A.vel[0] - B.vel[0], A.vel[1] - B.vel[1]];
      const relSpeed = Math.hypot(relV[0], relV[1]);

      let dirAB: Vec2;
      if (d0 < 1e-4) dirAB = [1, 0];
      else dirAB = [dx0 / d0, dy0 / d0];

      // Damage application (both directions).
      applyDamageFromPair(A, B, contact, dirAB, relSpeed, events, lastDamager);
      applyDamageFromPair(B, A, contact, [-dirAB[0], -dirAB[1]], relSpeed, events, lastDamager);

      // Soft separating impulse.
      A.vel[0] -= dirAB[0] * SEPARATION_IMPULSE;
      A.vel[1] -= dirAB[1] * SEPARATION_IMPULSE;
      B.vel[0] += dirAB[0] * SEPARATION_IMPULSE;
      B.vel[1] += dirAB[1] * SEPARATION_IMPULSE;

      // Hard separation along dirAB (SAT projection).
      let aMax = -Infinity, bMin = Infinity;
      for (const v of pa) {
        const p = v[0] * dirAB[0] + v[1] * dirAB[1];
        if (p > aMax) aMax = p;
      }
      for (const v of pb) {
        const p = v[0] * dirAB[0] + v[1] * dirAB[1];
        if (p < bMin) bMin = p;
      }
      let penetration = aMax - bMin;
      if (penetration <= 0) penetration = OVERLAP_PUSH_PAD;
      const pushMag = Math.min(penetration + OVERLAP_PUSH_PAD, OVERLAP_MAX_PUSH);
      const ma = Math.max(A.mass, 1e-3);
      const mb = Math.max(B.mass, 1e-3);
      const total = ma + mb;
      const aShare = mb / total;
      const bShare = ma / total;
      A.core_pos[0] -= dirAB[0] * pushMag * aShare;
      A.core_pos[1] -= dirAB[1] * pushMag * aShare;
      B.core_pos[0] += dirAB[0] * pushMag * bShare;
      B.core_pos[1] += dirAB[1] * pushMag * bShare;
      const clampInside = (m: Monster) => {
        const r = Math.hypot(m.core_pos[0], m.core_pos[1]);
        if (r > world.map_radius && r > 1e-3) {
          m.core_pos[0] = (m.core_pos[0] / r) * world.map_radius;
          m.core_pos[1] = (m.core_pos[1] / r) * world.map_radius;
        }
      };
      clampInside(A);
      clampInside(B);
      polys.set(A.id, worldPolygon(A));
      polys.set(B.id, worldPolygon(B));
    }
  }

  // 5: poison auras
  interface Emitter { id: number; owner: number; pos: Vec2; dps: number; r2: number; }
  const emitters: Emitter[] = [];
  for (const m of world.monsters.values()) {
    if (!m.alive || m.part_effect.poison_dps <= 0) continue;
    emitters.push({
      id: m.id, owner: m.owner, pos: [m.core_pos[0], m.core_pos[1]],
      dps: m.part_effect.poison_dps,
      r2: m.part_effect.poison_radius * m.part_effect.poison_radius
    });
  }
  for (const m of world.monsters.values()) {
    if (!m.alive) continue;
    for (const e of emitters) {
      if (e.id === m.id) continue;
      if (e.owner !== 0 && e.owner === m.owner) continue;
      const dx = m.core_pos[0] - e.pos[0];
      const dy = m.core_pos[1] - e.pos[1];
      if (dx * dx + dy * dy > e.r2) continue;
      const amt = e.dps * dt * (1 - m.part_effect.armor_dr);
      if (amt <= 0) continue;
      m.hp -= amt;
      lastDamager.set(m.id, e.id);
      events.push({ type: 'POISON_HIT', source: e.id, victim: m.id, amount: amt, pos: [m.core_pos[0], m.core_pos[1]] });
    }
  }

  // 6: venom DoT + regen
  for (const m of world.monsters.values()) {
    if (!m.alive) continue;
    for (const vs of m.venom_stacks) {
      if (vs.remaining <= 0) continue;
      const amt = vs.magnitude * dt * (1 - m.part_effect.armor_dr);
      if (amt > 0) {
        m.hp -= amt;
        lastDamager.set(m.id, vs.source);
        events.push({ type: 'VENOM_HIT', source: vs.source, victim: m.id, amount: amt, pos: [m.core_pos[0], m.core_pos[1]] });
      }
      vs.remaining -= dt;
    }
    m.venom_stacks = m.venom_stacks.filter((v) => v.remaining > 0);

    if (m.part_effect.regen_per_s > 0 && m.hp > 0 && m.hp < m.hp_max) {
      m.hp = Math.min(m.hp_max, m.hp + m.part_effect.regen_per_s * dt);
    }
  }

  // 7: finalize deaths
  const toRemove: number[] = [];
  for (const m of world.monsters.values()) {
    if (m.alive && m.hp <= 0) {
      m.alive = false;
      toRemove.push(m.id);
    }
  }
  for (const vid of toRemove) {
    const victim = getMonster(world, vid);
    if (!victim) continue;
    let total = victim.biomass;
    const killerId = lastDamager.get(vid) ?? 0;
    let attributed = false;
    if (killerId !== 0) {
      const k = getMonster(world, killerId);
      if (k && k.alive) {
        const toKiller = total * DEATH_BIOMASS_FRAC;
        k.biomass += toKiller;
        if (toKiller > 0) k.lifetime_biomass += toKiller;
        k.hp_max = Math.max(1, k.biomass * HP_PER_BIOMASS * k.part_effect.hp_mult);
        k.hp = Math.min(k.hp_max, k.hp + toKiller * 0.5);
        events.push({ type: 'KILL', killer: killerId, victim: vid, biomass: toKiller, pos: [victim.core_pos[0], victim.core_pos[1]] });
        const tu = maybeTierUp(k);
        if (tu) events.push(tu);
        total -= toKiller;
        attributed = true;
      }
    }
    if (!attributed) {
      events.push({ type: 'DEATH', victim: vid, biomass: total, pos: [victim.core_pos[0], victim.core_pos[1]] });
    }
    const remaining = Math.max(0, total);
    if (remaining > 0) {
      const pellets = 3;
      const each = remaining / pellets;
      for (let p = 0; p < pellets; ++p) {
        const ang = (Math.PI * 2 * p) / pellets;
        addFood(world, [victim.core_pos[0] + Math.cos(ang) * 6, victim.core_pos[1] + Math.sin(ang) * 6], each, FoodKind.MEAT);
      }
    }
    removeMonster(world, vid);
  }

  return events;
}

// Compute + apply damage that `atk` deals to `def` this contact.
function applyDamageFromPair(
  atk: Monster,
  def: Monster,
  contact: Vec2,
  dirIntoDef: Vec2,
  relSpeed: number,
  events: SimEvent[],
  lastDamager: Map<number, number>
): void {
  if (atk.part_effect.damaging_parts.length === 0) return;
  const c = Math.cos(atk.heading);
  const s = Math.sin(atk.heading);
  const speedMag = Math.max(relSpeed * PART_DMG_K_SPEED, PART_DMG_K_MIN);
  let total = 0;
  let venomConnected = false;
  for (const p of atk.part_effect.damaging_parts) {
    const wpx = atk.core_pos[0] + c * p.anchor[0] - s * p.anchor[1];
    const wpy = atk.core_pos[1] + s * p.anchor[0] + c * p.anchor[1];
    const reach = PART_CONTACT_RADIUS_K * Math.max(0.1, p.scale);
    const dpx = wpx - contact[0];
    const dpy = wpy - contact[1];
    if (dpx * dpx + dpy * dpy > reach * reach) continue;
    let dmg = p.base * Math.max(0.1, p.scale) * speedMag;
    if (p.kind === PartKind.HORN) {
      // Local horn direction = normalized anchor (fallback +X).
      const aLen = Math.hypot(p.anchor[0], p.anchor[1]);
      const hx = aLen > 1e-3 ? p.anchor[0] / aLen : 1;
      const hy = aLen > 1e-3 ? p.anchor[1] / aLen : 0;
      const wx = c * hx - s * hy;
      const wy = s * hx + c * hy;
      const cd = wx * dirIntoDef[0] + wy * dirIntoDef[1];
      if (cd >= PART_HORN_CONE_COS) dmg *= 1 + PART_HORN_DMG_MULT;
      else dmg *= 1 + PART_HORN_DMG_SIDE;
    }
    if (p.kind === PartKind.VENOM_SPIKE) venomConnected = true;
    total += dmg;
  }
  total *= 1 - def.part_effect.armor_dr;
  if (total <= 0) return;
  def.hp -= total;
  lastDamager.set(def.id, atk.id);
  events.push({ type: 'BITE', attacker: atk.id, victim: def.id, amount: total, pos: [contact[0], contact[1]] });
  if (venomConnected && atk.part_effect.venom_stacks_on_hit > 0) {
    for (let i = 0; i < atk.part_effect.venom_stacks_on_hit; ++i) {
      const vs: VenomStack = {
        remaining: PART_VENOM_DURATION,
        magnitude: PART_VENOM_DPS,
        source: atk.id
      };
      def.venom_stacks.push(vs);
    }
  }
}

function maybeTierUp(m: Monster): SimEvent | null {
  const newTier = computeTier(m.lifetime_biomass);
  if (newTier <= m.tier) return null;
  const oldTier = m.tier;
  const ratio = tierSizeMult(newTier) / tierSizeMult(oldTier);
  m.tier = newTier;
  if (ratio > 1 && ratio < 100) scaleShape(m, ratio);
  else refreshStats(m);
  m.body_scale = tierSizeMult(newTier);
  return { type: 'TIER_UP', monster: m.id, tier: newTier, pos: [m.core_pos[0], m.core_pos[1]] };
}

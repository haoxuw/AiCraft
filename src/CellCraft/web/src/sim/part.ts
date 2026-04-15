// Mirrors src/CellCraft/sim/part.h + part_stats.h. Keeps the PartKind
// string enum (nicer to log than numeric) while preserving the native
// PartType order for reference.

import type { Vec2 } from './vec2';
import {
  PART_ARMOR_DR_PER,
  PART_ARMOR_HP_ADD,
  PART_ARMOR_MAX_STACK,
  PART_CILIA_MAX_STACK,
  PART_CILIA_TURN_ADD,
  PART_EYES_MAX_STACK,
  PART_EYES_PERCEPTION_ADD,
  PART_FLAGELLA_MAX_STACK,
  PART_FLAGELLA_SPEED_ADD,
  PART_HORN_DMG_MULT,
  PART_HORN_MAX_STACK,
  PART_MOUTH_MAX_STACK,
  PART_MOUTH_RADIUS_ADD,
  PART_POISON_DPS,
  PART_POISON_RADIUS,
  PART_REGEN_HPS,
  PART_REGEN_MAX_STACK,
  PART_SPIKE_DMG_ADD,
  PART_TEETH_DMG_ADD,
  PART_VENOM_MAX_STACK
} from './tuning';

export enum Diet {
  CARNIVORE = 'carnivore',
  HERBIVORE = 'herbivore',
  OMNIVORE = 'omnivore'
}

export enum PartKind {
  SPIKE = 'SPIKE',
  TEETH = 'TEETH',
  FLAGELLA = 'FLAGELLA',
  POISON = 'POISON',
  ARMOR = 'ARMOR',
  CILIA = 'CILIA',
  HORN = 'HORN',
  REGEN = 'REGEN',
  MOUTH = 'MOUTH',
  VENOM_SPIKE = 'VENOM_SPIKE',
  EYES = 'EYES'
}

export interface Part {
  kind: PartKind;
  anchor: Vec2;   // local-space anchor on the body
  scale: number;
  orientation?: number; // radians; used for SPIKE/HORN direction fallback
}

export interface ArmorAnchor {
  anchor: Vec2;
  scale: number;
}

export interface DamagingPart {
  kind: PartKind.SPIKE | PartKind.TEETH | PartKind.HORN | PartKind.VENOM_SPIKE;
  anchor: Vec2;
  scale: number;
  base: number; // base damage pre-scale
}

export interface VenomStack {
  remaining: number;  // seconds
  magnitude: number;  // hp/s
  source: number;
}

export interface PartEffect {
  diet_score: number;
  diet: Diet;
  has_mouth: boolean;
  speed_mult: number;
  turn_mult: number;
  hp_mult: number;
  perception_mult: number;
  pickup_radius_mult: number;
  regen_per_s: number;
  poison_dps: number;
  poison_radius: number;
  armor_dr: number;            // global DR fallback
  local_armor_anchors: ArmorAnchor[]; // for per-hit local lookup
  venom_stacks_on_hit: number; // number of venom stacks to apply per hit
  damaging_parts: DamagingPart[];
}

import {
  PART_DMG_BASE_HORN,
  PART_DMG_BASE_SPIKE,
  PART_DMG_BASE_TEETH,
  PART_DMG_BASE_VENOM_SPIKE
} from './tuning';

function baseDmg(kind: PartKind): number {
  switch (kind) {
    case PartKind.SPIKE: return PART_DMG_BASE_SPIKE;
    case PartKind.TEETH: return PART_DMG_BASE_TEETH;
    case PartKind.HORN: return PART_DMG_BASE_HORN;
    case PartKind.VENOM_SPIKE: return PART_DMG_BASE_VENOM_SPIKE;
    default: return 0;
  }
}

// Port of part_stats.h:computePartEffects. Stack caps, diet scoring, and
// damaging-parts enumeration all match the native aggregation.
export function computePartEffects(parts: Part[]): PartEffect {
  const e: PartEffect = {
    diet_score: 0,
    diet: Diet.OMNIVORE,
    has_mouth: false,
    speed_mult: 1,
    turn_mult: 1,
    hp_mult: 1,
    perception_mult: 1,
    pickup_radius_mult: 1,
    regen_per_s: 0,
    poison_dps: 0,
    poison_radius: 0,
    armor_dr: 0,
    local_armor_anchors: [],
    venom_stacks_on_hit: 0,
    damaging_parts: []
  };

  let dietAccum = 0;
  for (const p of parts) {
    const s = Math.max(0.1, p.scale);
    switch (p.kind) {
      case PartKind.TEETH:
      case PartKind.SPIKE:
      case PartKind.VENOM_SPIKE:
      case PartKind.HORN:
      case PartKind.POISON:
        dietAccum += s; break;
      case PartKind.REGEN:
      case PartKind.ARMOR:
      case PartKind.CILIA:
        dietAccum -= s; break;
      case PartKind.MOUTH:
      case PartKind.FLAGELLA:
      case PartKind.EYES:
        break;
    }
  }
  e.diet_score = dietAccum;
  if (dietAccum > 0.8) e.diet = Diet.CARNIVORE;
  else if (dietAccum < -0.8) e.diet = Diet.HERBIVORE;
  else e.diet = Diet.OMNIVORE;

  let flagella = 0, armor = 0, cilia = 0, regen = 0, mouth = 0, venom = 0, horn = 0, eyes = 0;
  let havePoison = false;
  let poisonScaleSum = 0;
  let poisonRadiusMax = 0;

  for (const p of parts) {
    const s = Math.max(0.1, p.scale);
    switch (p.kind) {
      case PartKind.FLAGELLA:
        if (flagella < PART_FLAGELLA_MAX_STACK) {
          e.speed_mult += PART_FLAGELLA_SPEED_ADD * s;
          flagella++;
        }
        break;
      case PartKind.ARMOR:
        if (armor < PART_ARMOR_MAX_STACK) {
          e.hp_mult += PART_ARMOR_HP_ADD * s;
          e.armor_dr = Math.min(0.8, e.armor_dr + PART_ARMOR_DR_PER * s);
          armor++;
        }
        e.local_armor_anchors.push({ anchor: [p.anchor[0], p.anchor[1]], scale: s });
        break;
      case PartKind.SPIKE:
        e.damaging_parts.push({ kind: PartKind.SPIKE, anchor: p.anchor, scale: s, base: baseDmg(p.kind) });
        // keep damage_mult parity (unused by TS sim but cheap to compute)
        void PART_SPIKE_DMG_ADD;
        break;
      case PartKind.TEETH:
        e.damaging_parts.push({ kind: PartKind.TEETH, anchor: p.anchor, scale: s, base: baseDmg(p.kind) });
        void PART_TEETH_DMG_ADD;
        break;
      case PartKind.POISON:
        havePoison = true;
        poisonScaleSum += s;
        poisonRadiusMax = Math.max(poisonRadiusMax, PART_POISON_RADIUS * s);
        break;
      case PartKind.CILIA:
        if (cilia < PART_CILIA_MAX_STACK) {
          e.turn_mult += PART_CILIA_TURN_ADD * s;
          cilia++;
        }
        break;
      case PartKind.HORN:
        if (horn < PART_HORN_MAX_STACK) {
          e.damaging_parts.push({ kind: PartKind.HORN, anchor: p.anchor, scale: s, base: baseDmg(p.kind) });
          horn++;
          void PART_HORN_DMG_MULT;
        }
        break;
      case PartKind.REGEN:
        if (regen < PART_REGEN_MAX_STACK) {
          e.regen_per_s += PART_REGEN_HPS * s;
          regen++;
        }
        break;
      case PartKind.MOUTH:
        e.has_mouth = true;
        if (mouth < PART_MOUTH_MAX_STACK) {
          e.pickup_radius_mult += PART_MOUTH_RADIUS_ADD * s;
          mouth++;
        }
        break;
      case PartKind.VENOM_SPIKE:
        if (venom < PART_VENOM_MAX_STACK) {
          e.damaging_parts.push({ kind: PartKind.VENOM_SPIKE, anchor: p.anchor, scale: s, base: baseDmg(p.kind) });
          e.venom_stacks_on_hit += 1;
          venom++;
        }
        break;
      case PartKind.EYES:
        if (eyes < PART_EYES_MAX_STACK) {
          e.perception_mult += PART_EYES_PERCEPTION_ADD * s;
          eyes++;
        }
        break;
    }
  }
  if (havePoison) {
    e.poison_dps = PART_POISON_DPS * poisonScaleSum;
    e.poison_radius = poisonRadiusMax;
  }
  return e;
}

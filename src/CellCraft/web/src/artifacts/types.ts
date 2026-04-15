// Artifact type definitions. Mirrors the spirit of CivCraft's Python
// artifact system (src/CivCraft/artifacts/**) — each gameplay content
// type has a stable interface and a string "base:name" id so mods can
// override or extend without touching the engine (sim/renderer).

import type { Action } from '../sim/action';
import type { Monster } from '../sim/monster';
import type { Part, PartKind, Diet } from '../sim/part';
import type { World } from '../sim/world';

// --- Parts ----------------------------------------------------------
//
// A PartDef describes a part kind: its numeric tuning contribution plus
// any stat caps. The sim still resolves effects centrally via
// computePartEffects(), but parts must be registered here to be
// referenced by monsters.

export interface PartDef {
  id: string;                // "base:mouth"
  kind: PartKind;            // enum member
  displayName: string;
  // Free-form blurb for UI/docs. Tuning constants live in tuning.ts and
  // are read by computePartEffects(); parts declare only intent here.
  blurb?: string;
}

// --- Monsters -------------------------------------------------------

export interface MonsterPartSpec {
  kind: PartKind;
  anchor: [number, number];
  scale?: number;
}

export interface MonsterDef {
  id: string;                         // "base:shard"
  name: string;                       // display name, e.g. "SHARD"
  blurb?: string;                     // starter-select flavor text
  color: [number, number, number];
  parts: MonsterPartSpec[];
  seed: number;
  // Optional: baseRadius override. If omitted, spawners decide.
  baseRadius?: number;
  // Optional: diet override (otherwise derived from parts).
  diet?: Diet;
  // Starter tier (true = appears in starter-select UI).
  starter?: boolean;
  // Default behavior id when this monster is AI-controlled.
  behavior?: string;                  // default "base:mixed"
}

// --- Behaviors ------------------------------------------------------
//
// Behaviors are pure functions over (world, monster, dt, state) → Action.
// Per-monster scratch state lives outside the behavior itself (held by
// the AI dispatcher).

export interface BehaviorCtx {
  world: World;
  monster: Monster;
  dt: number;
  // Persistent scratch bag for this (behavior, monster) pair.
  state: Record<string, unknown>;
}

export interface BehaviorDef {
  id: string;                         // "base:hunt"
  name: string;
  decide(ctx: BehaviorCtx): Action;
}

// --- Registry API (implementation in registry.ts) -------------------

export interface RegistryAPI {
  registerPart(def: PartDef): void;
  registerMonster(def: MonsterDef): void;
  registerBehavior(def: BehaviorDef): void;
}

// A mod module may export `register(reg)` to imperatively add defs.
export type ModRegister = (reg: RegistryAPI) => void;

export type { Part, PartKind, Diet, Monster, World, Action };

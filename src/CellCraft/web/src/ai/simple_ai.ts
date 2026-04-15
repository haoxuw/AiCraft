// AI dispatcher. Each live non-player monster resolves its
// BehaviorDef once via the artifacts registry, then per-tick we call
// decide(ctx) with persistent per-monster scratch state. All real
// policy lives in artifacts/behaviors/**. This file stays tiny — the
// engine has no intelligence; the game is the artifacts.
//
// The default behavior id is "base:mixed" for monsters that don't
// declare one (diet-aware hunt/feeder/wander).

import { getBehavior } from '../artifacts';
import type { BehaviorDef } from '../artifacts';
import { Action } from '../sim/action';
import { Monster } from '../sim/monster';
import { Diet } from '../sim/part';
import { World } from '../sim/world';

const DEFAULT_BEHAVIOR = 'base:mixed';

interface PerMonsterAI {
  behavior: BehaviorDef;
  state: Record<string, unknown>;
}

export type AIStateMap = Map<number, PerMonsterAI>;

const defaultState: AIStateMap = new Map<number, PerMonsterAI>();

function defaultBehaviorFor(m: Monster): string {
  if (m.behavior_id) return m.behavior_id;
  // Legacy fallback for monsters spawned without a behavior id.
  if (m.part_effect.diet === Diet.CARNIVORE) return 'base:hunt';
  return 'base:mixed';
}

function ensure(m: Monster, state: AIStateMap): PerMonsterAI | null {
  let entry = state.get(m.id);
  if (entry) return entry;
  const id = defaultBehaviorFor(m);
  const behavior = getBehavior(id) ?? getBehavior(DEFAULT_BEHAVIOR);
  if (!behavior) return null; // artifacts not loaded; skip this monster.
  entry = { behavior, state: {} };
  state.set(m.id, entry);
  return entry;
}

export function makeAIStateMap(): AIStateMap {
  return new Map<number, PerMonsterAI>();
}

export function decideAll(world: World, dt: number, state: AIStateMap = defaultState): Action[] {
  const out: Action[] = [];
  for (const m of world.monsters.values()) {
    if (m.is_player || !m.alive) continue;
    const ai = ensure(m, state);
    if (!ai) continue;
    out.push(ai.behavior.decide({ world, monster: m, dt, state: ai.state }));
  }
  return out;
}

export function resetAI(): void {
  defaultState.clear();
}

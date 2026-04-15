// Simple AI: per-entity mode (wander / feeder / hunter) → MOVE actions.
// Rule 1 compliance: all decision logic lives client-side here; the sim
// merely consumes ActionProposal.MOVE.

import { Action, ActionType } from '../sim/action';
import { Monster } from '../sim/monster';
import { Diet } from '../sim/part';
import { World } from '../sim/world';

export type AIMode = 'wander' | 'feeder' | 'hunter';

interface AIState {
  mode: AIMode;
  dir: number;
  timer: number;
}

export type AIStateMap = Map<number, AIState>;

// Default state map for the primary (inner) world. External callers
// (e.g. the outer/background world) can pass their own map so state
// is per-world and doesn't collide across ID spaces.
const defaultState: AIStateMap = new Map<number, AIState>();

function pickMode(m: Monster): AIMode {
  if (m.part_effect.diet === Diet.CARNIVORE) return 'hunter';
  if (m.part_effect.diet === Diet.HERBIVORE) return 'feeder';
  return 'feeder';
}

function ensure(m: Monster, state: AIStateMap): AIState {
  let st = state.get(m.id);
  if (!st) {
    st = { mode: pickMode(m), dir: Math.random() * Math.PI * 2, timer: 0 };
    state.set(m.id, st);
  }
  return st;
}

export function makeAIStateMap(): AIStateMap {
  return new Map<number, AIState>();
}

export function decideAll(world: World, dt: number, state: AIStateMap = defaultState): Action[] {
  const out: Action[] = [];
  for (const m of world.monsters.values()) {
    if (m.is_player || !m.alive) continue;
    out.push(decideOne(world, m, dt, state));
  }
  return out;
}

function decideOne(world: World, m: Monster, dt: number, state: AIStateMap): Action {
  const st = ensure(m, state);
  const perception = 900 * m.part_effect.perception_mult;

  if (st.mode === 'hunter') {
    // Target nearest other monster (carnivore).
    let best: Monster | null = null;
    let bestD2 = Infinity;
    for (const other of world.monsters.values()) {
      if (other.id === m.id || !other.alive) continue;
      const dx = other.core_pos[0] - m.core_pos[0];
      const dy = other.core_pos[1] - m.core_pos[1];
      const d2 = dx * dx + dy * dy;
      if (d2 < bestD2) { bestD2 = d2; best = other; }
    }
    if (best && bestD2 < perception * perception) {
      const dx = best.core_pos[0] - m.core_pos[0];
      const dy = best.core_pos[1] - m.core_pos[1];
      const d = Math.sqrt(bestD2) || 1;
      const s = m.move_speed;
      return { type: ActionType.MOVE, monster_id: m.id, vel: [(dx / d) * s, (dy / d) * s] };
    }
  }

  // Feeder / fallback: seek nearest food.
  let best: { dx: number; dy: number; d2: number } | null = null;
  for (const f of world.food) {
    const dx = f.pos[0] - m.core_pos[0];
    const dy = f.pos[1] - m.core_pos[1];
    const d2 = dx * dx + dy * dy;
    if (best == null || d2 < best.d2) best = { dx, dy, d2 };
  }
  if (best && best.d2 < perception * perception) {
    const d = Math.sqrt(best.d2) || 1;
    const s = m.move_speed * 0.8;
    return { type: ActionType.MOVE, monster_id: m.id, vel: [(best.dx / d) * s, (best.dy / d) * s] };
  }

  // Wander.
  st.timer -= dt;
  if (st.timer <= 0) {
    st.dir += (Math.random() - 0.5) * 1.2;
    st.timer = 1.0 + Math.random() * 1.5;
  }
  const s = m.move_speed * 0.5;
  return {
    type: ActionType.MOVE,
    monster_id: m.id,
    vel: [Math.cos(st.dir) * s, Math.sin(st.dir) * s]
  };
}

export function resetAI(): void {
  defaultState.clear();
}

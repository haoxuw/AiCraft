// Base behavior definitions. Each behavior is a pure decide() that
// returns one of the four ActionProposal types — for CellCraft v1 that's
// always a MOVE. Behaviors compose freely: "mixed" is hunt-or-feeder
// based on the monster's diet.
//
// Per-monster scratch lives in ctx.state (the dispatcher owns the bag).

import { Action, ActionType } from '../../sim/action';
import { Monster } from '../../sim/monster';
import { Diet } from '../../sim/part';
import { World } from '../../sim/world';
import { registry } from '../registry';
import type { BehaviorCtx } from '../types';

interface WanderState {
  dir?: number;
  timer?: number;
}

function wanderMove(monster: Monster, dt: number, state: WanderState): Action {
  if (state.dir === undefined) state.dir = Math.random() * Math.PI * 2;
  if (state.timer === undefined) state.timer = 0;
  state.timer -= dt;
  if (state.timer <= 0) {
    state.dir += (Math.random() - 0.5) * 1.2;
    state.timer = 1.0 + Math.random() * 1.5;
  }
  const s = monster.move_speed * 0.5;
  return {
    type: ActionType.MOVE,
    monster_id: monster.id,
    vel: [Math.cos(state.dir) * s, Math.sin(state.dir) * s]
  };
}

function nearestMonster(world: World, self: Monster): { target: Monster; d2: number } | null {
  let best: Monster | null = null;
  let bestD2 = Infinity;
  for (const other of world.monsters.values()) {
    if (other.id === self.id || !other.alive) continue;
    const dx = other.core_pos[0] - self.core_pos[0];
    const dy = other.core_pos[1] - self.core_pos[1];
    const d2 = dx * dx + dy * dy;
    if (d2 < bestD2) { bestD2 = d2; best = other; }
  }
  return best ? { target: best, d2: bestD2 } : null;
}

function nearestFood(world: World, self: Monster): { dx: number; dy: number; d2: number } | null {
  let best: { dx: number; dy: number; d2: number } | null = null;
  for (const f of world.food) {
    const dx = f.pos[0] - self.core_pos[0];
    const dy = f.pos[1] - self.core_pos[1];
    const d2 = dx * dx + dy * dy;
    if (best == null || d2 < best.d2) best = { dx, dy, d2 };
  }
  return best;
}

function huntMove(ctx: BehaviorCtx): Action | null {
  const { world, monster } = ctx;
  const perception = 900 * monster.part_effect.perception_mult;
  const near = nearestMonster(world, monster);
  if (near && near.d2 < perception * perception) {
    const dx = near.target.core_pos[0] - monster.core_pos[0];
    const dy = near.target.core_pos[1] - monster.core_pos[1];
    const d = Math.sqrt(near.d2) || 1;
    const s = monster.move_speed;
    return { type: ActionType.MOVE, monster_id: monster.id, vel: [(dx / d) * s, (dy / d) * s] };
  }
  return null;
}

function feedMove(ctx: BehaviorCtx): Action | null {
  const { world, monster } = ctx;
  const perception = 900 * monster.part_effect.perception_mult;
  const near = nearestFood(world, monster);
  if (near && near.d2 < perception * perception) {
    const d = Math.sqrt(near.d2) || 1;
    const s = monster.move_speed * 0.8;
    return {
      type: ActionType.MOVE,
      monster_id: monster.id,
      vel: [(near.dx / d) * s, (near.dy / d) * s]
    };
  }
  return null;
}

// Wander: aimless drift with periodic direction change.
registry.registerBehavior({
  id: 'base:wander',
  name: 'Wander',
  decide(ctx) {
    return wanderMove(ctx.monster, ctx.dt, ctx.state as WanderState);
  }
});

// Hunt: seek nearest other monster; else wander.
registry.registerBehavior({
  id: 'base:hunt',
  name: 'Hunt',
  decide(ctx) {
    return huntMove(ctx) ?? wanderMove(ctx.monster, ctx.dt, ctx.state as WanderState);
  }
});

// Flee: move away from nearest other monster; else wander.
registry.registerBehavior({
  id: 'base:flee',
  name: 'Flee',
  decide(ctx) {
    const { world, monster } = ctx;
    const perception = 900 * monster.part_effect.perception_mult;
    const near = nearestMonster(world, monster);
    if (near && near.d2 < perception * perception) {
      const dx = monster.core_pos[0] - near.target.core_pos[0];
      const dy = monster.core_pos[1] - near.target.core_pos[1];
      const d = Math.sqrt(near.d2) || 1;
      const s = monster.move_speed;
      return { type: ActionType.MOVE, monster_id: monster.id, vel: [(dx / d) * s, (dy / d) * s] };
    }
    return wanderMove(monster, ctx.dt, ctx.state as WanderState);
  }
});

// Feeder: seek food; else wander.
registry.registerBehavior({
  id: 'base:feeder',
  name: 'Feeder',
  decide(ctx) {
    return feedMove(ctx) ?? wanderMove(ctx.monster, ctx.dt, ctx.state as WanderState);
  }
});

// Mixed: diet-aware default — carnivores hunt, others forage; fallback
// to wander. Matches the original simple_ai dispatch.
registry.registerBehavior({
  id: 'base:mixed',
  name: 'Mixed',
  decide(ctx) {
    const diet = ctx.monster.part_effect.diet;
    if (diet === Diet.CARNIVORE) {
      const hit = huntMove(ctx);
      if (hit) return hit;
    }
    const fed = feedMove(ctx);
    if (fed) return fed;
    return wanderMove(ctx.monster, ctx.dt, ctx.state as WanderState);
  }
});

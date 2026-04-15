import { describe, expect, it } from 'vitest';
import { ActionType } from '../action';
import { PartKind } from '../part';
import { tick } from '../sim';
import { TIER_SIZE_MULTS, TIER_THRESHOLDS } from '../tuning';
import { addFood, FoodKind, makeWorld, spawnMonster } from '../world';

describe('sim', () => {
  it('damage only flows from the attacker with a damaging part', () => {
    const w = makeWorld();
    // Attacker with SPIKE at the forward lobe.
    const attacker = spawnMonster(w, {
      pos: [-30, 0],
      baseRadius: 40,
      color: [1, 0, 0],
      seed: 1,
      parts: [
        { kind: PartKind.MOUTH, anchor: [30, 0], scale: 1 },
        { kind: PartKind.SPIKE, anchor: [36, 0], scale: 1 }
      ]
    });
    // Defender has only MOUTH (no damaging parts).
    const defender = spawnMonster(w, {
      pos: [30, 0],
      baseRadius: 40,
      color: [0, 0, 1],
      seed: 2,
      parts: [{ kind: PartKind.MOUTH, anchor: [30, 0], scale: 1 }]
    });
    // Give them a closing velocity so rel_speed > 0.
    attacker.vel = [200, 0];
    defender.vel = [-200, 0];
    const attackerHpBefore = attacker.hp;
    const defenderHpBefore = defender.hp;
    tick(w, [
      { type: ActionType.MOVE, monster_id: attacker.id, vel: [300, 0] },
      { type: ActionType.MOVE, monster_id: defender.id, vel: [-300, 0] }
    ], 1 / 60);
    // Defender took damage; attacker did not.
    expect(defender.hp).toBeLessThan(defenderHpBefore);
    expect(attacker.hp).toBeGreaterThanOrEqual(attackerHpBefore - 1e-6);
  });

  it('omnivore gets 1.0x yield from a plant food', () => {
    const w = makeWorld();
    const m = spawnMonster(w, {
      pos: [0, 0],
      baseRadius: 40,
      color: [0, 1, 0],
      seed: 3,
      parts: [{ kind: PartKind.MOUTH, anchor: [0, 0], scale: 1 }]
    });
    // Plant food placed at core — inside polygon for sure.
    addFood(w, [0, 0], 10, FoodKind.PLANT);
    const bmBefore = m.biomass;
    tick(w, [], 1 / 60);
    expect(w.food.length).toBe(0);
    // Omnivore yield = 1.0 → exactly +10 biomass.
    expect(m.biomass).toBeCloseTo(bmBefore + 10.0, 5);
  });

  it('tier-up at threshold 2 sets body_scale to 1.25', () => {
    const w = makeWorld();
    const m = spawnMonster(w, {
      pos: [0, 0],
      baseRadius: 40,
      color: [1, 1, 1],
      seed: 4,
      parts: [{ kind: PartKind.MOUTH, anchor: [0, 0], scale: 1 }]
    });
    // Directly set lifetime to a value in the tier-2 band, then drive
    // biomass accrual via a tiny pickup so the sim triggers maybeTierUp.
    m.lifetime_biomass = 0;
    m.biomass = 1;
    // Target lifetime between TIER_THRESHOLDS[2] (40) and [3] (120).
    const want = TIER_THRESHOLDS[2] + 5;
    addFood(w, [0, 0], want, FoodKind.MEAT);
    // Omnivore × meat → 1.0× yield. After pickup, lifetime ≈ want.
    tick(w, [], 1 / 60);
    expect(m.tier).toBe(2);
    expect(m.body_scale).toBeCloseTo(TIER_SIZE_MULTS[2], 5);
  });
});

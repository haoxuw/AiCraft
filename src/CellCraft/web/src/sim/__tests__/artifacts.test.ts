import { beforeAll, describe, expect, it } from 'vitest';
import {
  allBehaviors,
  allMonsters,
  allParts,
  allStarters,
  getBehavior,
  getMonster,
  getPart,
  loadAllArtifacts
} from '../../artifacts';
import { registry } from '../../artifacts/registry';
import { defToStarter, partsFromSpec } from '../../artifacts/spawn';
import { computePartEffects, Diet, PartKind } from '../part';

beforeAll(() => {
  loadAllArtifacts();
});

describe('artifacts registry', () => {
  it('registers all base parts', () => {
    const ids = allParts().map((p) => p.id).sort();
    for (const expected of [
      'base:armor',
      'base:cilia',
      'base:eyes',
      'base:flagella',
      'base:horn',
      'base:mouth',
      'base:poison',
      'base:regen',
      'base:spike',
      'base:teeth',
      'base:venom_spike'
    ]) {
      expect(ids).toContain(expected);
    }
  });

  it('each PartKind enum value has a registered def', () => {
    const kinds = new Set(allParts().map((p) => p.kind));
    for (const k of Object.values(PartKind)) {
      expect(kinds.has(k)).toBe(true);
    }
  });

  it('returns the three starters with non-empty parts', () => {
    const starters = allStarters();
    const names = starters.map((s) => s.name).sort();
    expect(names).toEqual(['DARTER', 'MOSS', 'SHARD']);
    for (const s of starters) {
      expect(s.parts.length).toBeGreaterThan(0);
      expect(s.color).toHaveLength(3);
    }
  });

  it('exposes AI-only monsters beyond starters', () => {
    const all = allMonsters();
    expect(all.length).toBeGreaterThan(allStarters().length);
    expect(getMonster('base:pricklet')).toBeDefined();
    expect(getMonster('base:drip')).toBeDefined();
  });

  it('defToStarter produces Parts suitable for spawnMonster', () => {
    const def = getMonster('base:shard');
    expect(def).toBeDefined();
    if (!def) return;
    const starter = defToStarter(def);
    expect(starter.parts.length).toBe(def.parts.length);
    expect(starter.parts[0].kind).toBe(def.parts[0].kind);
    expect(typeof starter.parts[0].scale).toBe('number');
  });

  it('diet scoring through registry matches hand-rolled parts', () => {
    const def = getMonster('base:shard');
    if (!def) throw new Error('missing shard');
    const parts = partsFromSpec(def.parts);
    const eff = computePartEffects(parts);
    // SHARD has MOUTH + 2×SPIKE → net carnivore.
    expect(eff.diet).toBe(Diet.CARNIVORE);
    expect(eff.has_mouth).toBe(true);
    expect(eff.damaging_parts.length).toBe(2);
  });

  it('rejects malformed ids', () => {
    expect(() => registry.registerPart({ id: 'Bad Id', kind: PartKind.MOUTH, displayName: 'x' })).toThrow();
  });

  it('returns undefined for unknown ids', () => {
    expect(getPart('base:nope')).toBeUndefined();
    expect(getMonster('base:nope')).toBeUndefined();
    expect(getBehavior('base:nope')).toBeUndefined();
  });

  it('registers the base behaviors', () => {
    const ids = allBehaviors().map((b) => b.id).sort();
    for (const expected of ['base:feeder', 'base:flee', 'base:hunt', 'base:mixed', 'base:wander']) {
      expect(ids).toContain(expected);
    }
  });
});

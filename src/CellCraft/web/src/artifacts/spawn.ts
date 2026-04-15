// Helpers to turn a MonsterDef into runtime sim data.
// Kept separate from registry.ts so tests can exercise registration
// without pulling in sim types.

import type { Part } from '../sim/part';
import type { MonsterDef, MonsterPartSpec } from './types';

export function partsFromSpec(spec: MonsterPartSpec[]): Part[] {
  return spec.map((p) => ({
    kind: p.kind,
    anchor: [p.anchor[0], p.anchor[1]],
    scale: p.scale ?? 1.0
  }));
}

export function defToStarter(def: MonsterDef): {
  id: string;
  name: string;
  blurb: string;
  color: [number, number, number];
  parts: Part[];
  seed: number;
} {
  return {
    id: def.id,
    name: def.name,
    blurb: def.blurb ?? '',
    color: def.color,
    parts: partsFromSpec(def.parts),
    seed: def.seed
  };
}

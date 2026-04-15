// Central artifact registry. String ids use the "base:name" convention
// per CLAUDE.md. The engine (sim + renderer) reads from here; gameplay
// content (parts, monsters, behaviors) register into here.

import type {
  BehaviorDef,
  MonsterDef,
  PartDef,
  RegistryAPI
} from './types';

const parts = new Map<string, PartDef>();
const monsters = new Map<string, MonsterDef>();
const behaviors = new Map<string, BehaviorDef>();

function assertId(id: string, kind: string): void {
  if (!/^[a-z0-9_]+:[a-z0-9_]+$/.test(id)) {
    throw new Error(`artifact: invalid ${kind} id "${id}" — expected "namespace:name"`);
  }
}

export const registry: RegistryAPI = {
  registerPart(def: PartDef): void {
    assertId(def.id, 'part');
    parts.set(def.id, def);
  },
  registerMonster(def: MonsterDef): void {
    assertId(def.id, 'monster');
    monsters.set(def.id, def);
  },
  registerBehavior(def: BehaviorDef): void {
    assertId(def.id, 'behavior');
    behaviors.set(def.id, def);
  }
};

export function getPart(id: string): PartDef | undefined {
  return parts.get(id);
}

export function getMonster(id: string): MonsterDef | undefined {
  return monsters.get(id);
}

export function getBehavior(id: string): BehaviorDef | undefined {
  return behaviors.get(id);
}

export function allParts(): PartDef[] {
  return [...parts.values()];
}

export function allMonsters(): MonsterDef[] {
  return [...monsters.values()];
}

export function allBehaviors(): BehaviorDef[] {
  return [...behaviors.values()];
}

export function allStarters(): MonsterDef[] {
  return [...monsters.values()].filter((m) => m.starter === true);
}

// Test-only: wipe every registered artifact. Not called in production.
export function _resetRegistryForTests(): void {
  parts.clear();
  monsters.clear();
  behaviors.clear();
}

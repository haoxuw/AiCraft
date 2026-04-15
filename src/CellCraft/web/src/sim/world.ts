import { makeMonster, Monster, refreshStats, Vec2 } from './monster';
import { Part, PartKind } from './part';
import {
  DEFAULT_MAP_RADIUS,
  FOOD_MEAT_BIOMASS_MAX,
  FOOD_MEAT_BIOMASS_MIN,
  FOOD_PLANT_BIOMASS_MAX,
  FOOD_PLANT_BIOMASS_MIN,
  FOOD_PLANT_FRACTION
} from './tuning';

export enum FoodKind {
  PLANT = 'PLANT',
  MEAT = 'MEAT'
}

export interface Food {
  id: number;
  kind: FoodKind;
  pos: Vec2;
  biomass: number;
  radius: number;
  seed: number;
}

export interface World {
  map_radius: number;
  monsters: Map<number, Monster>;
  food: Food[];
  next_id: number;
  time: number;
  scale: number;
}

export function makeWorld(): World {
  return {
    map_radius: DEFAULT_MAP_RADIUS,
    monsters: new Map(),
    food: [],
    next_id: 1,
    time: 0,
    scale: 1
  };
}

export function configureWorld(w: World, scale: number): void {
  w.scale = scale;
  w.map_radius = DEFAULT_MAP_RADIUS * scale;
}

function rand(range: number, fn: () => number): number {
  return (fn() - 0.5) * 2 * range;
}

export function addFood(w: World, pos: Vec2, biomass: number, kind: FoodKind): Food {
  const f: Food = {
    id: w.next_id++,
    kind,
    pos: [pos[0], pos[1]],
    biomass,
    radius: kind === FoodKind.PLANT ? 14 : 18,
    seed: Math.floor(Math.random() * 1e6)
  };
  w.food.push(f);
  return f;
}

export function scatterFood(w: World, count: number, rng: () => number = Math.random): void {
  for (let i = 0; i < count; ++i) {
    let x = 0, y = 0;
    for (;;) {
      x = rand(w.map_radius * 0.92, rng);
      y = rand(w.map_radius * 0.92, rng);
      if (Math.hypot(x, y) < w.map_radius * 0.92) break;
    }
    const plant = rng() < FOOD_PLANT_FRACTION;
    const kind = plant ? FoodKind.PLANT : FoodKind.MEAT;
    const bmMin = plant ? FOOD_PLANT_BIOMASS_MIN : FOOD_MEAT_BIOMASS_MIN;
    const bmMax = plant ? FOOD_PLANT_BIOMASS_MAX : FOOD_MEAT_BIOMASS_MAX;
    const biomass = bmMin + rng() * (bmMax - bmMin);
    w.food.push({
      id: w.next_id++,
      kind,
      pos: [x, y],
      biomass,
      radius: plant ? 14 : 18,
      seed: Math.floor(rng() * 1e6)
    });
  }
}

export interface SpawnOptions {
  pos: Vec2;
  baseRadius: number;
  color: [number, number, number];
  parts?: Part[];
  isPlayer?: boolean;
  seed?: number;
  owner?: number;
  behaviorId?: string;
}

export function spawnMonster(w: World, opts: SpawnOptions): Monster {
  const id = w.next_id++;
  const parts: Part[] =
    opts.parts ?? [{ kind: PartKind.MOUTH, anchor: [opts.baseRadius * 0.85, 0], scale: 1.0 }];
  const m = makeMonster({
    id,
    pos: opts.pos,
    baseRadius: opts.baseRadius,
    color: opts.color,
    parts,
    seed: opts.seed ?? Math.floor(Math.random() * 1e6),
    isPlayer: opts.isPlayer,
    owner: opts.owner,
    behaviorId: opts.behaviorId
  });
  w.monsters.set(id, m);
  refreshStats(m);
  m.hp = m.hp_max;
  return m;
}

export function removeMonster(w: World, id: number): void {
  w.monsters.delete(id);
}

export function getMonster(w: World, id: number): Monster | undefined {
  return w.monsters.get(id);
}

export function removeFood(w: World, id: number): Food | undefined {
  const idx = w.food.findIndex((f) => f.id === id);
  if (idx < 0) return undefined;
  const [taken] = w.food.splice(idx, 1);
  return taken;
}

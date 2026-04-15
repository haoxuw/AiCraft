// Minimal vec2 helpers. Tuple form `[x, y]` — matches existing sim code.
// Mutation style: functions return new tuples; use direct index access for
// hot loops.
export type Vec2 = [number, number];

export const v2 = (x: number, y: number): Vec2 => [x, y];
export const add = (a: Vec2, b: Vec2): Vec2 => [a[0] + b[0], a[1] + b[1]];
export const sub = (a: Vec2, b: Vec2): Vec2 => [a[0] - b[0], a[1] - b[1]];
export const scale = (a: Vec2, s: number): Vec2 => [a[0] * s, a[1] * s];
export const dot = (a: Vec2, b: Vec2): number => a[0] * b[0] + a[1] * b[1];
export const cross = (a: Vec2, b: Vec2): number => a[0] * b[1] - a[1] * b[0];
export const len = (a: Vec2): number => Math.hypot(a[0], a[1]);
export const len2 = (a: Vec2): number => a[0] * a[0] + a[1] * a[1];
export const normalize = (a: Vec2): Vec2 => {
  const L = Math.hypot(a[0], a[1]) || 1;
  return [a[0] / L, a[1] / L];
};
export const rotate = (a: Vec2, ang: number): Vec2 => {
  const c = Math.cos(ang);
  const s = Math.sin(ang);
  return [a[0] * c - a[1] * s, a[0] * s + a[1] * c];
};
export const clamp = (x: number, lo: number, hi: number): number =>
  x < lo ? lo : x > hi ? hi : x;

// CellCraft multiplayer wire protocol.
//
// JSON-encoded messages with a discriminant `type` field. Keep this file
// browser-safe and server-safe (no imports that aren't pure TS / sim).
//
// The sim is server-authoritative; clients submit ActionProposals and the
// server broadcasts periodic S_STATE snapshots. Client-side prediction
// (see net/client.ts) runs the same sim locally for the player monster.

import type { ActionProposal } from '../sim/action';
import type { Part } from '../sim/part';

export const DEFAULT_MP_PORT = 7781;

// --- Client → Server --------------------------------------------------

export interface C_JOIN {
  type: 'C_JOIN';
  name: string;
  starter_id: string;
  // Fully resolved starter spec — server has no artifact loader, so the
  // client hands over the parts/color/seed directly.
  color: [number, number, number];
  parts: Part[];
  seed: number;
}

export interface C_ACTION {
  type: 'C_ACTION';
  seq: number;                  // monotonic per-client counter
  action: ActionProposal;
  // Client-predicted position at the time this action was sent. Server
  // uses it for divergence checks (snap-back if > tolerance).
  clientPos?: [number, number];
}

export interface C_PING {
  type: 'C_PING';
  t: number; // client timestamp (ms)
}

export type ClientMsg = C_JOIN | C_ACTION | C_PING;

// --- Server → Client --------------------------------------------------

export interface S_WELCOME {
  type: 'S_WELCOME';
  playerId: number;
  tick: number;
  worldSeed: number;
  mapRadius: number;
}

// Compact entity snapshot — matches the fields the renderer needs.
// We keep it small so S_STATE fits in one TCP segment at N~16 entities.
export interface EntitySnap {
  id: number;
  owner: number;
  pos: [number, number];
  vel: [number, number];
  heading: number;
  hp: number;
  hp_max: number;
  biomass: number;
  tier: number;
  body_scale: number;
  alive: boolean;
  // Appearance — sent once at spawn, elided on subsequent snapshots to
  // save bytes. If missing, client keeps the last known value.
  color?: [number, number, number];
  parts?: unknown; // PartSpec-compatible; server treats as opaque
  seed?: number;
  isPlayer?: boolean;
}

export interface FoodSnap {
  id: number;
  kind: 'PLANT' | 'MEAT';
  pos: [number, number];
  biomass: number;
  radius: number;
  seed: number;
}

// Sim events forwarded to clients for HUD effects (bites, kills, etc).
export type NetSimEvent = {
  type: string;
  [k: string]: unknown;
};

export interface S_STATE {
  type: 'S_STATE';
  tick: number;
  // Server wall-clock when this snapshot was produced (ms). Clients
  // buffer ~100ms to interpolate non-player entities smoothly.
  serverTime: number;
  // Latest acknowledged action seq from the recipient. Client uses this
  // to prune its prediction ring buffer and decide what to replay.
  ackSeq: number;
  entities: EntitySnap[];
  removed: number[];           // entity IDs removed since last snap
  food: FoodSnap[];            // full food state (small, easier than diff)
  foodRemoved: number[];       // convenience for event playback
  events: NetSimEvent[];
}

export interface S_PONG {
  type: 'S_PONG';
  t: number; // echoed from C_PING
}

// Reject a misbehaved action (rate limit, speed cap, etc). Client is
// expected to drop it from its pending buffer.
export interface S_REJECT {
  type: 'S_REJECT';
  seq: number;
  reason: string;
}

export type ServerMsg = S_WELCOME | S_STATE | S_PONG | S_REJECT;

// --- Helpers ----------------------------------------------------------

export function encode(msg: ClientMsg | ServerMsg): string {
  return JSON.stringify(msg);
}

export function decodeClient(raw: string): ClientMsg | null {
  try {
    const m = JSON.parse(raw) as ClientMsg;
    if (!m || typeof (m as { type?: string }).type !== 'string') return null;
    return m;
  } catch {
    return null;
  }
}

export function decodeServer(raw: string): ServerMsg | null {
  try {
    const m = JSON.parse(raw) as ServerMsg;
    if (!m || typeof (m as { type?: string }).type !== 'string') return null;
    return m;
  } catch {
    return null;
  }
}

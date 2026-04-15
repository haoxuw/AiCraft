// GameServer — owns the World, ticks sim at 30 Hz, broadcasts to clients.
//
// Per CLAUDE.md Rule 3 (server-authoritative) + Rule 4 (no AI on server):
// in this MP PoC, the server runs NO AI. Only connected players exist in
// the world. Clients submit MOVE ActionProposals; server validates and
// applies them.

import { WebSocket } from 'ws';
import {
  C_ACTION,
  C_JOIN,
  ClientMsg,
  encode,
  EntitySnap,
  FoodSnap,
  NetSimEvent,
  S_REJECT,
  S_STATE,
  S_WELCOME
} from '../../src/net/protocol.js';
import { Action, ActionType } from '../../src/sim/action.js';
import { tick } from '../../src/sim/sim.js';
import { makeWorld, scatterFood, spawnMonster, World } from '../../src/sim/world.js';

const TICK_HZ = 30;
const TICK_DT = 1 / TICK_HZ;
const BROADCAST_EVERY_TICKS = 1; // i.e. 30 Hz broadcast

// Safety knobs.
const MAX_ACTIONS_PER_SEC = 60;
const SPEED_EXCEED_MULT = 1.5; // reject if |vel| > move_speed * 1.5

interface ClientCtx {
  ws: WebSocket;
  id: number;              // entity id owned by this client
  name: string;
  lastSeq: number;         // last applied action seq
  // Rolling window for rate-limiting (timestamps ms).
  actionTimes: number[];
  // Flags which fields we've already sent to avoid re-sending appearance.
  sentAppearance: Set<number>;
  alive: boolean;
}

export class GameServer {
  private world: World;
  private tickCount = 0;
  private clients = new Map<WebSocket, ClientCtx>();
  private timer: NodeJS.Timeout | null = null;
  private events: NetSimEvent[] = [];
  private removedSinceBroadcast: number[] = [];
  private foodRemovedSinceBroadcast: number[] = [];
  private readonly worldSeed: number;

  constructor(seed = Math.floor(Math.random() * 1e9)) {
    this.worldSeed = seed;
    this.world = makeWorld();
    let s = seed | 0;
    const rng = () => {
      s = (s * 1664525 + 1013904223) | 0;
      return ((s >>> 0) & 0xffff) / 0xffff;
    };
    scatterFood(this.world, 24, rng);
  }

  start(): void {
    if (this.timer) return;
    this.timer = setInterval(() => this.tickOnce(), Math.floor(1000 / TICK_HZ));
    // eslint-disable-next-line no-console
    console.log(`[server] ticking at ${TICK_HZ} Hz`);
  }

  stop(): void {
    if (this.timer) clearInterval(this.timer);
    this.timer = null;
  }

  // --- Connection lifecycle ------------------------------------------

  onConnect(ws: WebSocket): void {
    // No entity spawned yet — wait for C_JOIN.
    ws.on('message', (data) => {
      let msg: ClientMsg | null;
      try {
        msg = JSON.parse(String(data)) as ClientMsg;
      } catch {
        return;
      }
      if (!msg || typeof (msg as { type?: string }).type !== 'string') return;
      this.handleMessage(ws, msg);
    });
    ws.on('close', () => this.onDisconnect(ws));
    ws.on('error', () => this.onDisconnect(ws));
  }

  private onDisconnect(ws: WebSocket): void {
    const cc = this.clients.get(ws);
    if (!cc) return;
    this.clients.delete(ws);
    if (this.world.monsters.has(cc.id)) {
      this.world.monsters.delete(cc.id);
      this.removedSinceBroadcast.push(cc.id);
    }
    // eslint-disable-next-line no-console
    console.log(`[server] ${cc.name}#${cc.id} disconnected (players=${this.clients.size})`);
  }

  private handleMessage(ws: WebSocket, msg: ClientMsg): void {
    switch (msg.type) {
      case 'C_JOIN':
        this.handleJoin(ws, msg);
        break;
      case 'C_ACTION':
        this.handleAction(ws, msg);
        break;
      case 'C_PING':
        try {
          ws.send(encode({ type: 'S_PONG', t: msg.t }));
        } catch {
          /* ignore */
        }
        break;
    }
  }

  private handleJoin(ws: WebSocket, msg: C_JOIN): void {
    if (this.clients.has(ws)) return; // already joined
    // Spawn at a random point within 40% of map radius.
    const r = this.world.map_radius * 0.4;
    const ang = Math.random() * Math.PI * 2;
    const pos: [number, number] = [Math.cos(ang) * r, Math.sin(ang) * r];
    const mon = spawnMonster(this.world, {
      pos,
      baseRadius: 46,
      color: msg.color,
      isPlayer: true,
      seed: msg.seed,
      parts: msg.parts
    });
    const cc: ClientCtx = {
      ws,
      id: mon.id,
      name: msg.name || `player#${mon.id}`,
      lastSeq: 0,
      actionTimes: [],
      sentAppearance: new Set(),
      alive: true
    };
    this.clients.set(ws, cc);
    const welcome: S_WELCOME = {
      type: 'S_WELCOME',
      playerId: mon.id,
      tick: this.tickCount,
      worldSeed: this.worldSeed,
      mapRadius: this.world.map_radius
    };
    try {
      ws.send(encode(welcome));
    } catch {
      /* ignore */
    }
    // eslint-disable-next-line no-console
    console.log(`[server] ${cc.name}#${mon.id} joined (players=${this.clients.size})`);
  }

  private handleAction(ws: WebSocket, msg: C_ACTION): void {
    const cc = this.clients.get(ws);
    if (!cc) return;
    const now = Date.now();
    // Rate-limit.
    cc.actionTimes = cc.actionTimes.filter((t) => now - t < 1000);
    cc.actionTimes.push(now);
    if (cc.actionTimes.length > MAX_ACTIONS_PER_SEC) {
      this.reject(ws, msg.seq, 'rate-limit');
      return;
    }
    // Validate — actions must target the client's own entity.
    if (msg.action.monster_id !== cc.id) {
      this.reject(ws, msg.seq, 'id-mismatch');
      return;
    }
    const mon = this.world.monsters.get(cc.id);
    if (!mon || !mon.alive) {
      this.reject(ws, msg.seq, 'dead');
      return;
    }
    if (msg.action.type === ActionType.MOVE) {
      const v = msg.action.vel;
      const mag = Math.hypot(v[0], v[1]);
      if (mag > mon.move_speed * SPEED_EXCEED_MULT) {
        this.reject(ws, msg.seq, 'speed');
        return;
      }
      // Client prediction divergence check — snap back if too far.
      if (msg.clientPos) {
        const dx = msg.clientPos[0] - mon.core_pos[0];
        const dy = msg.clientPos[1] - mon.core_pos[1];
        if (dx * dx + dy * dy > 64 * 64) {
          // Too divergent — silently ignore and let the next S_STATE
          // snap the client.
        }
      }
      this.queuedActions.push(msg.action);
    } else {
      // RELOCATE/CONVERT/INTERACT — pass through (sim treats as no-op at v1).
      this.queuedActions.push(msg.action);
    }
    cc.lastSeq = Math.max(cc.lastSeq, msg.seq);
  }

  private reject(ws: WebSocket, seq: number, reason: string): void {
    const r: S_REJECT = { type: 'S_REJECT', seq, reason };
    try {
      ws.send(encode(r));
    } catch {
      /* ignore */
    }
  }

  // --- Tick + broadcast ----------------------------------------------

  private queuedActions: Action[] = [];

  private tickOnce(): void {
    this.tickCount++;
    const actions = this.queuedActions;
    this.queuedActions = [];
    const ev = tick(this.world, actions, TICK_DT);
    for (const e of ev) this.events.push(e as unknown as NetSimEvent);

    // Remove-on-death: check which clients' entities disappeared.
    for (const cc of this.clients.values()) {
      if (cc.alive && !this.world.monsters.has(cc.id)) {
        cc.alive = false;
        this.removedSinceBroadcast.push(cc.id);
      }
    }

    if (this.tickCount % BROADCAST_EVERY_TICKS === 0) this.broadcast();
  }

  private broadcast(): void {
    const serverTime = Date.now();
    // Build per-entity snaps once; appearance fields are sent per-client
    // (so a newly-joined client gets full colors for existing monsters).
    const baseSnaps: EntitySnap[] = [];
    for (const m of this.world.monsters.values()) {
      baseSnaps.push({
        id: m.id,
        owner: m.owner,
        pos: [m.core_pos[0], m.core_pos[1]],
        vel: [m.vel[0], m.vel[1]],
        heading: m.heading,
        hp: m.hp,
        hp_max: m.hp_max,
        biomass: m.biomass,
        tier: m.tier,
        body_scale: m.body_scale,
        alive: m.alive
      });
    }
    const foodSnaps: FoodSnap[] = this.world.food.map((f) => ({
      id: f.id,
      kind: f.kind,
      pos: [f.pos[0], f.pos[1]],
      biomass: f.biomass,
      radius: f.radius,
      seed: f.seed
    }));

    for (const cc of this.clients.values()) {
      // Add appearance fields for entities this client hasn't seen yet.
      const snaps: EntitySnap[] = baseSnaps.map((s) => {
        if (cc.sentAppearance.has(s.id)) return s;
        const m = this.world.monsters.get(s.id);
        if (!m) return s;
        cc.sentAppearance.add(s.id);
        return {
          ...s,
          color: [m.color[0], m.color[1], m.color[2]],
          parts: m.parts,
          seed: m.noise_seed,
          isPlayer: m.is_player
        };
      });
      const state: S_STATE = {
        type: 'S_STATE',
        tick: this.tickCount,
        serverTime,
        ackSeq: cc.lastSeq,
        entities: snaps,
        removed: this.removedSinceBroadcast.slice(),
        food: foodSnaps,
        foodRemoved: this.foodRemovedSinceBroadcast.slice(),
        events: this.events.slice()
      };
      try {
        cc.ws.send(encode(state));
      } catch {
        /* ignore */
      }
    }
    this.events.length = 0;
    this.removedSinceBroadcast.length = 0;
    this.foodRemovedSinceBroadcast.length = 0;
  }
}

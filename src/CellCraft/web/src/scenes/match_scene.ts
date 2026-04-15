import * as THREE from 'three';
import { AIStateMap, decideAll, makeAIStateMap, resetAI } from '../ai/simple_ai';
import { getMonster } from '../artifacts';
import { partsFromSpec } from '../artifacts/spawn';
import {
  makeGlassPanel,
  makePillBadge,
  makeRingProgress,
  makeStatBar,
  makeText,
  RingProgressHandle,
  StatBarHandle,
  UI_PALETTE
} from '../render/ui';
import { NetClient } from '../net/client';
import { EntitySnap, FoodSnap, S_STATE, S_WELCOME } from '../net/protocol';
import { Action, ActionType } from '../sim/action';
import { makeMonster, Monster, refreshStats } from '../sim/monster';
import { Part } from '../sim/part';
import { tick } from '../sim/sim';
import * as perf from '../perf';
import { FoodKind } from '../sim/world';
import { TIER_COUNT, TIER_SIZE_MULTS, TIER_THRESHOLDS } from '../sim/tuning';
import { configureWorld, makeWorld, scatterFood, spawnMonster, World } from '../sim/world';
import { buttonHit, makeMenuButton, MenuButtonHandle, pointerToHud } from './menu_widgets';
import { makeEndScene } from './end_scene';
import { makeMainMenuScene } from './main_menu_scene';
import { Scene, SceneCtx, disposeGroup } from './scene';
import { makeTierUpOverlay, TierUpOverlay } from './tier_up_scene';

// Sim ticks at a fixed 30 Hz (inner) / 15 Hz (outer) regardless of render
// rate. The renderer interpolates monster positions between the two most
// recent sim snapshots so motion stays smooth at high FPS.
const FIXED_DT = 1 / 30;
const OUTER_FIXED_DT = 1 / 15;
const BASE_SPEED = 360;

export interface MatchStarter {
  id: string;
  name: string;
  color: [number, number, number];
  parts: Part[];
  seed: number;
}

export interface MatchOpts {
  starter: MatchStarter;
  // When set, connect to the given WebSocket URL and drive the match
  // from server state. Omit (or pass undefined) for fully-offline SP.
  mpUrl?: string;
}

export interface MatchStats {
  starterName: string;
  survivedSec: number;
  kills: number;
  peakTier: number;
  biomassCollected: number;
  finalBiomass: number;
}

function buildWorld(starter: MatchStarter): { world: World; player: Monster } {
  const world = makeWorld();
  scatterFood(world, 12);
  const player = spawnMonster(world, {
    pos: [0, 0],
    baseRadius: 46,
    color: starter.color,
    isPlayer: true,
    seed: starter.seed,
    parts: starter.parts
  });

  const r = 520;
  const ringIds: Array<{ pos: [number, number]; id: string }> = [
    { pos: [r, 0], id: 'base:pricklet' },
    { pos: [-r, 0], id: 'base:shelly' },
    { pos: [0, r], id: 'base:fang' },
    { pos: [0, -r], id: 'base:zip' }
  ];
  for (const c of ringIds) {
    const def = getMonster(c.id);
    if (!def) continue;
    spawnMonster(world, {
      pos: c.pos,
      baseRadius: 38 + (def.seed % 7),
      color: def.color,
      seed: def.seed,
      parts: partsFromSpec(def.parts),
      behaviorId: def.behavior
    });
  }

  return { world, player };
}

// Build the outer ("background") world — 2× map radius, 4 AI-only
// creatures, no player. Mirrors App::enterMatch() in native app.cpp.
// IDs start at OUTER_ID_BASE so they never collide with inner-world IDs
// in the shared AI state / events.
const OUTER_ID_BASE = 100000;
function buildOuterWorld(seed: number, scale: number = 2.0): World {
  const w = makeWorld();
  w.next_id = OUTER_ID_BASE;
  configureWorld(w, scale);
  // Outer world is decorative — a cheap silhouette layer. Two creatures
  // are plenty to imply "other cells out there" without sim/render cost.
  const ids = ['base:pricklet', 'base:fang'];
  const defs = ids.map((id) => getMonster(id)).filter((d): d is NonNullable<typeof d> => !!d);
  const N = defs.length;
  const r = w.map_radius * 0.5;
  for (let i = 0; i < N; ++i) {
    const a = (2 * Math.PI * i) / N;
    const pos: [number, number] = [Math.cos(a) * r, Math.sin(a) * r];
    const tierMult = TIER_SIZE_MULTS[2] / TIER_SIZE_MULTS[1];
    spawnMonster(w, {
      pos,
      baseRadius: (38 + i) * tierMult,
      color: defs[i].color,
      parts: partsFromSpec(defs[i].parts),
      seed: seed ^ (0xD1A11E5 + i),
      behaviorId: defs[i].behavior
    });
  }
  // Outer world food is not rendered (silhouette layer only) — skip
  // scattering it to keep outer sim cost minimal.
  return w;
}

// Build a fresh inner world for a tier-up migration. The player is
// carried over with its current biomass/parts/color but re-placed at
// the center; four fresh AI opponents spawn at ring positions.
function buildMigratedInner(
  _starter: MatchStarter,
  carry: Monster,
  seed: number
): { world: World; player: Monster } {
  const world = makeWorld();
  scatterFood(world, 12);
  // Re-spawn the player with carried parts/color but fresh shape at the
  // starter baseRadius — refreshStats() then re-derives stats, and we
  // restore biomass/tier/lifetime_biomass so the tier carries over.
  const player = spawnMonster(world, {
    pos: [0, 0],
    baseRadius: 46,
    color: carry.color,
    isPlayer: true,
    seed,
    parts: carry.parts
  });
  player.biomass = carry.biomass;
  player.lifetime_biomass = carry.lifetime_biomass;
  player.tier = carry.tier;
  player.body_scale = carry.body_scale;
  // Apply the tier's body scale to the new shape so the player visibly
  // owns the new arena.
  if (carry.body_scale !== 1.0) {
    for (const v of player.shape) {
      v[0] *= carry.body_scale;
      v[1] *= carry.body_scale;
    }
  }
  // refreshStats happens inside spawnMonster via the shape area; after
  // manual edits above, re-derive hp_max for the new biomass.
  player.hp_max = Math.max(1, player.biomass * 2 * player.part_effect.hp_mult);
  player.hp = player.hp_max;

  const r = 520;
  const migrants: Array<{ pos: [number, number]; id: string; seedOff: number }> = [
    { pos: [r, 0], id: 'base:pricklet', seedOff: 11 },
    { pos: [-r, 0], id: 'base:shelly', seedOff: 12 },
    { pos: [0, r], id: 'base:fang', seedOff: 13 },
    { pos: [0, -r], id: 'base:zip', seedOff: 14 }
  ];
  for (const c of migrants) {
    const def = getMonster(c.id);
    if (!def) continue;
    spawnMonster(world, {
      pos: c.pos,
      baseRadius: 38 + ((seed + c.seedOff) % 7),
      color: def.color,
      seed: seed + c.seedOff,
      parts: partsFromSpec(def.parts),
      behaviorId: def.behavior
    });
  }
  return { world, player };
}

export function makeMatchScene(opts: MatchOpts): Scene {
  // Sim state.
  let world: World;
  let player: Monster;
  let outerWorld: World | null = null;
  const innerAI: AIStateMap = makeAIStateMap();
  let outerAI: AIStateMap = makeAIStateMap();
  let goTo: [number, number] | null = null;
  let acc = 0;
  let outerAcc = 0;
  let enteredAt = 0;

  // Interpolation snapshots: position/heading at the previous and current
  // sim tick boundaries, per monster id. Render reads `lerp(prev, cur,
  // alpha)` with alpha = acc / FIXED_DT, then restores authoritative
  // values so the next sim tick reads untouched state.
  interface InterpSnap { prev: [number, number]; cur: [number, number]; prevH: number; curH: number; }
  const innerSnaps = new Map<number, InterpSnap>();
  const outerSnaps = new Map<number, InterpSnap>();
  // Authoritative (un-interpolated) state stashed across render.
  const authStash: Array<{ m: Monster; pos: [number, number]; h: number }> = [];

  function captureSnap(w: World, snaps: Map<number, InterpSnap>): void {
    for (const m of w.monsters.values()) {
      const s = snaps.get(m.id);
      if (s) {
        s.prev = s.cur;
        s.prevH = s.curH;
        s.cur = [m.core_pos[0], m.core_pos[1]];
        s.curH = m.heading;
      } else {
        snaps.set(m.id, {
          prev: [m.core_pos[0], m.core_pos[1]],
          cur: [m.core_pos[0], m.core_pos[1]],
          prevH: m.heading,
          curH: m.heading
        });
      }
    }
    // Drop stale entries for monsters that no longer exist.
    for (const id of snaps.keys()) {
      if (!w.monsters.has(id)) snaps.delete(id);
    }
  }

  function applyInterp(w: World, snaps: Map<number, InterpSnap>, alpha: number): void {
    for (const m of w.monsters.values()) {
      const s = snaps.get(m.id);
      if (!s) continue;
      authStash.push({ m, pos: [m.core_pos[0], m.core_pos[1]], h: m.heading });
      m.core_pos = [
        s.prev[0] + (s.cur[0] - s.prev[0]) * alpha,
        s.prev[1] + (s.cur[1] - s.prev[1]) * alpha
      ];
      let dh = s.curH - s.prevH;
      while (dh > Math.PI) dh -= 2 * Math.PI;
      while (dh < -Math.PI) dh += 2 * Math.PI;
      m.heading = s.prevH + dh * alpha;
    }
  }

  function restoreInterp(): void {
    for (const e of authStash) {
      e.m.core_pos = e.pos;
      e.m.heading = e.h;
    }
    authStash.length = 0;
  }

  // Stats.
  let kills = 0;
  let peakTier = 1;
  let biomassCollected = 0;
  const initialBiomass = { v: 0 };

  // HUD.
  const hudGroup = new THREE.Group();
  let hpBar: StatBarHandle | null = null;
  let xpRing: RingProgressHandle | null = null;
  let tierPillGroup: THREE.Group | null = null;
  let currentTierPill: THREE.Group | null = null;
  let statsText: THREE.Object3D | null = null;
  let hintText: THREE.Object3D | null = null;
  let dangerText: THREE.Object3D | null = null;

  // Pause overlay.
  let paused = false;
  const pauseGroup = new THREE.Group();
  let pauseBtns: MenuButtonHandle[] = [];
  let pauseSelected = 0;

  // Tier-up celebrate overlay (attached to hudScene when active).
  let tierUp: TierUpOverlay | null = null;

  // --- Multiplayer state ---
  const isMP = !!opts.mpUrl;
  let net: NetClient | null = null;
  let mpPlayerId: number = 0;       // server-assigned entity id
  let mpJoined = false;             // sent C_JOIN
  let mpWelcomed = false;           // got S_WELCOME
  const MP_SNAP_TOLERANCE = 8;      // world units — below this, accept local prediction
  const MP_INTERP_DELAY_MS = 100;   // render non-player entities this far in the past
  // Latest server snapshot (for interpolation / override).
  const mpAppearance = new Map<number, {
    color: [number, number, number];
    parts: Part[];
    seed: number;
    isPlayer: boolean;
  }>();
  // Ring buffer of predicted player positions keyed by seq.
  const mpPredicted = new Map<number, [number, number]>();
  // Two-snapshot interpolation buffer per non-player entity id.
  interface InterpSlot {
    t0: number; p0: [number, number]; h0: number;
    t1: number; p1: [number, number]; h1: number;
  }
  const mpInterp = new Map<number, InterpSlot>();
  // Estimated server→client clock offset: client_t - server_t (ms).
  let mpServerClockOffset = 0;

  // MP HUD extras.
  let pingText: THREE.Object3D | null = null;
  let connGroup: THREE.Group | null = null;    // "Reconnecting…" overlay
  let connText: THREE.Object3D | null = null;
  let mpStatus: 'connecting' | 'open' | 'closed' | 'reconnecting' = 'connecting';

  // Apply an EntitySnap to the local world, creating the Monster if new.
  // For the local player, runs reconciliation — snapping back + replay
  // only when prediction has drifted past MP_SNAP_TOLERANCE. For others,
  // pushes into the interpolation buffer and leaves the render position
  // to be eased each frame.
  function applyEntitySnap(s: EntitySnap, serverTime: number, ackSeq: number): void {
    if (s.color && s.parts && typeof s.seed === 'number') {
      mpAppearance.set(s.id, {
        color: s.color,
        parts: s.parts as Part[],
        seed: s.seed,
        isPlayer: !!s.isPlayer
      });
    }
    let m = world.monsters.get(s.id);
    if (!m) {
      const ap = mpAppearance.get(s.id);
      if (!ap) return;
      m = makeMonster({
        id: s.id,
        pos: [s.pos[0], s.pos[1]],
        baseRadius: 46,
        color: ap.color,
        parts: ap.parts,
        seed: ap.seed,
        isPlayer: ap.isPlayer
      });
      refreshStats(m);
      world.monsters.set(s.id, m);
      if (s.id === mpPlayerId) player = m;
    }
    m.vel = [s.vel[0], s.vel[1]];
    m.hp = s.hp;
    m.hp_max = s.hp_max;
    m.biomass = s.biomass;
    m.tier = s.tier;
    m.body_scale = s.body_scale;
    m.alive = s.alive;

    if (s.id === mpPlayerId) {
      // Reconciliation: compare server pos at ackSeq to the prediction
      // we had at that seq. If drift > tolerance, snap and replay.
      const pred = mpPredicted.get(ackSeq);
      const sp: [number, number] = [s.pos[0], s.pos[1]];
      let drift = Infinity;
      if (pred) {
        drift = Math.hypot(pred[0] - sp[0], pred[1] - sp[1]);
      }
      if (!pred || drift > MP_SNAP_TOLERANCE) {
        m.core_pos = sp;
        m.heading = s.heading;
        // Replay all pending predicted inputs > ackSeq by re-integrating.
        // We grab them from net.pending in seq order.
        if (net) {
          const replays = Array.from(net.pending.values())
            .filter((p) => p.seq > ackSeq)
            .sort((a, b) => a.seq - b.seq);
          for (const r of replays) {
            if (r.msg.action.type === ActionType.MOVE) {
              tick(world, [r.msg.action], FIXED_DT);
            }
          }
        }
      }
      // Prune prediction buffer for acked seqs.
      for (const seq of mpPredicted.keys()) {
        if (seq <= ackSeq) mpPredicted.delete(seq);
      }
    } else {
      // Non-player: push into interp buffer. Hold last two snapshots.
      const prev = mpInterp.get(s.id);
      if (!prev) {
        mpInterp.set(s.id, {
          t0: serverTime, p0: [s.pos[0], s.pos[1]], h0: s.heading,
          t1: serverTime, p1: [s.pos[0], s.pos[1]], h1: s.heading
        });
        m.core_pos = [s.pos[0], s.pos[1]];
        m.heading = s.heading;
      } else {
        prev.t0 = prev.t1;
        prev.p0 = prev.p1;
        prev.h0 = prev.h1;
        prev.t1 = serverTime;
        prev.p1 = [s.pos[0], s.pos[1]];
        prev.h1 = s.heading;
      }
    }
  }

  // Ease non-player entities toward their interpolated position.
  function updateMPInterp(): void {
    if (!isMP) return;
    // Target wall-clock (client time) we want to render at.
    const renderServerT = Date.now() - mpServerClockOffset - MP_INTERP_DELAY_MS;
    for (const [id, slot] of mpInterp) {
      const m = world.monsters.get(id);
      if (!m) continue;
      const span = Math.max(1, slot.t1 - slot.t0);
      let t = (renderServerT - slot.t0) / span;
      if (!isFinite(t)) t = 1;
      if (t < 0) t = 0;
      else if (t > 1) t = 1;
      m.core_pos = [
        slot.p0[0] + (slot.p1[0] - slot.p0[0]) * t,
        slot.p0[1] + (slot.p1[1] - slot.p0[1]) * t
      ];
      // Heading: shortest-arc lerp.
      let dh = slot.h1 - slot.h0;
      while (dh > Math.PI) dh -= 2 * Math.PI;
      while (dh < -Math.PI) dh += 2 * Math.PI;
      m.heading = slot.h0 + dh * t;
    }
  }

  function applyFoodSnaps(snaps: FoodSnap[]): void {
    world.food.length = 0;
    for (const f of snaps) {
      world.food.push({
        id: f.id,
        kind: f.kind === 'PLANT' ? FoodKind.PLANT : FoodKind.MEAT,
        pos: [f.pos[0], f.pos[1]],
        biomass: f.biomass,
        radius: f.radius,
        seed: f.seed
      });
    }
  }

  function onWelcome(w: S_WELCOME): void {
    mpPlayerId = w.playerId;
    world.map_radius = w.mapRadius;
    // Re-id the placeholder player to the server-assigned id so local
    // prediction ticks (which target mpPlayerId) affect our monster.
    if (player && player.id !== w.playerId) {
      world.monsters.delete(player.id);
      player.id = w.playerId;
      world.monsters.set(player.id, player);
    }
    mpWelcomed = true;
  }

  function onServerState(s: S_STATE): void {
    if (!mpWelcomed) return;
    // Update clock-offset estimate (client clock - server clock).
    mpServerClockOffset = Date.now() - s.serverTime;
    for (const es of s.entities) applyEntitySnap(es, s.serverTime, s.ackSeq);
    for (const rid of s.removed) {
      world.monsters.delete(rid);
      mpAppearance.delete(rid);
      mpInterp.delete(rid);
    }
    applyFoodSnaps(s.food);
    if (net) net.ackUpTo(s.ackSeq);
    // Surface simple events for HUD counters.
    for (const ev of s.events) {
      if (ev.type === 'PICKUP' && ev.monster === mpPlayerId) {
        biomassCollected += (ev.gained as number) || 0;
      } else if (ev.type === 'KILL' && ev.killer === mpPlayerId) {
        kills += 1;
        biomassCollected += (ev.biomass as number) || 0;
      }
    }
  }

  // Tracks which monster IDs existed last tick so we can detect "player died".
  let onResize: (() => void) | null = null;

  function layoutHud(w: number, h: number): void {
    // Bottom-center cluster: XP ring, HP bar, tier pill.
    const cy = -h / 2 + 56;
    if (hpBar) hpBar.group.position.set(0, cy, 0);
    if (xpRing) xpRing.group.position.set(-180, cy, 0);
    if (tierPillGroup) tierPillGroup.position.set(180, cy, 0);

    // Top-left mini-stats.
    if (statsText) statsText.position.set(-w / 2 + 20, h / 2 - 28, 0);

    // Bottom-right hint.
    if (hintText) hintText.position.set(w / 2 - 20, -h / 2 + 20, 0);

    // Danger pulse label anchored just above the HP bar.
    if (dangerText) dangerText.position.set(0, cy + 44, 0);

    // MP ping indicator: top-right.
    if (pingText) pingText.position.set(w / 2 - 20, h / 2 - 28, 0);
  }

  function buildTierPill(tier: number): THREE.Group {
    return makePillBadge(`TIER ${tier}`, { color: UI_PALETTE.neonAmber });
  }

  function buildPauseOverlay(): void {
    // Full-screen dim panel + two centered buttons.
    const bg = makeGlassPanel(420, 260, {
      radius: 24,
      tint: 0x0c1014,
      alpha: 0.92,
      borderColor: UI_PALETTE.neonCyan
    });
    pauseGroup.add(bg);

    const title = makeText('PAUSED', {
      size: 42,
      color: UI_PALETTE.paper,
      glow: UI_PALETTE.neonCyan,
      weight: 'bold'
    });
    title.position.set(0, 70, 0.1);
    pauseGroup.add(title);

    const resumeBtn = makeMenuButton('RESUME', { width: 260, height: 44, textSize: 18 });
    resumeBtn.group.position.set(0, 0, 0.1);
    pauseGroup.add(resumeBtn.group);

    const menuBtn = makeMenuButton('MAIN MENU', { width: 260, height: 44, textSize: 18 });
    menuBtn.group.position.set(0, -60, 0.1);
    pauseGroup.add(menuBtn.group);

    pauseBtns = [resumeBtn, menuBtn];
    pauseSelected = 0;
    resumeBtn.setFocused(true);
  }

  function setPauseSelection(i: number): void {
    pauseSelected = ((i % pauseBtns.length) + pauseBtns.length) % pauseBtns.length;
    for (let k = 0; k < pauseBtns.length; ++k) pauseBtns[k].setFocused(k === pauseSelected);
  }

  function setPaused(on: boolean, ctx: SceneCtx): void {
    if (on === paused) return;
    paused = on;
    if (on) {
      ctx.renderer.hudScene.add(pauseGroup);
    } else {
      ctx.renderer.hudScene.remove(pauseGroup);
    }
  }

  function activatePauseButton(i: number, ctx: SceneCtx): void {
    const btn = pauseBtns[i];
    if (!btn) return;
    if (btn.label === 'RESUME') {
      setPaused(false, ctx);
    } else if (btn.label === 'MAIN MENU') {
      ctx.requestGoto(() => makeMainMenuScene(), { fade: true });
    }
  }

  function tierProgress(m: Monster): number {
    // Fraction from current tier threshold to next tier threshold.
    const t = Math.max(1, Math.min(TIER_COUNT, m.tier));
    if (t >= TIER_COUNT) return 1.0;
    const lo = TIER_THRESHOLDS[t];
    const hi = TIER_THRESHOLDS[t + 1];
    if (hi <= lo) return 1.0;
    const frac = (m.lifetime_biomass - lo) / (hi - lo);
    return Math.max(0, Math.min(1, frac));
  }

  function currentStats(ctx: SceneCtx): MatchStats {
    return {
      starterName: opts.starter.name,
      survivedSec: ctx.now - enteredAt,
      kills,
      peakTier,
      biomassCollected,
      finalBiomass: player ? player.lifetime_biomass : 0
    };
  }

  function stepSim(dtReal: number, ctx: SceneCtx): void {
    acc += dtReal;
    while (acc >= FIXED_DT) {
      const actions: Action[] = [];

      // Player input → action.
      if (player && player.alive) {
        const intent = ctx.input.sample();
        if (intent.clickTarget) goTo = intent.clickTarget;
        if (intent.vel[0] !== 0 || intent.vel[1] !== 0) {
          goTo = null;
          actions.push({
            type: ActionType.MOVE,
            monster_id: player.id,
            vel: [intent.vel[0] * BASE_SPEED, intent.vel[1] * BASE_SPEED]
          });
        } else if (goTo) {
          const dx = goTo[0] - player.core_pos[0];
          const dy = goTo[1] - player.core_pos[1];
          const d = Math.hypot(dx, dy);
          if (d < 12) {
            goTo = null;
          } else {
            actions.push({
              type: ActionType.MOVE,
              monster_id: player.id,
              vel: [(dx / d) * BASE_SPEED, (dy / d) * BASE_SPEED]
            });
          }
        }
      }

      // --- Multiplayer branch ---
      if (isMP) {
        if (net) {
          // Drain inbound first so reconciliation uses fresh ack.
          for (const m of net.drain()) {
            if (m.type === 'S_WELCOME') onWelcome(m);
            else if (m.type === 'S_STATE') onServerState(m);
            else if (m.type === 'S_REJECT') net.pending.delete(m.seq);
          }
          if (net.getStatus() === 'open' && !mpJoined) {
            mpJoined = net.sendJoin({
              name: opts.starter.name,
              starter_id: opts.starter.id,
              color: opts.starter.color,
              parts: opts.starter.parts,
              seed: opts.starter.seed
            });
          }
          if (mpWelcomed) {
            // Re-point actions at the server-assigned player id, then
            // send + locally predict.
            for (const a of actions) {
              if (a.monster_id !== player.id) continue;
              a.monster_id = mpPlayerId;
              const seq = net.sendAction(a, [player.core_pos[0], player.core_pos[1]]);
              // Local prediction: run the sim forward one tick with
              // only this MOVE action. Non-player entities are also
              // advanced (via tick) but their positions get overwritten
              // by updateMPInterp() each frame, so that's fine.
              tick(world, [a], FIXED_DT);
              if (a.type === ActionType.MOVE && player) {
                mpPredicted.set(seq, [player.core_pos[0], player.core_pos[1]]);
              }
            }
            if (actions.length === 0) {
              // No input this tick: still integrate the damping / coast.
              tick(world, [], FIXED_DT);
            }
          }
        }
        acc -= FIXED_DT;
        continue;
      }

      const tAi = perf.start('sim.ai.inner');
      actions.push(...decideAll(world, FIXED_DT, innerAI));
      perf.end('sim.ai.inner', tAi);
      const tInner = perf.start('sim.tick.inner');
      const events = tick(world, actions, FIXED_DT);
      perf.end('sim.tick.inner', tInner);
      captureSnap(world, innerSnaps);

      for (const e of events) {
        if (e.type === 'PICKUP' && player && e.monster === player.id) {
          biomassCollected += e.gained;
        } else if (e.type === 'KILL' && player && e.killer === player.id) {
          kills += 1;
          biomassCollected += e.biomass;
        } else if (e.type === 'TIER_UP' && player && e.monster === player.id) {
          peakTier = Math.max(peakTier, e.tier);
          // Rebuild pill.
          if (tierPillGroup && currentTierPill) {
            tierPillGroup.remove(currentTierPill);
            disposeGroup(currentTierPill);
            currentTierPill = buildTierPill(e.tier);
            tierPillGroup.add(currentTierPill);
          }
          // Fire celebrate overlay.
          if (!tierUp) {
            tierUp = makeTierUpOverlay(e.tier, ctx.now);
            ctx.renderer.hudScene.add(tierUp.group);
          }
          if (e.tier >= TIER_COUNT) {
            // APEX: drop the outer world entirely — the background is
            // now just the chalky ocean signifying "you are the biggest
            // thing in the sea." Victory transition still fires after
            // the celebration plays.
            outerWorld = null;
            ctx.renderer.setOcean(true);
            setTimeout(() => {
              ctx.requestGoto(() => makeEndScene({ outcome: 'victory', stats: currentStats(ctx) }), { fade: true });
            }, 2500);
          } else {
            // Intermediate tier-up: migrate. The current inner world
            // becomes the new outer ("background"); a fresh inner arena
            // spawns with the tiered-up player at its center. Strictly
            // two layers, always, until APEX.
            const oldInner = world;
            // Stop the previous outer — player has outgrown it.
            outerWorld = oldInner;
            outerAI = innerAI;          // reuse inner AI state as outer
            innerAI.clear();            // fresh map for the new inner
            const migrated = buildMigratedInner(
              opts.starter,
              player,
              opts.starter.seed ^ (0xA11E + e.tier)
            );
            world = migrated.world;
            player = migrated.player;
            // Ensure the new inner isn't marked as dead on first tick
            // because of goTo leftovers.
            goTo = null;
          }
        } else if (e.type === 'DEATH' && player && e.victim === player.id) {
          const snap = currentStats(ctx);
          ctx.requestGoto(() => makeEndScene({ outcome: 'defeat', stats: snap }), { fade: true });
        }
      }

      acc -= FIXED_DT;
    }

    // Outer (background) sim — runs at 15 Hz, independent of inner rate.
    // AI-only, no player. Events intentionally discarded; HUD only
    // surfaces inner-world combat.
    if (outerWorld) {
      outerAcc += dtReal;
      // Cap to avoid spiral-of-death after a long stall.
      if (outerAcc > OUTER_FIXED_DT * 4) outerAcc = OUTER_FIXED_DT * 4;
      while (outerAcc >= OUTER_FIXED_DT) {
        const tAiO = perf.start('sim.ai.outer');
        const outerActions = decideAll(outerWorld, OUTER_FIXED_DT, outerAI);
        perf.end('sim.ai.outer', tAiO);
        const tO = perf.start('sim.tick.outer');
        tick(outerWorld, outerActions, OUTER_FIXED_DT);
        perf.end('sim.tick.outer', tO);
        captureSnap(outerWorld, outerSnaps);
        outerAcc -= OUTER_FIXED_DT;
      }
    }
  }

  return {
    enter(ctx) {
      enteredAt = ctx.now;
      resetAI();
      if (isMP) {
        // Empty local world — populated from server snapshots.
        world = makeWorld();
        // Placeholder player until S_WELCOME + S_STATE arrive. A dummy
        // monster keeps HUD safe from nulls; it gets replaced when the
        // real snap comes in.
        player = spawnMonster(world, {
          pos: [0, 0],
          baseRadius: 46,
          color: opts.starter.color,
          isPlayer: true,
          seed: opts.starter.seed,
          parts: opts.starter.parts
        });
        outerWorld = null;
        initialBiomass.v = 0;
        net = new NetClient({
          url: opts.mpUrl!,
          autoReconnect: true,
          onStatus: (s) => {
            mpStatus = s;
            if (s !== 'open' && s !== 'closed') mpJoined = false;
          }
        });
      } else {
        const built = buildWorld(opts.starter);
        world = built.world;
        player = built.player;
        outerWorld = buildOuterWorld(opts.starter.seed);
        outerAI = makeAIStateMap();
        initialBiomass.v = player.lifetime_biomass;
      }
      ctx.renderer.setOcean(false);

      // HUD build.
      hpBar = makeStatBar(280, 22, { color: UI_PALETTE.hpRed });
      hudGroup.add(hpBar.group);

      xpRing = makeRingProgress(26, 6, { color: UI_PALETTE.neonCyan });
      hudGroup.add(xpRing.group);

      tierPillGroup = new THREE.Group();
      currentTierPill = buildTierPill(1);
      tierPillGroup.add(currentTierPill);
      hudGroup.add(tierPillGroup);

      statsText = makeText('kills 0 · biomass 0', {
        size: 14,
        color: UI_PALETTE.paper,
        anchorX: 'left',
        anchorY: 'top',
        weight: 'bold'
      });
      hudGroup.add(statsText);

      hintText = makeText('WASD/arrows · click to steer · Esc to pause', {
        size: 12,
        color: UI_PALETTE.chalkSoft,
        anchorX: 'right',
        anchorY: 'bottom'
      });
      hudGroup.add(hintText);

      dangerText = makeText('DANGER', {
        size: 18,
        color: UI_PALETTE.hpRed,
        glow: UI_PALETTE.neonPink,
        weight: 'bold'
      });
      dangerText.visible = false;
      hudGroup.add(dangerText);

      ctx.renderer.hudScene.add(hudGroup);

      if (isMP) {
        pingText = makeText('— ms', {
          size: 14,
          color: UI_PALETTE.chalkSoft,
          anchorX: 'right',
          anchorY: 'top'
        });
        hudGroup.add(pingText);

        // Full-screen "Reconnecting…" overlay, hidden until status != open.
        connGroup = new THREE.Group();
        const panel = makeGlassPanel(360, 120, {
          radius: 20,
          tint: 0x0c1014,
          alpha: 0.9,
          borderColor: UI_PALETTE.neonAmber
        });
        connGroup.add(panel);
        connText = makeText('Connecting…', {
          size: 22,
          color: UI_PALETTE.paper,
          glow: UI_PALETTE.neonAmber,
          weight: 'bold'
        });
        connGroup.add(connText);
        connGroup.visible = false;
        ctx.renderer.hudScene.add(connGroup);
      }

      buildPauseOverlay();

      layoutHud(window.innerWidth, window.innerHeight);
      onResize = () => layoutHud(window.innerWidth, window.innerHeight);
      window.addEventListener('resize', onResize);
    },

    exit(ctx) {
      if (net) {
        net.close();
        net = null;
      }
      if (onResize) window.removeEventListener('resize', onResize);
      onResize = null;

      ctx.renderer.hudScene.remove(hudGroup);
      disposeGroup(hudGroup);
      ctx.renderer.hudScene.remove(pauseGroup);
      disposeGroup(pauseGroup);
      if (tierUp) {
        ctx.renderer.hudScene.remove(tierUp.group);
        disposeGroup(tierUp.group);
        tierUp = null;
      }
      hpBar = null;
      xpRing = null;
      tierPillGroup = null;
      currentTierPill = null;
      statsText = null;
      hintText = null;
      dangerText = null;
      pauseBtns = [];
      if (connGroup) {
        ctx.renderer.hudScene.remove(connGroup);
        disposeGroup(connGroup);
        connGroup = null;
      }
      connText = null;
      pingText = null;
      ctx.renderer.setLowHp(0);
      ctx.renderer.setOcean(false);
    },

    update(dtReal, ctx) {
      if (dtReal > 0.25) dtReal = 0.25;

      if (!paused) stepSim(dtReal, ctx);

      // Tier-up overlay timing.
      if (tierUp) {
        tierUp.update(ctx.now);
        if (tierUp.finished(ctx.now)) {
          ctx.renderer.hudScene.remove(tierUp.group);
          disposeGroup(tierUp.group);
          tierUp = null;
        }
      }

      // HUD state.
      if (player) {
        const hpFrac = Math.max(0, Math.min(1, player.hp / Math.max(1e-6, player.hp_max)));
        const low = hpFrac < 0.35 ? 1.0 - hpFrac / 0.35 : 0.0;
        ctx.renderer.setLowHp(low);
        if (hpBar) {
          hpBar.setValue(hpFrac);
          hpBar.setTime(ctx.now);
        }
        if (xpRing) xpRing.setValue(tierProgress(player));
        if (dangerText) {
          dangerText.visible = low > 0.35;
          // Blink with the pulse.
          dangerText.scale.setScalar(1 + Math.sin(ctx.now * 7) * 0.05);
        }
        if (statsText) {
          const txt = statsText as unknown as { text: string; sync(): void };
          const newTxt = `kills ${kills} · biomass ${Math.floor(biomassCollected)}`;
          if (txt.text !== newTxt) {
            txt.text = newTxt;
            txt.sync();
          }
        }
      }

      // Interpolate non-player MP entities toward their buffered server
      // snapshots before handing the world to the renderer.
      updateMPInterp();

      // MP HUD refresh.
      if (isMP) {
        if (pingText && net) {
          const txt = pingText as unknown as { text: string; sync(): void };
          const label = net.pingMs > 0 ? `${Math.round(net.pingMs)} ms` : '— ms';
          if (txt.text !== label) { txt.text = label; txt.sync(); }
        }
        if (connGroup && connText) {
          const needOverlay = mpStatus !== 'open';
          connGroup.visible = needOverlay;
          if (needOverlay) {
            const lbl =
              mpStatus === 'reconnecting' ? 'Reconnecting…' :
              mpStatus === 'closed' ? 'Disconnected' :
              'Connecting…';
            const t = connText as unknown as { text: string; sync(): void };
            if (t.text !== lbl) { t.text = lbl; t.sync(); }
          }
        }
      }

      // Render world (or paused: freeze at current time). Apply sim-tick
      // interpolation so high-Hz frames look smooth despite the 30 Hz
      // inner / 15 Hz outer tick rates.
      const innerAlpha = Math.max(0, Math.min(1, acc / FIXED_DT));
      const outerAlpha = Math.max(0, Math.min(1, outerAcc / OUTER_FIXED_DT));
      applyInterp(world, innerSnaps, innerAlpha);
      if (outerWorld) applyInterp(outerWorld, outerSnaps, outerAlpha);
      const cam: [number, number] = player
        ? [player.core_pos[0], player.core_pos[1]]
        : [0, 0];
      ctx.renderer.renderDual(world, outerWorld, cam, ctx.now);
      restoreInterp();
    },

    onKey(e, ctx) {
      if (e.type !== 'keydown') return;
      if (e.key === 'Escape') {
        setPaused(!paused, ctx);
        e.preventDefault();
        return;
      }
      if (paused) {
        if (e.key === 'ArrowUp' || e.key.toLowerCase() === 'w') setPauseSelection(pauseSelected - 1);
        else if (e.key === 'ArrowDown' || e.key.toLowerCase() === 's') setPauseSelection(pauseSelected + 1);
        else if (e.key === 'Enter' || e.key === ' ') activatePauseButton(pauseSelected, ctx);
        e.preventDefault();
        return;
      }
      // Skip tier-up celebration.
      if (tierUp && (e.key === 'Enter' || e.key === ' ')) {
        tierUp.skip(ctx.now);
      }
    },

    onPointer(e, ctx) {
      if (paused) {
        const [hx, hy] = pointerToHud(ctx.renderer.domElement, e.clientX, e.clientY);
        for (let i = 0; i < pauseBtns.length; ++i) {
          if (buttonHit(pauseBtns[i], hx, hy)) {
            if (e.type === 'pointermove') setPauseSelection(i);
            else if (e.type === 'pointerdown') {
              setPauseSelection(i);
              activatePauseButton(i, ctx);
            }
            return;
          }
        }
        return;
      }
      // In-match clicks fall through to Input's screenToWorld click-to-move
      // (handled natively by Input listener). Skip tier-up if present.
      if (tierUp && e.type === 'pointerdown') tierUp.skip(ctx.now);
    }
  };
}

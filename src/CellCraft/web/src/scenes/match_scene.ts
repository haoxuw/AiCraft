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
import { Action, ActionType } from '../sim/action';
import { Monster } from '../sim/monster';
import { Part } from '../sim/part';
import { tick } from '../sim/sim';
import { TIER_COUNT, TIER_SIZE_MULTS, TIER_THRESHOLDS } from '../sim/tuning';
import { configureWorld, makeWorld, scatterFood, spawnMonster, World } from '../sim/world';
import { buttonHit, makeMenuButton, MenuButtonHandle, pointerToHud } from './menu_widgets';
import { makeEndScene } from './end_scene';
import { makeMainMenuScene } from './main_menu_scene';
import { Scene, SceneCtx, disposeGroup } from './scene';
import { makeTierUpOverlay, TierUpOverlay } from './tier_up_scene';

const FIXED_DT = 1 / 60;
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
  scatterFood(world, 20);
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
      parts: partsFromSpec(def.parts)
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
  const ids = ['base:pricklet', 'base:shelly', 'base:fang', 'base:zip', 'base:thorn', 'base:drip'];
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
      seed: seed ^ (0xD1A11E5 + i)
    });
  }
  scatterFood(w, 16);
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
  scatterFood(world, 20);
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
      parts: partsFromSpec(def.parts)
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
  let enteredAt = 0;

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

      actions.push(...decideAll(world, FIXED_DT, innerAI));
      const events = tick(world, actions, FIXED_DT);

      // Outer (background) sim — AI-only, no player. Events intentionally
      // discarded; HUD only surfaces inner-world combat.
      if (outerWorld) {
        const outerActions = decideAll(outerWorld, FIXED_DT, outerAI);
        tick(outerWorld, outerActions, FIXED_DT);
      }

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
  }

  return {
    enter(ctx) {
      enteredAt = ctx.now;
      resetAI();
      const built = buildWorld(opts.starter);
      world = built.world;
      player = built.player;
      outerWorld = buildOuterWorld(opts.starter.seed);
      outerAI = makeAIStateMap();
      ctx.renderer.setOcean(false);
      initialBiomass.v = player.lifetime_biomass;

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

      buildPauseOverlay();

      layoutHud(window.innerWidth, window.innerHeight);
      onResize = () => layoutHud(window.innerWidth, window.innerHeight);
      window.addEventListener('resize', onResize);
    },

    exit(ctx) {
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

      // Render world (or paused: freeze at current time).
      const cam: [number, number] = player
        ? [player.core_pos[0], player.core_pos[1]]
        : [0, 0];
      ctx.renderer.renderDual(world, outerWorld, cam, ctx.now);
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

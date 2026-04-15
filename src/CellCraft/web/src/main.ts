import { decideAll, resetAI } from './ai/simple_ai';
import { Input } from './input/input';
import { Renderer } from './render/renderer';
import { DemoHudHandle, makeDemoHud } from './render/ui';
import { Action, ActionType } from './sim/action';
import { Monster } from './sim/monster';
import { Part, PartKind } from './sim/part';
import { tick } from './sim/sim';
import { makeWorld, scatterFood, spawnMonster, World } from './sim/world';

const FIXED_DT = 1 / 60;

interface Game {
  world: World;
  player: Monster;
  renderer: Renderer;
  input: Input;
  // Click-to-move target in world coords; cleared when reached.
  goTo: [number, number] | null;
  acc: number;
  lastT: number;
  hud: DemoHudHandle | null;
}

function buildWorld(): { world: World; player: Monster } {
  const world = makeWorld();
  scatterFood(world, 16);
  const player = spawnMonster(world, {
    pos: [0, 0],
    baseRadius: 46,
    color: [0.38, 0.78, 0.38],
    isPlayer: true,
    seed: 1001,
    parts: [{ kind: PartKind.MOUTH, anchor: [42, 0], scale: 1.1 }]
  });

  // 4 AI cells with varied loadouts (diets picked to exercise part effects).
  const r = 520;
  type AI = { pos: [number, number]; color: [number, number, number]; seed: number; parts: Part[] };
  const aiConfigs: AI[] = [
    // Carnivore: SPIKE + MOUTH
    { pos: [r, 0], color: [0.86, 0.4, 0.42], seed: 2001, parts: [
      { kind: PartKind.MOUTH, anchor: [34, 0], scale: 1.0 },
      { kind: PartKind.SPIKE, anchor: [38, 8], scale: 1.2 },
      { kind: PartKind.SPIKE, anchor: [38, -8], scale: 1.2 }
    ] },
    // Herbivore: REGEN + ARMOR + MOUTH
    { pos: [-r, 0], color: [0.55, 0.65, 0.9], seed: 2002, parts: [
      { kind: PartKind.MOUTH, anchor: [34, 0], scale: 1.0 },
      { kind: PartKind.REGEN, anchor: [-20, 12], scale: 1.0 },
      { kind: PartKind.ARMOR, anchor: [-24, 0], scale: 1.0 }
    ] },
    // Omnivore: mixed
    { pos: [0, r], color: [0.92, 0.78, 0.42], seed: 2003, parts: [
      { kind: PartKind.MOUTH, anchor: [34, 0], scale: 1.0 },
      { kind: PartKind.TEETH, anchor: [36, 6], scale: 1.0 },
      { kind: PartKind.REGEN, anchor: [-20, 0], scale: 0.8 }
    ] },
    // EYES + FLAGELLA scout
    { pos: [0, -r], color: [0.7, 0.5, 0.85], seed: 2004, parts: [
      { kind: PartKind.MOUTH, anchor: [34, 0], scale: 1.0 },
      { kind: PartKind.EYES, anchor: [28, 14], scale: 1.0 },
      { kind: PartKind.EYES, anchor: [28, -14], scale: 1.0 },
      { kind: PartKind.FLAGELLA, anchor: [-28, 0], scale: 1.2 }
    ] }
  ];
  for (const c of aiConfigs) {
    spawnMonster(world, {
      pos: c.pos,
      baseRadius: 38 + (c.seed % 7),
      color: c.color,
      seed: c.seed,
      parts: c.parts
    });
  }

  return { world, player };
}

function restart(g: Game): void {
  resetAI();
  const { world, player } = buildWorld();
  g.world = world;
  g.player = player;
  g.goTo = null;
}

function playerAction(g: Game): Action | null {
  const intent = g.input.sample();

  if (intent.restart) {
    restart(g);
    return null;
  }

  // Click sets a persistent goTo target; WASD overrides instantaneously.
  if (intent.clickTarget) g.goTo = intent.clickTarget;

  const baseSpeed = 360;
  if (intent.vel[0] !== 0 || intent.vel[1] !== 0) {
    g.goTo = null;
    return {
      type: ActionType.MOVE,
      monster_id: g.player.id,
      vel: [intent.vel[0] * baseSpeed, intent.vel[1] * baseSpeed]
    };
  }

  if (g.goTo) {
    const dx = g.goTo[0] - g.player.core_pos[0];
    const dy = g.goTo[1] - g.player.core_pos[1];
    const d = Math.hypot(dx, dy);
    if (d < 12) {
      g.goTo = null;
      return null;
    }
    return {
      type: ActionType.MOVE,
      monster_id: g.player.id,
      vel: [(dx / d) * baseSpeed, (dy / d) * baseSpeed]
    };
  }

  return null;
}

function frame(g: Game, tMs: number): void {
  const t = tMs / 1000;
  let dtReal = t - g.lastT;
  if (dtReal > 0.25) dtReal = 0.25; // tab-switch guard
  g.lastT = t;
  g.acc += dtReal;

  while (g.acc >= FIXED_DT) {
    const actions: Action[] = [];
    const pa = playerAction(g);
    if (pa) actions.push(pa);
    actions.push(...decideAll(g.world, FIXED_DT));
    const events = tick(g.world, actions, FIXED_DT);
    for (const e of events) {
      if (e.type === 'TIER_UP') {
        // Phase 1 placeholder; Phase 2 will flash the cell visually.
        // eslint-disable-next-line no-console
        console.log(`TIER_UP monster=${e.monster} → tier ${e.tier}`);
      }
    }
    g.acc -= FIXED_DT;
  }

  // Low-HP red pulse: ramps in as hp drops below 35%, full at 0%.
  const hpFrac = Math.max(0, Math.min(1, g.player.hp / Math.max(1e-6, g.player.hp_max)));
  const lowHp = hpFrac < 0.35 ? 1.0 - hpFrac / 0.35 : 0.0;
  g.renderer.setLowHp(lowHp);

  if (g.hud) {
    g.hud.setHp(hpFrac);
    // Demo XP: sinusoidal so we can see the ring animate.
    g.hud.setXp(0.5 + 0.5 * Math.sin(t * 0.6));
    g.hud.setTime(t);
    // Anchor HUD at bottom-center of screen.
    const h = window.innerHeight;
    g.hud.group.position.set(0, -h / 2 + 60, 0);
  }

  g.renderer.render(g.world, t);
  requestAnimationFrame((tt) => frame(g, tt));
}

function main(): void {
  const canvas = document.getElementById('canvas') as HTMLCanvasElement;
  const renderer = new Renderer(canvas);
  const input = new Input({
    screenToWorld: (cx, cy) => renderer.screenToWorld(cx, cy)
  });

  const { world, player } = buildWorld();

  // Dev flag: ?hud=1 attaches the demo HUD (HP bar + tier pill + XP ring).
  const params = new URLSearchParams(window.location.search);
  let hud: DemoHudHandle | null = null;
  if (params.get('hud') === '1') {
    hud = makeDemoHud();
    renderer.hudScene.add(hud.group);
  }

  const g: Game = {
    world,
    player,
    renderer,
    input,
    goTo: null,
    acc: 0,
    lastT: performance.now() / 1000,
    hud
  };

  // Demo transition: press T to fade-out → fade-in.
  window.addEventListener('keydown', (ev) => {
    if (ev.key !== 't' && ev.key !== 'T') return;
    if (g.renderer.fader.isAnimating()) return;
    const nowSec = () => performance.now() / 1000;
    void (async () => {
      await g.renderer.fader.fadeOut(450, nowSec());
      await g.renderer.fader.fadeIn(550, nowSec());
    })();
  });

  requestAnimationFrame((t) => frame(g, t));
}

main();

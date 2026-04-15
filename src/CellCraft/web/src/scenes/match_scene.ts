import * as THREE from 'three';
import { makeText, UI_PALETTE } from '../render/ui';
import { Part } from '../sim/part';
import { makeMainMenuScene } from './main_menu_scene';
import { Scene, disposeGroup } from './scene';

// Placeholder — the real match scene (sim + HUD + pause) lands in the
// next commit. For now we just show "Starting match…" and bounce the
// user back to the main menu on any key/click. This lets Commit A ship
// without pulling in the HUD + sim driver.

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

export function makeMatchScene(opts: MatchOpts): Scene {
  const hudGroup = new THREE.Group();
  let onResize: (() => void) | null = null;

  const layout = (_w: number, _h: number): void => {
    // centered, no extra positioning needed.
  };

  return {
    enter(ctx) {
      const title = makeText(`starting ${opts.starter.name}…`, {
        size: 32,
        color: UI_PALETTE.paper,
        glow: UI_PALETTE.neonCyan,
        weight: 'bold'
      });
      hudGroup.add(title);

      const hint = makeText('(match scene lands in next commit — press Esc)', {
        size: 14,
        color: UI_PALETTE.chalkSoft
      });
      hint.position.set(0, -40, 0);
      hudGroup.add(hint);

      ctx.renderer.hudScene.add(hudGroup);
      layout(window.innerWidth, window.innerHeight);
      onResize = () => layout(window.innerWidth, window.innerHeight);
      window.addEventListener('resize', onResize);
    },
    exit(ctx) {
      if (onResize) window.removeEventListener('resize', onResize);
      onResize = null;
      ctx.renderer.hudScene.remove(hudGroup);
      disposeGroup(hudGroup);
    },
    update(_dt, ctx) {
      ctx.renderer.renderEmpty(ctx.now);
    },
    onKey(e, ctx) {
      if (e.type !== 'keydown') return;
      ctx.requestGoto(() => makeMainMenuScene(), { fade: true });
      e.preventDefault();
    },
    onPointer(e, ctx) {
      if (e.type !== 'pointerdown') return;
      ctx.requestGoto(() => makeMainMenuScene(), { fade: true });
    }
  };
}

import * as THREE from 'three';
import { makeText, UI_PALETTE } from '../render/ui';
import { makeMainMenuScene } from './main_menu_scene';
import { Scene, disposeGroup } from './scene';
import { MatchStats } from './match_scene';

// Placeholder end screen — shows outcome + stats as text; any input
// returns to main menu. The real styled end screen (glass panel +
// restart/menu buttons) lands in the next commit.

export interface EndOpts {
  outcome: 'victory' | 'defeat';
  stats: MatchStats;
}

export function makeEndScene(opts: EndOpts): Scene {
  const hudGroup = new THREE.Group();
  let onResize: (() => void) | null = null;

  return {
    enter(ctx) {
      const title = makeText(opts.outcome === 'victory' ? 'APEX' : 'CONSUMED', {
        size: 64,
        color: UI_PALETTE.paper,
        glow: opts.outcome === 'victory' ? UI_PALETTE.neonAmber : UI_PALETTE.neonPink,
        weight: 'bold'
      });
      title.position.set(0, 60, 0);
      hudGroup.add(title);

      const stats = makeText(
        `kills ${opts.stats.kills}   peak tier ${opts.stats.peakTier}   biomass ${Math.floor(opts.stats.biomassCollected)}`,
        { size: 16, color: UI_PALETTE.chalkSoft }
      );
      stats.position.set(0, -20, 0);
      hudGroup.add(stats);

      const hint = makeText('press any key for main menu', {
        size: 14,
        color: UI_PALETTE.chalkSoft
      });
      hint.position.set(0, -60, 0);
      hudGroup.add(hint);

      ctx.renderer.hudScene.add(hudGroup);
      onResize = () => { /* no layout */ };
      window.addEventListener('resize', onResize);
      ctx.renderer.setLowHp(0);
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
    },
    onPointer(e, ctx) {
      if (e.type !== 'pointerdown') return;
      ctx.requestGoto(() => makeMainMenuScene(), { fade: true });
    }
  };
}

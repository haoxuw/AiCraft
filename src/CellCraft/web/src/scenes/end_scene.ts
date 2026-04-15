import * as THREE from 'three';
import { makeGlassPanel, makeText, UI_PALETTE } from '../render/ui';
import { buttonHit, makeMenuButton, MenuButtonHandle, pointerToHud } from './menu_widgets';
import { makeMainMenuScene } from './main_menu_scene';
import { makeStarterSelectScene } from './starter_select_scene';
import { Scene, SceneCtx, disposeGroup, advanceEnter, applyEnterOpacity } from './scene';
import { MatchStats } from './match_scene';

export interface EndOpts {
  outcome: 'victory' | 'defeat';
  stats: MatchStats;
}

function fmtTime(sec: number): string {
  const s = Math.max(0, Math.floor(sec));
  const mm = Math.floor(s / 60);
  const ss = s % 60;
  return `${mm}:${ss.toString().padStart(2, '0')}`;
}

export function makeEndScene(opts: EndOpts): Scene {
  const hudGroup = new THREE.Group();
  let titleObj: THREE.Object3D | null = null;
  let panel: THREE.Mesh | null = null;
  const statRows: THREE.Object3D[] = [];
  let restartBtn: MenuButtonHandle | null = null;
  let menuBtn: MenuButtonHandle | null = null;
  let selected = 0;
  let onResize: (() => void) | null = null;
  let enteredAt = 0;
  let enterT = 0;

  const victory = opts.outcome === 'victory';
  const titleText = victory ? 'APEX' : 'CONSUMED';
  const titleGlow = victory ? UI_PALETTE.neonAmber : UI_PALETTE.neonPink;

  const statLines = [
    `starter       ${opts.stats.starterName}`,
    `survived      ${fmtTime(opts.stats.survivedSec)}`,
    `kills         ${opts.stats.kills}`,
    `peak tier     ${opts.stats.peakTier}`,
    `biomass       ${Math.floor(opts.stats.biomassCollected)}`,
    `final mass    ${Math.floor(opts.stats.finalBiomass)}`
  ];

  const layout = (_w: number, h: number): void => {
    if (titleObj) titleObj.position.set(0, h * 0.28, 0);
    if (panel) panel.position.set(0, 20, 0);
    const lineStep = 26;
    const top = 20 + (statLines.length - 1) * 0.5 * lineStep;
    for (let i = 0; i < statRows.length; ++i) {
      statRows[i].position.set(-140, top - i * lineStep, 0.1);
    }
    if (restartBtn) restartBtn.group.position.set(-120, -h * 0.3, 0);
    if (menuBtn) menuBtn.group.position.set(120, -h * 0.3, 0);
  };

  function setSelected(i: number): void {
    const btns = [restartBtn, menuBtn].filter(Boolean) as MenuButtonHandle[];
    if (!btns.length) return;
    selected = ((i % btns.length) + btns.length) % btns.length;
    for (let k = 0; k < btns.length; ++k) btns[k].setFocused(k === selected);
  }

  function activate(i: number, ctx: SceneCtx): void {
    if (i === 0) {
      ctx.requestGoto(() => makeStarterSelectScene(), { fade: true });
    } else {
      ctx.requestGoto(() => makeMainMenuScene(), { fade: true });
    }
  }

  return {
    enter(ctx) {
      enteredAt = ctx.now;

      titleObj = makeText(titleText, {
        size: 96,
        color: UI_PALETTE.paper,
        glow: titleGlow,
        weight: 'bold'
      });
      hudGroup.add(titleObj);

      panel = makeGlassPanel(400, statLines.length * 26 + 40, {
        radius: 16,
        tint: 0x0c1014,
        alpha: 0.86,
        borderColor: UI_PALETTE.chalkSoft
      });
      hudGroup.add(panel);

      for (const line of statLines) {
        const t = makeText(line, {
          size: 16,
          color: UI_PALETTE.paper,
          anchorX: 'left',
          anchorY: 'middle'
        });
        hudGroup.add(t);
        statRows.push(t);
      }

      restartBtn = makeMenuButton('RESTART', { width: 200, height: 48, textSize: 20 });
      hudGroup.add(restartBtn.group);
      menuBtn = makeMenuButton('MAIN MENU', { width: 200, height: 48, textSize: 20 });
      hudGroup.add(menuBtn.group);
      setSelected(0);

      ctx.renderer.hudScene.add(hudGroup);
      layout(window.innerWidth, window.innerHeight);
      onResize = () => layout(window.innerWidth, window.innerHeight);
      window.addEventListener('resize', onResize);

      // Clear low-HP pulse from the match.
      ctx.renderer.setLowHp(0);
    },

    exit(ctx) {
      if (onResize) window.removeEventListener('resize', onResize);
      onResize = null;
      ctx.renderer.hudScene.remove(hudGroup);
      disposeGroup(hudGroup);
      statRows.length = 0;
      titleObj = null;
      panel = null;
      restartBtn = null;
      menuBtn = null;
    },

    update(dt, ctx) {
      enterT = advanceEnter(enterT, dt);
      hudGroup.position.y = (1 - enterT) * -20;
      applyEnterOpacity(hudGroup, enterT);
      if (titleObj) {
        const t = ctx.now - enteredAt;
        titleObj.scale.setScalar(1 + Math.sin(t * 1.2) * 0.02);
      }
      // No world to render; just the background board.
      ctx.renderer.renderEmpty(ctx.now);
    },

    onKey(e, ctx) {
      if (e.type !== 'keydown') return;
      if (e.key === 'ArrowLeft' || e.key.toLowerCase() === 'a') setSelected(selected - 1);
      else if (e.key === 'ArrowRight' || e.key.toLowerCase() === 'd') setSelected(selected + 1);
      else if (e.key === 'Enter' || e.key === ' ') activate(selected, ctx);
      else if (e.key === 'Escape') ctx.requestGoto(() => makeMainMenuScene(), { fade: true });
      else return;
      e.preventDefault();
    },

    onPointer(e, ctx) {
      const [hx, hy] = pointerToHud(ctx.renderer.domElement, e.clientX, e.clientY);
      const btns = [restartBtn, menuBtn].filter(Boolean) as MenuButtonHandle[];
      for (let i = 0; i < btns.length; ++i) {
        if (buttonHit(btns[i], hx, hy)) {
          if (e.type === 'pointermove') setSelected(i);
          else if (e.type === 'pointerdown') {
            setSelected(i);
            activate(i, ctx);
          }
          return;
        }
      }
    }
  };
}

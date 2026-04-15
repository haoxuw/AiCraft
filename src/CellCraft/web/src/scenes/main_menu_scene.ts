import * as THREE from 'three';
import { makeText, UI_PALETTE } from '../render/ui';
import { buttonHit, makeMenuButton, MenuButtonHandle, pointerToHud } from './menu_widgets';
import { Scene, SceneCtx, disposeGroup } from './scene';
import { makeStarterSelectScene } from './starter_select_scene';

// Main menu: title + [Play, Lab, Quit]. Keyboard up/down/enter or mouse.
//
// World layer: keeps the board material as the background (the arena
// noise already reads well as a main-menu backdrop). A faint empty
// world is rendered by the renderer; we don't add any cells. The HUD
// overlay owns the actual menu.

export function makeMainMenuScene(): Scene {
  const hudGroup = new THREE.Group();
  const buttons: MenuButtonHandle[] = [];
  let selected = 0;
  let enteredAt = 0;

  // Title text nodes — kept as refs for breathing animation.
  let titleObj: THREE.Object3D | null = null;
  let subtitleObj: THREE.Object3D | null = null;

  let onResize: (() => void) | null = null;
  let sceneCtxRef: SceneCtx | null = null;

  const layout = (_w: number, h: number): void => {
    // Title: upper third of screen.
    if (titleObj) titleObj.position.set(0, h * 0.22, 0);
    if (subtitleObj) subtitleObj.position.set(0, h * 0.22 - 80, 0);

    // Buttons: stacked vertically, centered, lower half.
    const gap = 72;
    const startY = -h * 0.05;
    for (let i = 0; i < buttons.length; ++i) {
      buttons[i].group.position.set(0, startY - i * gap, 0);
    }
  };

  function activate(index: number, ctx: SceneCtx): void {
    const btn = buttons[index];
    if (!btn || btn.disabled) return;
    btn.setPressed(true);
    setTimeout(() => btn.setPressed(false), 120);
    switch (btn.label) {
      case 'PLAY':
        ctx.requestGoto(() => makeStarterSelectScene(), { fade: true });
        break;
      case 'LAB':
        // Lab scene not in Phase 3 deliverables; reuse starter select
        // as a placeholder so the button isn't dead.
        ctx.requestGoto(() => makeStarterSelectScene(), { fade: true });
        break;
      case 'QUIT':
        // noop on web
        break;
    }
  }

  function setSelected(index: number): void {
    selected = ((index % buttons.length) + buttons.length) % buttons.length;
    for (let i = 0; i < buttons.length; ++i) buttons[i].setFocused(i === selected);
  }

  return {
    enter(ctx) {
      sceneCtxRef = ctx;
      enteredAt = ctx.now;

      titleObj = makeText('CellCraft', {
        size: 120,
        color: UI_PALETTE.chalk,
        glow: UI_PALETTE.neonCyan,
        weight: 'bold'
      });
      hudGroup.add(titleObj);

      subtitleObj = makeText('a chalk-cell arena', {
        size: 22,
        color: UI_PALETTE.chalkSoft,
        weight: 'regular'
      });
      hudGroup.add(subtitleObj);

      for (const lbl of ['PLAY', 'LAB', 'QUIT']) {
        const btn = makeMenuButton(lbl, { disabled: lbl === 'QUIT' });
        buttons.push(btn);
        hudGroup.add(btn.group);
      }
      setSelected(0);

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
      buttons.length = 0;
      titleObj = null;
      subtitleObj = null;
      sceneCtxRef = null;
    },
    update(_dt, ctx) {
      // Title breathing glow — subtle size pulse via scale.
      if (titleObj) {
        const t = ctx.now - enteredAt;
        const s = 1.0 + Math.sin(t * 0.8) * 0.012;
        titleObj.scale.setScalar(s);
      }
      ctx.renderer.renderEmpty(ctx.now);
    },
    onKey(e, ctx) {
      if (e.type !== 'keydown') return;
      if (e.key === 'ArrowUp' || e.key.toLowerCase() === 'w') {
        setSelected(selected - 1);
        e.preventDefault();
      } else if (e.key === 'ArrowDown' || e.key.toLowerCase() === 's') {
        setSelected(selected + 1);
        e.preventDefault();
      } else if (e.key === 'Enter' || e.key === ' ') {
        activate(selected, ctx);
        e.preventDefault();
      }
    },
    onPointer(e, ctx) {
      if (!sceneCtxRef) return;
      const [hx, hy] = pointerToHud(ctx.renderer.domElement, e.clientX, e.clientY);
      if (e.type === 'pointermove') {
        for (let i = 0; i < buttons.length; ++i) {
          if (buttonHit(buttons[i], hx, hy)) {
            if (i !== selected) setSelected(i);
            return;
          }
        }
      } else if (e.type === 'pointerdown') {
        for (let i = 0; i < buttons.length; ++i) {
          if (buttonHit(buttons[i], hx, hy)) {
            setSelected(i);
            activate(i, ctx);
            return;
          }
        }
      }
    }
  };
}

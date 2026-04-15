import * as THREE from 'three';
import { allStarters } from '../artifacts';
import { defToStarter } from '../artifacts/spawn';
import { makeText, UI_PALETTE } from '../render/ui';
import { makeMonster } from '../sim/monster';
import { Part } from '../sim/part';
import { buttonHit, makeMenuButton, MenuButtonHandle, pointerToHud } from './menu_widgets';
import { makeMatchScene } from './match_scene';
import { makeMainMenuScene } from './main_menu_scene';
import { Scene, disposeGroup } from './scene';

// Starter select: three cells on pedestals, slowly spinning. Pick one
// to start a match. Back button returns to main menu.
//
// The previews are real sim Monsters rendered via renderer.buildMonsterPreview(),
// but they live on the HUD layer so they're positioned in screen-pixel
// space around the screen center.

export interface Starter {
  id: string;
  name: string;
  blurb: string;
  color: [number, number, number];
  parts: Part[];
  seed: number;
}

function buildStarterList(): Starter[] {
  return allStarters().map(defToStarter);
}

export function makeStarterSelectScene(): Scene {
  const STARTERS: Starter[] = buildStarterList();
  const hudGroup = new THREE.Group();
  // One pedestal group per starter. We swap the preview mesh inside
  // each frame to animate the heading (cheap rebuild).
  const pedestals: THREE.Group[] = [];
  // Persistent per-starter state: current heading + dynamic mesh slot.
  const previewState: Array<{
    heading: number;
    currentMesh: THREE.Group | null;
  }> = [];
  const nameTexts: THREE.Object3D[] = [];
  const blurbTexts: THREE.Object3D[] = [];
  const ringHalos: THREE.Mesh[] = [];

  let selected = 0;
  let titleObj: THREE.Object3D | null = null;
  let hintObj: THREE.Object3D | null = null;
  let backBtn: MenuButtonHandle | null = null;
  let onResize: (() => void) | null = null;
  let enteredAt = 0;

  const PEDESTAL_Y = 20;
  const BACK_Y_FROM_BOTTOM = 60;

  const layout = (w: number, h: number): void => {
    if (titleObj) titleObj.position.set(0, h * 0.34, 0);
    if (hintObj) hintObj.position.set(0, h * 0.34 - 60, 0);

    // Three pedestals across the middle, equally spaced.
    const spacing = Math.min(320, w / 3.5);
    for (let i = 0; i < pedestals.length; ++i) {
      const x = (i - 1) * spacing;
      pedestals[i].position.set(x, PEDESTAL_Y, 0);
      nameTexts[i].position.set(x, PEDESTAL_Y - 110, 0);
      blurbTexts[i].position.set(x, PEDESTAL_Y - 140, 0);
    }

    if (backBtn) backBtn.group.position.set(0, -h / 2 + BACK_Y_FROM_BOTTOM, 0);
  };

  function setSelected(i: number): void {
    selected = ((i % STARTERS.length) + STARTERS.length) % STARTERS.length;
    for (let k = 0; k < ringHalos.length; ++k) {
      ringHalos[k].visible = k === selected;
    }
  }

  function pickStarter(i: number, ctx: Parameters<Scene['enter']>[0]): void {
    const s = STARTERS[i];
    ctx.requestGoto(() => makeMatchScene({ starter: s }), { fade: true });
  }

  return {
    enter(ctx) {
      enteredAt = ctx.now;

      titleObj = makeText('CHOOSE YOUR CELL', {
        size: 48,
        color: UI_PALETTE.chalk,
        glow: UI_PALETTE.neonCyan,
        weight: 'bold'
      });
      hudGroup.add(titleObj);

      hintObj = makeText('←/→ or click to select · Enter to confirm', {
        size: 16,
        color: UI_PALETTE.chalkSoft
      });
      hudGroup.add(hintObj);

      // Build one persistent pedestal group per starter.
      for (let i = 0; i < STARTERS.length; ++i) {
        const s = STARTERS[i];
        const group = new THREE.Group();

        // Subtle halo ring behind the preview for the selected cell.
        const haloGeom = new THREE.CircleGeometry(80, 48);
        const haloMat = new THREE.MeshBasicMaterial({
          color: UI_PALETTE.neonCyan,
          transparent: true,
          opacity: 0.12,
          depthTest: false,
          depthWrite: false
        });
        const halo = new THREE.Mesh(haloGeom, haloMat);
        halo.renderOrder = -1;
        halo.visible = false;
        group.add(halo);
        ringHalos.push(halo);

        hudGroup.add(group);
        pedestals.push(group);
        previewState.push({ heading: 0, currentMesh: null });

        const name = makeText(s.name, {
          size: 22,
          color: UI_PALETTE.paper,
          glow: UI_PALETTE.neonAmber,
          weight: 'bold'
        });
        hudGroup.add(name);
        nameTexts.push(name);

        const blurb = makeText(s.blurb, {
          size: 14,
          color: UI_PALETTE.chalkSoft
        });
        hudGroup.add(blurb);
        blurbTexts.push(blurb);
      }
      setSelected(0);

      backBtn = makeMenuButton('BACK', { width: 160, height: 42, textSize: 16 });
      hudGroup.add(backBtn.group);

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
      pedestals.length = 0;
      previewState.length = 0;
      nameTexts.length = 0;
      blurbTexts.length = 0;
      ringHalos.length = 0;
      titleObj = null;
      hintObj = null;
      backBtn = null;
    },

    update(dt, ctx) {
      const t = ctx.now - enteredAt;
      for (let i = 0; i < STARTERS.length; ++i) {
        const ped = pedestals[i];
        const state = previewState[i];
        state.heading += dt * 0.6 * (i === selected ? 1.3 : 0.7);

        // Rebuild preview mesh for this pedestal. Real Monster with
        // updated heading so the mouth sweeps around.
        const s = STARTERS[i];
        const mon = makeMonster({
          id: -1 - i,
          pos: [0, 0],
          baseRadius: 46,
          color: s.color,
          parts: s.parts,
          seed: s.seed
        });
        mon.heading = state.heading;
        const mesh = ctx.renderer.buildMonsterPreview(mon, t);
        // Selected one pulses slightly larger.
        mesh.scale.setScalar(i === selected ? 1.15 + Math.sin(t * 2.5) * 0.03 : 0.95);

        if (state.currentMesh) {
          ped.remove(state.currentMesh);
          disposeGroup(state.currentMesh);
        }
        ped.add(mesh);
        state.currentMesh = mesh;

        // Halo soft breathing on the selected slot.
        if (i === selected && ringHalos[i].material) {
          const m = ringHalos[i].material as THREE.MeshBasicMaterial;
          m.opacity = 0.12 + 0.05 * (0.5 + 0.5 * Math.sin(t * 2.0));
        }
      }
      ctx.renderer.renderEmpty(ctx.now);
    },

    onKey(e, ctx) {
      if (e.type !== 'keydown') return;
      if (e.key === 'ArrowLeft' || e.key.toLowerCase() === 'a') {
        setSelected(selected - 1);
        e.preventDefault();
      } else if (e.key === 'ArrowRight' || e.key.toLowerCase() === 'd') {
        setSelected(selected + 1);
        e.preventDefault();
      } else if (e.key === 'Enter' || e.key === ' ') {
        pickStarter(selected, ctx);
        e.preventDefault();
      } else if (e.key === 'Escape') {
        ctx.requestGoto(() => makeMainMenuScene(), { fade: true });
        e.preventDefault();
      }
    },

    onPointer(e, ctx) {
      const [hx, hy] = pointerToHud(ctx.renderer.domElement, e.clientX, e.clientY);

      if (backBtn && e.type === 'pointerdown' && buttonHit(backBtn, hx, hy)) {
        ctx.requestGoto(() => makeMainMenuScene(), { fade: true });
        return;
      }

      // Hit-test each pedestal as a ~90px circle.
      for (let i = 0; i < pedestals.length; ++i) {
        const p = pedestals[i].position;
        const d = Math.hypot(hx - p.x, hy - p.y);
        if (d < 90) {
          if (e.type === 'pointermove') {
            if (i !== selected) setSelected(i);
          } else if (e.type === 'pointerdown') {
            setSelected(i);
            pickStarter(i, ctx);
          }
          return;
        }
      }
    }
  };
}

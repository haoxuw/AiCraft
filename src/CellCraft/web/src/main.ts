import { loadAllArtifacts } from './artifacts';
import * as perf from './perf';
import { Input } from './input/input';
import { Renderer } from './render/renderer';
import { makeMainMenuScene } from './scenes/main_menu_scene';
import { SceneManager } from './scenes/scene_manager';

function main(): void {
  // Must run before any scene that resolves starters or behaviors.
  loadAllArtifacts();
  const canvas = document.getElementById('canvas') as HTMLCanvasElement;
  const renderer = new Renderer(canvas);
  const input = new Input({
    screenToWorld: (cx, cy) => renderer.screenToWorld(cx, cy)
  });

  const nowSec = (): number => performance.now() / 1000;
  const mgr = new SceneManager(renderer, input, nowSec);
  mgr.boot(() => makeMainMenuScene());

  // Scene-level input plumbing. The Input class already handles player
  // move/click for the match scene; scenes also get raw events for UI
  // (menus, pause).
  window.addEventListener('keydown', (e) => mgr.onKey(e));
  window.addEventListener('keyup', (e) => mgr.onKey(e));
  window.addEventListener('pointerdown', (e) => mgr.onPointer(e));
  window.addEventListener('pointermove', (e) => mgr.onPointer(e));

  // FPS overlay — opt-in via `?fps=1` query param. Default production run
  // has no overlay and no perf console spam. When enabled, shows 1s avg,
  // 1% low, max frame time plus a per-phase perf breakdown.
  const wantFps = new URLSearchParams(window.location.search).get('fps') === '1';
  let fpsEl: HTMLDivElement | null = null;
  if (wantFps) {
    fpsEl = document.createElement('div');
    fpsEl.style.cssText =
      'position:fixed;top:6px;left:6px;z-index:9999;' +
      'font:12px/1.3 ui-monospace,monospace;color:#2a2a2a;' +
      'background:rgba(242,236,216,.85);padding:4px 8px;border-radius:4px;' +
      'pointer-events:none;white-space:pre';
    document.body.appendChild(fpsEl);
  }

  const samples: number[] = [];
  let acc = 0;
  let frames = 0;
  let worstMs = 0;

  let lastT = nowSec();
  function frame(): void {
    const t = nowSec();
    let dt = t - lastT;
    if (dt > 0.25) dt = 0.25;
    lastT = t;

    if (wantFps) {
      const fms = dt * 1000;
      samples.push(fms);
      if (samples.length > 240) samples.shift();
      if (fms > worstMs) worstMs = fms;
      acc += dt;
      frames++;
      if (acc >= 1.0) {
        const avgFps = frames / acc;
        const sorted = [...samples].sort((a, b) => a - b);
        const p99 = sorted[Math.floor(sorted.length * 0.99)] || 0;
        const lowFps = 1000 / p99;
        const line =
          `fps ${avgFps.toFixed(1)}  1%low ${lowFps.toFixed(1)}  ` +
          `worst ${worstMs.toFixed(1)}ms`;
        const snap = perf.snapshotAndReset();
        if (fpsEl) fpsEl.textContent = line + '\n' + snap.line;
        // eslint-disable-next-line no-console
        console.log('[fps]', line);
        // eslint-disable-next-line no-console
        console.log('[perf]', snap.line);
        acc = 0;
        frames = 0;
        worstMs = 0;
      }
    }

    mgr.tick(dt);
    perf.endFrame();
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
}

main();

import { Input } from './input/input';
import { Renderer } from './render/renderer';
import { makeMainMenuScene } from './scenes/main_menu_scene';
import { SceneManager } from './scenes/scene_manager';

function main(): void {
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

  let lastT = nowSec();
  function frame(): void {
    const t = nowSec();
    let dt = t - lastT;
    if (dt > 0.25) dt = 0.25;
    lastT = t;
    mgr.tick(dt);
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
}

main();

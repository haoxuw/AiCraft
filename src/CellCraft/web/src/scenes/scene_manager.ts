import { Input } from '../input/input';
import { Renderer } from '../render/renderer';
import { Scene, SceneCtx, SceneFactory } from './scene';

// Owns the active scene and sequences transitions through SceneFader.
// Only the manager calls enter/exit; scenes request transitions via
// ctx.requestGoto() and the manager serializes them.

export class SceneManager {
  private current: Scene | null = null;
  private transitioning = false;
  private pending: {
    factory: SceneFactory;
    fade: boolean;
    fadeOutMs: number;
    fadeInMs: number;
  } | null = null;

  constructor(
    private renderer: Renderer,
    private input: Input,
    private nowSec: () => number
  ) {}

  private makeCtx(): SceneCtx {
    return {
      renderer: this.renderer,
      input: this.input,
      now: this.nowSec(),
      requestGoto: (factory, opts) => {
        this.pending = {
          factory,
          fade: opts?.fade ?? true,
          fadeOutMs: opts?.fadeOutMs ?? 450,
          fadeInMs: opts?.fadeInMs ?? 550
        };
      }
    };
  }

  // Synchronous boot: no fade, just enter the first scene.
  boot(factory: SceneFactory): void {
    const ctx = this.makeCtx();
    this.current = factory();
    this.current.enter(ctx);
  }

  getCurrent(): Scene | null {
    return this.current;
  }

  isTransitioning(): boolean {
    return this.transitioning;
  }

  // Per-frame tick: runs update + flushes any pending transition.
  tick(dt: number): void {
    const ctx = this.makeCtx();
    if (this.current) this.current.update(dt, ctx);

    if (this.pending && !this.transitioning) {
      const p = this.pending;
      this.pending = null;
      this.transitioning = true;
      void this.runTransition(p.factory, p.fade, p.fadeOutMs, p.fadeInMs);
    }
  }

  private async runTransition(
    factory: SceneFactory,
    fade: boolean,
    fadeOutMs: number,
    fadeInMs: number
  ): Promise<void> {
    const fader = this.renderer.fader;
    if (fade) await fader.fadeOut(fadeOutMs, this.nowSec());

    // Exit old.
    const ctxOut = this.makeCtx();
    if (this.current) this.current.exit(ctxOut);

    // Enter new.
    this.current = factory();
    const ctxIn = this.makeCtx();
    this.current.enter(ctxIn);

    if (fade) await fader.fadeIn(fadeInMs, this.nowSec());
    this.transitioning = false;
  }

  onKey(e: KeyboardEvent): void {
    if (this.transitioning) return;
    const ctx = this.makeCtx();
    this.current?.onKey?.(e, ctx);
  }
  onPointer(e: PointerEvent): void {
    if (this.transitioning) return;
    const ctx = this.makeCtx();
    this.current?.onPointer?.(e, ctx);
  }
}

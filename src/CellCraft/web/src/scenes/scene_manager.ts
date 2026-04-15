import { Input } from '../input/input';
import { Renderer } from '../render/renderer';
import { Scene, SceneCtx, SceneFactory } from './scene';

// Owns the active scene and sequences scene swaps. Swaps are INSTANT —
// scenes do their own ~180ms slide+fade on the UI group in enter()/update().
// The old fullscreen cream SceneFader is retained as a class (transitions.ts)
// but no longer used here; it felt jarring between chalkboard scenes.

export class SceneManager {
  private current: Scene | null = null;
  private pending: { factory: SceneFactory } | null = null;

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
      requestGoto: (factory, _opts) => {
        this.pending = { factory };
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
    return false;
  }

  // Per-frame tick: runs update + flushes any pending transition.
  tick(dt: number): void {
    const ctx = this.makeCtx();
    if (this.current) this.current.update(dt, ctx);

    if (this.pending) {
      const p = this.pending;
      this.pending = null;
      // Instant swap: exit old, enter new. Per-scene enter animation
      // handles the visual transition.
      const ctxOut = this.makeCtx();
      if (this.current) this.current.exit(ctxOut);
      this.current = p.factory();
      const ctxIn = this.makeCtx();
      this.current.enter(ctxIn);
    }
  }

  onKey(e: KeyboardEvent): void {
    const ctx = this.makeCtx();
    this.current?.onKey?.(e, ctx);
  }
  onPointer(e: PointerEvent): void {
    const ctx = this.makeCtx();
    this.current?.onPointer?.(e, ctx);
  }
}

// Keyboard + pointer → player intent (a 2D velocity vector, screen-space
// independent). Also tracks click-to-move target and restart trigger.

export interface Intent {
  vel: [number, number]; // normalized; magnitude 0..1
  clickTarget: [number, number] | null; // world coords, set when fresh click lands
  restart: boolean;
}

export interface InputOpts {
  // screen → world projection (we get it from the renderer each frame).
  screenToWorld: (clientX: number, clientY: number) => [number, number];
}

export class Input {
  private keys = new Set<string>();
  private lastClick: [number, number] | null = null;
  private restartPulse = false;
  private opts: InputOpts;

  constructor(opts: InputOpts) {
    this.opts = opts;
    window.addEventListener('keydown', this.onKeyDown);
    window.addEventListener('keyup', this.onKeyUp);
    window.addEventListener('pointerdown', this.onPointerDown);
  }

  dispose(): void {
    window.removeEventListener('keydown', this.onKeyDown);
    window.removeEventListener('keyup', this.onKeyUp);
    window.removeEventListener('pointerdown', this.onPointerDown);
  }

  private onKeyDown = (e: KeyboardEvent): void => {
    this.keys.add(e.key.toLowerCase());
    if (e.key.toLowerCase() === 'r') this.restartPulse = true;
  };
  private onKeyUp = (e: KeyboardEvent): void => {
    this.keys.delete(e.key.toLowerCase());
  };
  private onPointerDown = (e: PointerEvent): void => {
    // Only left/primary clicks issue a move order.
    if (e.button !== 0) return;
    this.lastClick = this.opts.screenToWorld(e.clientX, e.clientY);
  };

  sample(): Intent {
    let dx = 0;
    let dy = 0;
    if (this.keys.has('w') || this.keys.has('arrowup')) dy += 1;
    if (this.keys.has('s') || this.keys.has('arrowdown')) dy -= 1;
    if (this.keys.has('a') || this.keys.has('arrowleft')) dx -= 1;
    if (this.keys.has('d') || this.keys.has('arrowright')) dx += 1;
    const mag = Math.hypot(dx, dy);
    const vel: [number, number] = mag > 0 ? [dx / mag, dy / mag] : [0, 0];

    const click = this.lastClick;
    this.lastClick = null;
    const restart = this.restartPulse;
    this.restartPulse = false;
    return { vel, clickTarget: click, restart };
  }
}

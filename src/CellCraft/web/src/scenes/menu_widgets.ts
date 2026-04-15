import * as THREE from 'three';
import { makeGlassPanel, makeText, UI_PALETTE } from '../render/ui';

// Tiny reusable menu button: a glass panel with a centered text label.
// Hover/focus state is driven externally (scene tracks selection index).

export interface MenuButtonOpts {
  width?: number;
  height?: number;
  textSize?: number;
  disabled?: boolean;
}

export interface MenuButtonHandle {
  group: THREE.Group;
  setFocused(on: boolean): void;
  setPressed(on: boolean): void;
  // World-space half-size, for hit-testing. Matches width/height.
  halfW: number;
  halfH: number;
  label: string;
  disabled: boolean;
}

export function makeMenuButton(label: string, opts: MenuButtonOpts = {}): MenuButtonHandle {
  const w = opts.width ?? 300;
  const h = opts.height ?? 56;
  const size = opts.textSize ?? 22;
  const disabled = !!opts.disabled;

  const group = new THREE.Group();

  // Base panel (always present).
  const base = makeGlassPanel(w, h, {
    radius: h * 0.35,
    tint: disabled ? 0x22272d : 0x1a2026,
    alpha: disabled ? 0.6 : 0.88,
    borderColor: disabled ? UI_PALETTE.chalkSoft : UI_PALETTE.neonCyan
  });
  group.add(base);

  // Focus halo (hidden by default): brighter panel overlay.
  const halo = makeGlassPanel(w + 10, h + 10, {
    radius: (h + 10) * 0.35,
    tint: UI_PALETTE.neonCyan,
    alpha: 0.18,
    borderColor: UI_PALETTE.neonCyan
  });
  halo.position.z = -0.01;
  halo.visible = false;
  group.add(halo);

  const labelColor = disabled ? UI_PALETTE.chalkSoft : UI_PALETTE.paper;
  const text = makeText(label, {
    size,
    color: labelColor,
    weight: 'bold',
    anchorX: 'center',
    anchorY: 'middle'
  });
  text.position.z = 0.05;
  group.add(text);

  return {
    group,
    halfW: w / 2,
    halfH: h / 2,
    label,
    disabled,
    setFocused(on) {
      halo.visible = on && !disabled;
    },
    setPressed(on) {
      group.scale.setScalar(on ? 0.97 : 1.0);
    }
  };
}

// Hit-test helper: given a button's world (HUD-space) position and its
// half-extents, check if a HUD-space point is inside.
export function buttonHit(btn: MenuButtonHandle, hudX: number, hudY: number): boolean {
  const p = btn.group.position;
  return (
    hudX >= p.x - btn.halfW &&
    hudX <= p.x + btn.halfW &&
    hudY >= p.y - btn.halfH &&
    hudY <= p.y + btn.halfH
  );
}

// Convert a pointer's clientX/clientY into HUD-space coordinates (pixels
// from center, +y up). The HUD camera is sized exactly 1 unit per pixel.
export function pointerToHud(canvas: HTMLCanvasElement, clientX: number, clientY: number): [number, number] {
  const rect = canvas.getBoundingClientRect();
  const x = clientX - rect.left - rect.width / 2;
  const y = -(clientY - rect.top - rect.height / 2);
  return [x, y];
}

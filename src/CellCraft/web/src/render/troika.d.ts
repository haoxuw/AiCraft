// Minimal ambient declaration for troika-three-text. Troika ships no
// .d.ts in its UMD bundle; we only use the `Text` class and a handful
// of writable properties, so a loose declaration is sufficient.
declare module 'troika-three-text' {
  import * as THREE from 'three';
  export class Text extends THREE.Object3D {
    text: string;
    font?: string;
    fontSize: number;
    color: number | string;
    anchorX: 'left' | 'center' | 'right' | number | string;
    anchorY: 'top' | 'top-baseline' | 'middle' | 'bottom-baseline' | 'bottom' | number | string;
    outlineWidth: number | string;
    outlineColor: number | string;
    outlineOpacity: number;
    outlineBlur: number | string;
    maxWidth: number;
    textAlign: 'left' | 'right' | 'center' | 'justify';
    sync(cb?: () => void): void;
    dispose(): void;
  }
  export function preloadFont(
    opts: { font?: string; characters?: string | string[] },
    cb: () => void
  ): void;
}

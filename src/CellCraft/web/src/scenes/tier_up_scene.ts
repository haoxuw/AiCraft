import * as THREE from 'three';
import { makeText, UI_PALETTE } from '../render/ui';

// Placeholder tier-up overlay — minimal text flash. The real cinematic
// (bloomed title + pill animation + ring fill) lands in the next commit.

export interface TierUpOverlay {
  group: THREE.Group;
  update(nowSec: number): void;
  finished(nowSec: number): boolean;
  skip(nowSec: number): void;
}

const DURATION = 1.5;

export function makeTierUpOverlay(newTier: number, startedAt: number): TierUpOverlay {
  const group = new THREE.Group();
  let endAt = startedAt + DURATION;
  const title = makeText(`TIER ${newTier}`, {
    size: 72,
    color: UI_PALETTE.paper,
    glow: UI_PALETTE.neonAmber,
    weight: 'bold'
  });
  group.add(title);
  return {
    group,
    update(now) {
      const t = Math.min(1, (now - startedAt) / DURATION);
      title.scale.setScalar(1 + Math.sin(t * Math.PI) * 0.1);
    },
    finished(now) { return now >= endAt; },
    skip(now) { endAt = now; }
  };
}

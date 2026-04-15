import * as THREE from 'three';
import { makeGlassPanel, makePillBadge, makeRingProgress, makeText, UI_PALETTE } from '../render/ui';

// TierUpOverlay is not a full Scene — it's a HUD-scene attachment that
// the MatchScene adds on a TIER_UP event and removes when .finished()
// returns true. Keeps the match sim running in the background (slightly
// dimmed via a big translucent panel).

export interface TierUpOverlay {
  group: THREE.Group;
  update(nowSec: number): void;
  finished(nowSec: number): boolean;
  skip(nowSec: number): void;
}

const DURATION = 2.5;

export function makeTierUpOverlay(newTier: number, startedAt: number): TierUpOverlay {
  const group = new THREE.Group();
  let endAt = startedAt + DURATION;

  // Fullscreen dim. The HUD camera is sized to window dimensions each
  // frame; we use a very large panel to safely cover any screen.
  const dim = makeGlassPanel(4000, 4000, {
    radius: 0,
    tint: 0x000000,
    alpha: 0.45,
    borderColor: 0x000000
  });
  // Render behind the title.
  dim.position.z = -0.5;
  group.add(dim);

  const title = makeText('TIER UP', {
    size: 120,
    color: UI_PALETTE.paper,
    glow: UI_PALETTE.neonAmber,
    weight: 'bold'
  });
  title.position.set(0, 40, 0);
  group.add(title);

  const pill = makePillBadge(`TIER ${newTier}`, { color: UI_PALETTE.neonAmber });
  pill.position.set(0, -60, 0);
  pill.scale.setScalar(0.0);
  group.add(pill);

  const ring = makeRingProgress(60, 8, { color: UI_PALETTE.neonCyan });
  ring.group.position.set(0, -60, -0.1);
  group.add(ring.group);

  return {
    group,
    update(now) {
      const t = Math.min(1, Math.max(0, (now - startedAt) / DURATION));
      // Title breathes.
      const breathe = 1.0 + Math.sin(t * Math.PI * 2) * 0.04;
      title.scale.setScalar(breathe);

      // Pill scales in over first 0.4, bounces slightly.
      const p = Math.min(1, t / 0.4);
      const elastic = p < 1 ? 1 - Math.pow(1 - p, 3) : 1 + Math.sin((t - 0.4) * 10) * 0.02;
      pill.scale.setScalar(elastic);

      // Ring fills 0→1 over the duration.
      ring.setValue(t);
    },
    finished(now) {
      return now >= endAt;
    },
    skip(now) {
      endAt = now;
    }
  };
}

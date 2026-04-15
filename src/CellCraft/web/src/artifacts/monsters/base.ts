// Base monster definitions. The three starters (SHARD/MOSS/DARTER) are
// flagged starter=true and appear in StarterSelectScene via
// allStarters(). The rest are AI-only creatures used by world spawners
// for outer-world variety.

import { PartKind } from '../../sim/part';
import { registry } from '../registry';

registry.registerMonster({
  id: 'base:shard',
  name: 'SHARD',
  blurb: 'carnivore — spike + mouth',
  color: [0.86, 0.4, 0.42],
  seed: 2001,
  starter: true,
  behavior: 'base:hunt',
  parts: [
    { kind: PartKind.MOUTH, anchor: [34, 0], scale: 1.0 },
    { kind: PartKind.SPIKE, anchor: [38, 8], scale: 1.2 },
    { kind: PartKind.SPIKE, anchor: [38, -8], scale: 1.2 }
  ]
});

registry.registerMonster({
  id: 'base:moss',
  name: 'MOSS',
  blurb: 'herbivore — regen + armor',
  color: [0.55, 0.75, 0.55],
  seed: 2002,
  starter: true,
  behavior: 'base:wander',
  parts: [
    { kind: PartKind.MOUTH, anchor: [34, 0], scale: 1.0 },
    { kind: PartKind.REGEN, anchor: [-20, 12], scale: 1.0 },
    { kind: PartKind.ARMOR, anchor: [-24, 0], scale: 1.0 }
  ]
});

registry.registerMonster({
  id: 'base:darter',
  name: 'DARTER',
  blurb: 'scout — eyes + flagella',
  color: [0.7, 0.5, 0.85],
  seed: 2004,
  starter: true,
  behavior: 'base:mixed',
  parts: [
    { kind: PartKind.MOUTH, anchor: [34, 0], scale: 1.0 },
    { kind: PartKind.EYES, anchor: [28, 14], scale: 1.0 },
    { kind: PartKind.EYES, anchor: [28, -14], scale: 1.0 },
    { kind: PartKind.FLAGELLA, anchor: [-28, 0], scale: 1.2 }
  ]
});

// AI-only variety: used by the outer world + match-scene ring spawns.
registry.registerMonster({
  id: 'base:pricklet',
  name: 'PRICKLET',
  color: [0.86, 0.4, 0.42],
  seed: 3001,
  behavior: 'base:hunt',
  parts: [
    { kind: PartKind.MOUTH, anchor: [34, 0], scale: 1.0 },
    { kind: PartKind.SPIKE, anchor: [38, 8], scale: 1.2 },
    { kind: PartKind.SPIKE, anchor: [38, -8], scale: 1.2 }
  ]
});

registry.registerMonster({
  id: 'base:shelly',
  name: 'SHELLY',
  color: [0.55, 0.65, 0.9],
  seed: 3002,
  behavior: 'base:wander',
  parts: [
    { kind: PartKind.MOUTH, anchor: [34, 0], scale: 1.0 },
    { kind: PartKind.REGEN, anchor: [-20, 12], scale: 1.0 },
    { kind: PartKind.ARMOR, anchor: [-24, 0], scale: 1.0 }
  ]
});

registry.registerMonster({
  id: 'base:fang',
  name: 'FANG',
  color: [0.92, 0.78, 0.42],
  seed: 3003,
  behavior: 'base:hunt',
  parts: [
    { kind: PartKind.MOUTH, anchor: [34, 0], scale: 1.0 },
    { kind: PartKind.TEETH, anchor: [36, 6], scale: 1.0 },
    { kind: PartKind.REGEN, anchor: [-20, 0], scale: 0.8 }
  ]
});

registry.registerMonster({
  id: 'base:zip',
  name: 'ZIP',
  color: [0.7, 0.5, 0.85],
  seed: 3004,
  behavior: 'base:mixed',
  parts: [
    { kind: PartKind.MOUTH, anchor: [34, 0], scale: 1.0 },
    { kind: PartKind.EYES, anchor: [28, 14], scale: 1.0 },
    { kind: PartKind.FLAGELLA, anchor: [-28, 0], scale: 1.2 }
  ]
});

// A couple of extras for outer-world variety.
registry.registerMonster({
  id: 'base:thorn',
  name: 'THORN',
  color: [0.8, 0.55, 0.3],
  seed: 3101,
  behavior: 'base:mixed',
  parts: [
    { kind: PartKind.MOUTH, anchor: [34, 0], scale: 1.0 },
    { kind: PartKind.HORN, anchor: [40, 0], scale: 1.2 },
    { kind: PartKind.CILIA, anchor: [-22, 10], scale: 1.0 }
  ]
});

registry.registerMonster({
  id: 'base:drip',
  name: 'DRIP',
  color: [0.45, 0.7, 0.55],
  seed: 3102,
  behavior: 'base:wander',
  parts: [
    { kind: PartKind.MOUTH, anchor: [34, 0], scale: 1.0 },
    { kind: PartKind.POISON, anchor: [0, 0], scale: 1.0 },
    { kind: PartKind.FLAGELLA, anchor: [-28, 0], scale: 1.0 }
  ]
});

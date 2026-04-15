// Base part definitions. These mirror the native PartKind enum and the
// tuning constants in sim/tuning.ts — registration is the source of
// truth that the UI + tooling introspect; numeric stats still live in
// tuning.ts so they can be tweaked without touching artifact code.

import { PartKind } from '../../sim/part';
import { registry } from '../registry';

registry.registerPart({
  id: 'base:mouth',
  kind: PartKind.MOUTH,
  displayName: 'Mouth',
  blurb: 'Expands pickup radius. Required to eat.'
});

registry.registerPart({
  id: 'base:flagella',
  kind: PartKind.FLAGELLA,
  displayName: 'Flagella',
  blurb: 'Increases movement speed.'
});

registry.registerPart({
  id: 'base:eyes',
  kind: PartKind.EYES,
  displayName: 'Eyes',
  blurb: 'Extends perception range.'
});

registry.registerPart({
  id: 'base:spike',
  kind: PartKind.SPIKE,
  displayName: 'Spike',
  blurb: 'Deals contact damage on collision.'
});

registry.registerPart({
  id: 'base:cilia',
  kind: PartKind.CILIA,
  displayName: 'Cilia',
  blurb: 'Improves turning agility.'
});

registry.registerPart({
  id: 'base:teeth',
  kind: PartKind.TEETH,
  displayName: 'Teeth',
  blurb: 'High-damage bite on contact.'
});

registry.registerPart({
  id: 'base:armor',
  kind: PartKind.ARMOR,
  displayName: 'Armor',
  blurb: 'Reduces damage taken; adds local HP.'
});

registry.registerPart({
  id: 'base:regen',
  kind: PartKind.REGEN,
  displayName: 'Regen',
  blurb: 'Passively restores HP over time.'
});

registry.registerPart({
  id: 'base:horn',
  kind: PartKind.HORN,
  displayName: 'Horn',
  blurb: 'Heavy forward-facing ram damage.'
});

registry.registerPart({
  id: 'base:poison',
  kind: PartKind.POISON,
  displayName: 'Poison',
  blurb: 'Radiates a DoT aura around the body.'
});

registry.registerPart({
  id: 'base:venom_spike',
  kind: PartKind.VENOM_SPIKE,
  displayName: 'Venom Spike',
  blurb: 'On hit, applies a stacking venom DoT.'
});

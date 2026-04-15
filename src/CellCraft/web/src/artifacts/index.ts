// Artifact auto-loader. Vite's import.meta.glob pulls every .ts file
// under src/artifacts/**/*.ts at boot time (eager=true → synchronous
// side-effect imports). Each module imports `registry` and calls
// `registry.registerPart/Monster/Behavior()` at module scope.
//
// To add a mod, create `src/artifacts/<category>/<your_mod>.ts` and
// either register at module scope, or export a
// `register(reg: RegistryAPI)` function — loadAllArtifacts() calls it
// automatically if present. Id convention: "<namespace>:<name>", e.g.
// "mymod:bloodfang".

import { registry } from './registry';
import type { ModRegister } from './types';

let loaded = false;

export function loadAllArtifacts(): void {
  if (loaded) return;
  loaded = true;

  // Eager glob so every module's top-level register*() calls run now.
  // We also respect an optional exported `register(reg)` for mods that
  // prefer imperative registration.
  const mods = import.meta.glob('./**/*.ts', { eager: true }) as Record<
    string,
    { register?: ModRegister }
  >;
  for (const [path, mod] of Object.entries(mods)) {
    // Skip self + registry + types + tests.
    if (
      path.endsWith('/index.ts') ||
      path.endsWith('/registry.ts') ||
      path.endsWith('/types.ts') ||
      path.includes('/__tests__/')
    ) {
      continue;
    }
    if (typeof mod.register === 'function') {
      mod.register(registry);
    }
  }
}

export { registry, getPart, getMonster, getBehavior, allParts, allMonsters, allBehaviors, allStarters } from './registry';
export type { PartDef, MonsterDef, BehaviorDef, BehaviorCtx, MonsterPartSpec, ModRegister, RegistryAPI } from './types';

# CellCraft — Design Overview

> **NOTE:** This doc describes the LEGACY native C++ client (Python mod
> layer, four-action-type server, etc.). The current CellCraft
> implementation is the Three.js + TypeScript web client at
> `src/CellCraft/web/`, with a Node WebSocket server at
> `src/CellCraft/web/server/` and JS artifacts (parts / monsters /
> behaviors) in `web/src/artifacts/`. The design vision below (cells,
> parts, tiers, diets, moddable behaviors) still applies; the
> implementation details referring to C++ / Python / pybind are
> historical.

> **Spore's cell stage, multiplayer, every behavior moddable in Python.
> The gameplay is chalk on paper; the UI around it is a sleek modern app.**

## Elevator pitch

You are a **cell** on a shared cream-paper arena. You sculpt your body
(start from a circle, push/pull with a playdough brush, enforce
vertical-axis symmetry), then drag-and-drop **parts** — MOUTH, SPIKE,
TEETH, FLAGELLA, HORN, EYES, REGEN, ARMOR, CILIA, POISON, VENOM_SPIKE
— onto the body. The lab is a creature editor; the arena is where
your creature lives, eats, grows, and fights.

Every tick you must decide: do I chase food, stab a rival, flee the
larger shape looming in the background? Every match you accumulate
**biomass** and climb **5 tiers** (SPECK → NIBBLER → HUNTER → PREDATOR
→ APEX). Each tier unlocks more parts, scales your body up, and
changes what lives in the background layer — from giant cells, to
fish, to turtles. At APEX you have graduated out of the cell world.

Every behavior (AI `decide()`, part effect, tier table, food yield,
diet rule) is a Python artifact. Nothing except the physics + the four
server action types is hard-coded in C++.

## The five design rules (non-negotiable)

### Rule 1 — Damage requires damaging parts
A plain cell bumping another plain cell does **zero damage**. Damage
is only dealt when a SPIKE, TEETH, HORN, or VENOM_SPIKE part's
world-space anchor is within contact distance of the collision point.
This makes part placement a combat decision, not just aesthetics.

Poison (POISON part) is an area aura; it does not need contact.

### Rule 2 — Solid bodies
Cells never overlap or tunnel. After the soft-spring collision
response, any residual polygon-polygon penetration is resolved by SAT
push along the contact normal, split mass-proportional between the
two cells, and clamped inside the arena radius.

### Rule 3 — MOUTH is required to eat
No MOUTH part → no food pickup, ever. Eating is gated by mouth just
like damage is gated by damaging parts.

### Rule 4 — Diet is derived, not chosen
Carnivore/Herbivore/Omnivore is computed from the creature's other
parts. The player does not pick a diet; they build a body, and the
diet falls out. Yield multiplier applies on pickup:

- Matching diet + food type → ×1.5
- Opposing → ×0.4
- Omnivore → ×1.0 (both types)

### Rule 5 — Conservation of mass (inherited from CivCraft)
**Nothing is created from nothing.** Every plant/meat blub carries a
biomass value. Every kill transfers `DEATH_BIOMASS_FRAC × victim.mass`
to the killer; the rest drops as MEAT pellets. The server enforces
that no ActionProposal increases the board's total biomass.

## Part catalog

All parts are Python artifacts (`src/CellCraft/artifacts/parts/base/*.py`).
C++ knows only their enum IDs and queries the cached `PartEffect` struct.

| Part         | Unlock tier | Diet pull | Effect |
|--------------|-------------|-----------|--------|
| MOUTH        | 1 SPECK     | neutral   | Enables food pickup |
| FLAGELLA     | 1 SPECK     | neutral   | Speed multiplier |
| EYES         | 1 SPECK     | neutral   | Perception radius |
| SPIKE        | 2 NIBBLER   | carnivore | Contact damage at spike location |
| CILIA        | 2 NIBBLER   | herbivore | Turn rate |
| TEETH        | 3 HUNTER    | carnivore | Contact damage, short range |
| ARMOR        | 3 HUNTER    | herbivore | Local damage reduction at armor location |
| REGEN        | 3 HUNTER    | herbivore | HP regeneration per second |
| HORN         | 4 PREDATOR  | carnivore | Damage + ±15° forward-cone bonus |
| POISON       | 4 PREDATOR  | carnivore | Aura DoT around the cell |
| VENOM_SPIKE  | 5 APEX      | carnivore | SPIKE + venom stacks on target |

Part **contribution to diet score = ±scale** per part (MOUTH, FLAGELLA,
EYES contribute 0). Thresholds: `> +0.8` carnivore, `< -0.8` herbivore,
else omnivore.

## The five tiers

Tiers are gated by **lifetime biomass** (monotonic, not current). On
tier-up: body scales, stats refresh, TIER_UP event fires, sparkle burst
renders, next-tier parts unlock in the lab.

| Tier | Name      | Threshold | Body scale | Parts unlocked here              |
|------|-----------|-----------|------------|-----------------------------------|
| 1    | SPECK     | 0         | 1.0×       | MOUTH, FLAGELLA, EYES             |
| 2    | NIBBLER   | 40        | 1.25×      | + SPIKE, CILIA                    |
| 3    | HUNTER    | 120       | 1.5×       | + TEETH, ARMOR, REGEN             |
| 4    | PREDATOR  | 300       | 1.8×       | + HORN, POISON                    |
| 5    | APEX      | 700       | 2.2×       | + VENOM_SPIKE                     |

## The evolution economy *(design direction — not yet built)*

Biomass from food only feeds survival (HP, fullness). To **grow** (bigger
tier OR more parts) you spend **material** — a second resource gained
specifically by killing other cells.

| Action              | Biomass change      | Material change |
|---------------------|---------------------|-----------------|
| Eat plant/meat blub | `+yield × food.bm`  | 0 — survival only |
| Kill another cell   | `+DEATH_BIOMASS_FRAC × victim.mass` | `+victim.material_banked` |
| Respawn / starve    | bm→0                | material persists in bank |

At any time (between matches, or via a "mutate" button in the lab)
the player spends material two ways:

- **Grow bigger.** Cross the next tier threshold early by buying it
  with material. Bigger = tankier + unlocks higher-tier parts, but
  bigger cells also cost more material to maintain per second (Rule 5
  — mass conservation — larger bodies draw a passive tax from the
  material bank, simulating metabolism).
- **Grow more parts / shapes.** Unlock an extra part slot, or an
  extra "sculpt surface" to add more complex silhouettes. This is how
  specialization emerges: an r-strategist stays small with many parts
  clustered tightly; a K-strategist spends the same material on size.

This mirrors real evolutionary pressure: a species with more
phenotypes can exploit more niches; a species with larger bodies wins
fights but burns more energy. Neither strategy dominates — the
roguelike meta rewards both.

**Status:** material bank is specified here; sim/tuning changes to
track it + a lab UI to spend it are roadmap items (commit 7+).

## Food

Two food types, rendered as distinct chalk blubs:

- **PLANT** (green 3-leaf cluster with brown stem): biomass 4–9,
  plentiful (60% of scatter).
- **MEAT** (pink lobed blob with darker marbling): biomass 8–15,
  rarer (40% of scatter). Also drops from kills.

Visual radius is decoupled from biomass so food reads at a glance
regardless of value.

## Parallax background — the food chain is visible

Behind the play surface, large desaturated silhouettes drift slowly.
Their shape depends on **player tier**:

| Player tier | Background silhouettes | Role |
|-------------|------------------------|------|
| 1 SPECK     | 4× cells               | Big cells loom overhead |
| 2 NIBBLER   | 8× cells               | Bigger cells now |
| 3 HUNTER    | 16× cells              | Giant cells, edge of animal-scale |
| 4 PREDATOR  | 32× cells              | Titanic cells — almost animals |
| 5 APEX      | Fish + Turtle + Jelly  | **Animal world.** You graduated. |

Silhouettes are pure visual — no collision, no logic. Warm tan tint,
thin single-pass outline, slow drift + wrap. Counts: 7 at T1–4, 3 at T5
(sparse so shapes read).

## Creature rendering direction *(roadmap — not yet built)*

**Current:** cells render as chalk-outline polygons. Reads well on the
cream paper but makes them feel "schematic" instead of alive.

**Target:** add a fragment shader that fills the polygon with an
organic-looking material. Inspiration: watercolor wash with uneven
saturation, subtle internal veins/texture, a darker "membrane" ring
near the silhouette edge. The cell should look like a **living
translucent thing**, not a line drawing. Parts remain chalk strokes
over the fill.

This also makes the background parallax creatures read better: a fish
filled with a gradient body + fin shading is instantly a fish, whereas
a pure-outline fish at 32× scale can look like abstract scribble
(documented issue in the tier-5 screenshots).

Shader sketch:
- Input: polygon fill (premultiplied), cell's **diet color** (cyan for
  herbivore, red for carnivore, purple for omnivore) + per-creature
  noise seed.
- Fragment: distance-to-nearest-edge (approximated from a SDF of the
  polygon baked per frame, or from barycentric interpolation of
  per-vertex inset distance) drives a radial gradient: darker membrane
  near the edge, lighter cytoplasm in the middle.
- Layer 2: low-freq value-noise on diet color, gives the "cell looks
  organic" wash.
- Layer 3: thin darker outline along the polygon edge for definition.
- Background silhouettes use the same shader at lower detail + heavy
  desaturation toward the cream paper tone.

This ships as commit 7 (planned).

## UI — chalk for the art, modern for the app

**The arena is chalk on cream paper.** Cells, parts, food, and
background silhouettes all draw in the chalk ribbon pipeline
(`src/CellCraft/shaders/chalk.frag`).

**Everything around the arena is modern.** Glass panels, charcoal
scrim backgrounds, cyan and amber accents, Inter sans-serif UI text,
Audiowide arcade-neon display titles with SDF-driven glow halos.

| Surface          | Palette                   | Typography      |
|------------------|---------------------------|-----------------|
| Arena (in-game)  | Cream paper + saturated chalk pigment | Chalk (creature names only) |
| Main menu        | Charcoal scrim, cyan ambient motes    | Chalk CELLCRAFT logotype + Inter |
| Creature lab     | Dark chrome glass panels framing cream bezel | Inter labels, Inter-Bold stats |
| Match HUD        | Glass bars floating over arena | Inter numerics, tracked labels |
| End screen       | Charcoal scrim + metric rings | Audiowide hero ("YOU DIED", "APEX REACHED") |

Primary accent `#22D3EE` (cyan), progression accent `#F59E0B` (amber),
danger `#EF4444` (red). Pink is reserved for meat; never UI chrome.

Modern UI primitives live in `src/CellCraft/client/ui_modern.{h,cpp}`
with tokens (colors, spacing, radii, type sizes) and primitives
(scrim, rounded rect, glass panel, soft shadow, inner glow, buttons,
stat bar, pill badge, ring progress, divider). The SDF font stack is
`src/CellCraft/client/sdf_font.{h,cpp}` + `shaders/text_modern.*` with
stb_truetype and bundled Inter + Audiowide TTFs.

## Action types — same four as CivCraft

The server validates exactly four `ActionProposal` types. Every action
a cell can take compiles to one of these.

| # | Type | CellCraft meaning |
|---|------|-------------------|
| 0 | `TYPE_MOVE`     | Set a cell's 2D velocity (Python `decide()` emits this) |
| 1 | `TYPE_RELOCATE` | Pick up a food blub; transfer biomass on kill |
| 2 | `TYPE_CONVERT`  | Apply damage (HP→nothing), regenerate HP (material→HP), tier up (biomass→size), apply venom (HP→status) |
| 3 | `TYPE_INTERACT` | Reserved for future interactive world objects |

No new action types. Anything a modder builds must compile to these.

## Architecture — mirrors CivCraft

Three process types. Always TCP. Identical for singleplayer (localhost)
and multiplayer (remote server).

| Process           | Binary             | Responsibility |
|-------------------|--------------------|----|
| Server            | `cellcraft-server` | Headless. Owns arena, runs 2D physics + collision + damage, validates actions. **No Python, no OpenGL.** |
| Player client     | `cellcraft`        | Chalk rendering, modern UI chrome, mouse sculpt + drag input, lab editor. **OpenGL, no Python.** |
| Agent client      | `cellcraft-agent`  | One process per cell. Runs Python `decide()`. **Python + pybind11, no OpenGL.** |

Dependency rules match CivCraft:
- `platform/` — game-agnostic engine (shared with CivCraft)
- `CellCraft/` — must never `#include "CivCraft/..."` and vice versa
- Shared code gets promoted to `platform/` first

**Status:** currently shipping as a single `cellcraft` binary with a
C++ fallback AI (priority stack: flee > feed > hunt > wander). Server
split + Python agent path is M1 work, mirroring CivCraft's M1.

## Source layout

```
src/CellCraft/
  sim/             RadialCell, Part, Monster, World, Sim — headless sim
    tuning.h         all tunable constants (thresholds, multipliers)
    part_stats.h     computePartEffects — diet, damage, stat multipliers
    sim.cpp          tick loop, collisions, pickup, kill, tier-up
  artifacts/
    monsters/base/   prebuilt.h (Stinger/Blob/Dart/Tusker) + starters.h
    parts/base/      part definitions (Python target; currently C++)
  client/
    app.{h,cpp}      state machine + scene rendering
    creature_lab.*   sculpt editor + drawer panels
    chalk_renderer.* ribbon primitive + cell/part draw
    background_layer.* parallax silhouettes (cells / fish / turtle / jelly)
    ui_modern.*      modern UI tokens + primitives
    ui_button/text/particles/anim/… (reused where modern not yet wired)
    sdf_font.*       stb_truetype atlas + per-role rendering
    post_fx.*        HDR + bloom + vignette + low-HP pulse
  shaders/
    chalk.{vert,frag}       chalk pigment ribbon on cream
    board.frag              cream paper with drifting noise
    text_modern.{vert,frag} SDF text with 13-tap isotropic glow
    bloom_*.frag            post-fx chain
  vendor/          stb_truetype.h
  fonts/           Inter-{Regular,Bold}.ttf, Audiowide-Regular.ttf
  docs/            you are here
```

## Implementation status

### Shipped

- Single `cellcraft` binary. Main menu → starter → lab → celebrate →
  playing → end screen, all with modern UI chrome.
- **Gameplay sim:** RadialCell sculpt body, 11 part types, collision
  with hard overlap resolution, part-gated damage (+ HORN cone, ARMOR
  local DR, VENOM stacks), diet-gated yield, MOUTH-gated pickup, 5
  growth tiers with lifetime-biomass thresholds and body scaling,
  TIER_UP events.
- **Food:** MEAT + PLANT types, distinct blub visuals, corpse drops.
- **Parallax background:** tier-scaled silhouettes, T5 is fish/turtle/jelly.
- **UI:** modern design system (charcoal + cyan + amber), SDF TTF
  fonts (Inter + Audiowide) with neon glow shader, glass panels,
  rounded-rect primitives, stat bars, ring progress, pill badges,
  scene fade transitions.
- **QA & tooling:** CLI flags `--autotest --seed N`, `--menu-screenshot`,
  `--lab-screenshot`, `--starter-screenshot`, `--celebrate-screenshot`,
  `--play-screenshot`, `--end-screenshot`, `--autotest-tier N`,
  `--ui-kitchen-sink`. Headless log at `/tmp/cellcraft_game.log`.

### Next (roadmap)

1. **Cell fill shader** — make cells look like living organisms, not
   line drawings. Watercolor-wash material with diet-tinted gradient,
   darker membrane ring, subtle internal noise. Applies to in-arena
   cells AND background silhouettes.
2. **Material economy** — second resource gained on kills only; spent
   on size-up (early tier purchase) or slot-up (extra part capacity).
   Lab gets a "MUTATE" panel for spending material.
3. **Server split** — `cellcraft-server` + `cellcraft-agent` processes,
   TCP protocol, authoritative sim. Matches CivCraft's M1.
4. **Python agent** — `decide()` in artifacts/behaviors; hot-reload.
5. **Multiplayer** — 2–8 player arenas with tier-mixed matchmaking
   (players see bigger tiers' actual creatures in their background
   layer, not synthetic silhouettes).
6. **Roguelike meta** — persistent mod unlocks (new parts, new stroke
   types, new Python helpers) between matches.

## Relationship to CivCraft

CellCraft and CivCraft are **separate games sharing the C++ engine in
`src/platform/`**. They do not share artifacts, content, or game rules.
Any code either game wants from the other must first be promoted into
`platform/`. See the root `CLAUDE.md` § Two games, same engine.

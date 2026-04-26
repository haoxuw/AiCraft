---
name: refine-model-and-animation
description: Iterative visual QA workflow for item AND character models. Use when the user wants to inspect, fix, or compare how an item or character looks in-game (FPS hand, TPS character, RPG/RTS overhead, ground, inventory icon, character front/side/back/top) or wants to evaluate animation quality (swing, equip, idle, walk). Covers scale, orientation, equip transform, slash axis, silhouette, palette, and the full code→screenshot→analyze→fix loop.
---

# Refine Model & Animation

A structured workflow for achieving correct visual quality for items in every in-game context.

---

## When to use this skill

- Item looks wrong in hand vs on the ground
- Swing / slash animation axis is off (purely vertical when it should be diagonal)
- Scale mismatch between FPS, TPS, RPG, and ground views
- New item model needs visual calibration before shipping

---

## Key concepts

### Character model hand annotations (critical for held item placement)

Every humanoid model must define `hand_r`, `hand_l`, `pivot_r`, `pivot_l` so held items grip at the correct height. Without these, items default to the `BoxModel` fallback values which may misplace the item.

Derive from the arm part in the model file:
- `hand_r` / `hand_l` → bottom of arm (grip position), derived from arm `offset.y - halfHeight`
- `pivot_r` / `pivot_l` → shoulder joint, derived from arm `offset.y + halfHeight`

Add to each character `.py` file alongside the `parts` list. The C++ parsing is in `shared/model_loader.h → dictToBoxModel()`.

### Minimum part depth for ground visibility

Ground items are scaled to `targetH / modelHeight` (default 0.35 / 1.4 ≈ 0.25× for a sword). Any model dimension below ~0.10 will become sub-pixel at that scale.

**Rule for thin items (swords, spears, axes)**: the thinnest axis (usually Z/depth) must be ≥ 0.10 so the item is readable on the ground. Silhouette matters more than detail at small scale — reduce part count and make each part chunkier.

### Scale reference values
| Context | Target | Formula |
|---------|--------|---------|
| Ground (floating) | 0.35 blocks tall | `worldScale = 0.35 / modelHeight` |
| TPS/RPG/RTS hand | ~35% of character height | `equip.scale ≈ 0.70 / modelHeight` |
| FPS screen | ~40–45% of FOV height | `fpScale = equip.scale × 0.72` |
| Inventory icon | camera at `2.8 × modelHeight` | isometric 3/4 view |

For a 2.0-block player, the sword (height=1.4) should be:
- Ground: 0.35 blocks → `scale = 0.25`
- Hand: 0.70 blocks → `equip.scale ≈ 0.50`
- FPS: `0.50 × 0.72 × 1.4 ≈ 0.50 blocks` on screen (good for bottom-right corner)

### Equip rotation
- Defined in `artifacts/models/<id>.py` → `"equip": { "rotation": [pitch, yaw, roll] }`
- `[-20, 30, -10]` = good default for swords: blade angled out 30° so the face is visible from both FPS and TPS views. Yaw 0 makes the blade edge-on when viewed from behind in TPS.

### FPS slash animation axis
The rotation axis determines the arc of the slash. For a diagonal slash that sweeps the blade's edge through the air:
- **Wrong**: `vec3(1, 0, 0)` — pure pitch (up/down only, looks rigid)
- **Correct**: `normalize(vec3(0.8, 0.0, 0.5))` — pitch + forward roll, diagonal arc

```cpp
glm::vec3 slashAxis = glm::normalize(glm::vec3(0.8f, 0.0f, 0.5f));
fpRoot = glm::rotate(fpRoot, glm::radians(swing), slashAxis);
```

Amplitude: 65° feels natural; 40° is too subtle, 90° is too dramatic.

---

## The iteration loop

### Step 1 — Take baseline screenshots

**Items** — FPS/TPS/RPG/RTS + ground:
```bash
make item_views ITEM=base:sword
# or: ./build/solarium-ui-vk --skip-menu --debug-scenario item_views --debug-item base:sword
```
Produces `/tmp/debug_N_{fps,tps,rpg,rts,ground}.ppm`.

**Characters** — 6-angle orbit (front, three-quarter, side, back, top, RTS):
```bash
make character_views CHARACTER=base:pig
# or: ./build/solarium-ui-vk --skip-menu --debug-scenario character_views --debug-character base:pig
```
Produces `/tmp/debug_N_{front,three_q,side,back,top,rts}.ppm`. The scenario
overrides the local player's `character_skin` prop, so any model in
`artifacts/models/base/<id>.py` can be previewed by passing `base:<id>`.

Note: non-humanoid models (pig, cat, chicken, dog) lack `hand_r/hand_l`
annotations, so any held item renders at fallback coordinates during the
preview — ignore for character-only evaluation.

Convert to PNG for inspection:
```bash
python3 -c "
from PIL import Image
for s in ['fps','tps','rpg','rts','ground']:
    Image.open(f'/tmp/debug_0_{s}.ppm').save(f'/tmp/shot_{s}.png')
"
```

For animation frames:
```bash
./build/solarium-ui-vk --skip-menu --debug-scenario animation --debug-item base:sword
```

### Step 2 — Analyse

Read all PNG screenshots. Check for:
- **Scale**: Does the item look proportional to the character? To the ground version?
- **Orientation**: Is the blade facing the right direction? Is the grip at the bottom?
- **FPS position**: Is it at bottom-right? Does it fill ~40% of vertical FOV?
- **Animation**: Does the slash sweep diagonally (not just up/down)? Does the edge lead?
- **Ground**: Is it recognisably the same item as when held?

Common issues:
| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| Held item 3–5× larger than on ground | `equip.scale` too high | Lower `scale` in model's `equip` block |
| FPS item too large | `0.55f` multiplier | Raise to `0.72f` after lowering equip scale |
| Slash looks vertical / robotic | Rotation axis is `(1,0,0)` | Use `normalize(0.8, 0, 0.5)` |
| Slash too subtle | Amplitude 40° | Raise to 65° |
| Ground item shows as colored cube | Model not found in registry | Check model key matches `"model":` field |
| Ground item invisible (model found) | Parts too thin at reduced scale | Make all dimensions ≥ 0.10 in the model |
| Item invisible in hand (TPS) | Player inside structure, scale too small | Move to open area; check `equip.scale` |
| White box around player in ground shot | RTS selection persists after camera switch | Known debug artifact — ignore for visual review |

### Step 3 — Fix

**Model Python file** (`artifacts/models/base/<item>.py`):
```python
"equip": {
    "rotation": [-20, 30, -10],  # pitch back, yaw 30° outward (face visible from front+back), slight roll
    "offset": [0.03, 0.0, -0.05],
    "scale": 0.50,               # target: ~35% of character height (for 1.4-height sword)
},
```

**C++ FPS rendering** (`src/client/game.cpp`, FPS held-item section):
```cpp
// Diagonal slash along blade edge
glm::vec3 slashAxis = glm::normalize(glm::vec3(0.8f, 0.0f, 0.5f));
float swing = std::sin(t * 3.14159f) * (-65.0f);
fpRoot = glm::rotate(fpRoot, glm::radians(swing), slashAxis);

// Scale: equip.scale × 0.72 fills ~40% of 70° FOV at z=-0.70
float fpScale = fpEs * 0.72f;
```

### Step 4 — Rebuild and re-screenshot

```bash
cmake --build build -j$(($(nproc)/2)) && \
./build/solarium-ui-vk --skip-menu --debug-scenario item_views --debug-item base:sword
```

Re-read the screenshots. Repeat until satisfied.

> **Python-only edits don't rebuild.** If you only edited a model `.py` file,
> the `solarium-ui-vk` target won't relink, so the CMake `POST_BUILD copy_directory
> src/artifacts → build/artifacts` step doesn't fire and the game
> reads the **stale cached model**. Two fixes:
>
> ```bash
> # Fast: sync just the edited file
> cp src/artifacts/models/base/pig.py build/artifacts/models/base/pig.py
>
> # Or: force a relink so POST_BUILD re-copies everything
> touch src/platform/client/main.cpp && cmake --build build -j$(($(nproc)/2))
> ```
>
> Symptom when you forget: edits "have no effect." You'll blame shaders,
> lighting, or colors — they're red herrings. Always re-check
> `build/artifacts/models/base/<id>.py` matches the edited source.

---

## Model authoring gotchas (the ones that waste an hour)

### The model loader is a literal tokenizer — no Python evaluation

`src/platform/client/model_loader.h` parses a subset: dicts, lists, numbers,
strings, `math.pi`, `True`, `False`, `#` comments. It **cannot resolve
variable references**. Code like:

```python
BASE = [0.85, 0.45, 0.52, 1]
model = {"parts": [{"color": BASE, ...}]}   # ← BASE is never substituted!
```

silently produces `color=white` on every part — symptom: the model renders
as a **plain white silhouette** regardless of palette. Rules:

- Every `color`, `offset`, `size`, `pivot`, `swing_axis` must be an inline
  literal `[...]`.
- You may keep a palette reference block in the module docstring or a
  comment, but the live data must be inlined and kept in sync by hand.
- Same rule for any repeated constant (pivot coordinates, axes): inline them
  or the loader silently drops them.

### Saturation budget — keep RGB channels below ~0.85

The shader multiplies part color by sunlight + ambient. Scene ambient at
dusk/night is already dim, but sun-lit faces can clip. Channels above `0.90`
tend to **wash toward white** on lit faces, undoing the palette work.

Guideline: pick the most saturated hue you want, then cap the dominant
channel at ~0.85. `[0.85, 0.45, 0.52]` reads as bold pink; `[0.95, 0.75,
0.72]` reads as beige/white under the same lighting.

### Z-fighting on overlay parts (stripes, spots, patches, bandanas)

Symptom: a decorative thin part appears to **flicker / flash** between its
color and the underlying body color as the camera moves. Not a lighting bug,
not a shader bug — it's **z-fighting**.

Cause: two surfaces at (nearly) the same depth. The GPU depth test can't
reliably decide which is in front, so it alternates each frame based on
floating-point rounding. Anything closer than ~0.005 world-units — or
*exactly coplanar* — is at risk.

Concrete cases that bit us:

- **Cat belly** at `y=0.17, size.y=0.04` → bottom face `y=0.15` coplanar with
  torso bottom (also `y=0.15`). Fix: shift belly down to `y=0.164` so its
  bottom face pokes out by 0.006.
- **Cat cream muzzle** at `z=-0.39, size.z=0.04` → front face `z=-0.41`
  coplanar with head front face. Fix: shift to `z=-0.396`.
- **Pig back spots** at `y=0.685, size.y=0.02` → top face `y=0.695` only
  0.005 beneath torso top `y=0.70` — enough to z-fight at far camera
  distances. Fix: raise to `y=0.706` so the top face pokes 0.016 past torso.
- **Pig cheek spots** at `x=±0.221` with head half-x `0.22` → outer face only
  0.006 past head side. Fix: push to `x=±0.224`.

### Automated check: `tools/check_zfight.py`

Rather than eyeballing every part, run the validator:

```bash
# All base models
python3 tools/check_zfight.py

# Just the characters, stricter tolerance (exact coplanar only)
python3 tools/check_zfight.py --tolerance 0.003 \
    src/artifacts/models/base/{cat,pig,dog,chicken,skeleton,knight,mage,villager,player,giant,crewmate}.py
```

The tool reports every pair of **same-direction coplanar faces** whose parts
also overlap on the other two axes — the exact geometric definition of a
z-fighting surface. Exit status is the warning count (0 = clean).

Output format:

```
[9]head  lo.z=-0.4100  ≈  [12]nose  lo.z=-0.4170  (d=0.0070)
```

means part #9 (`head`) and part #12 (`nose`) both have their -z face at
essentially the same depth. The fix: push the nose's -z face further from
the head (more outward offset). **Make this check part of the model-review
loop — run it before and after every character edit.**

### Rules of thumb

- **Never coplanar.** If part A decorates body B, offset A outward by at
  least 0.005 (prefer 0.01+) along the axis they share.
- **Poke through.** A spot/stripe should have its *visible* face poking
  *outside* the body it sits on — never flush with the body surface and
  never fully inside it (invisible).
- **When in doubt, enlarge the overlay.** Making the overlay 0.01 wider than
  the body on the shared axis guarantees its side faces aren't coplanar with
  the body's side faces.
- **After editing positions, re-check all six faces.** A 0.005 shift on y
  fixes y-coplanarity but a spot that's `x=±body_half_x` still z-fights on x.

### Scene lighting is dusk-y in the default world

The `flat` world spawns near sunset, so even properly-coloured models come
out muted in screenshots. For a fair palette check, either:

- Shoot against the spawn-platform floor (dark stone) so lit faces pop, or
- Add a `--bright` flag / noon override to the scenario (future work).

---

## Adding new scenarios

Subclass `IScenario` in `src/development/`:

```cpp
// src/development/my_scenario.h
class MyScenario : public IScenario {
public:
    const char* name() const override { return "my_scenario"; }
    bool tick(float dt, Entity* player, Camera& camera,
              const ScenarioCallbacks& cb) override {
        // ... step machine ...
    }
};
```

Register in `src/development/debug_capture.h`:
```cpp
} else if (cfg.scenario == "my_scenario") {
    m_scenario = std::make_unique<MyScenario>(cfg.targetItem);
}
```

Run with:
```bash
./build/solarium-ui-vk --skip-menu --debug-scenario my_scenario --debug-item base:sword
```

---

## Reference: debug_capture architecture

```
src/platform/development/scenario.h              ← IScenario interface + ScenarioCallbacks
src/platform/development/
  debug_capture.h                                 ← coordinator; registers scenarios
  item_views_scenario.h                           ← fps/tps/rpg/rts/ground shots for items
  animation_scenario.h                            ← progressive swing-frame capture
  character_views_scenario.h                      ← front/three_q/side/back/top/rts for characters
```

## Character evaluation rubric

When reviewing `character_views` output, score each model 1–5 on:

1. **Readability** — can you name the species in 1s from `three_q`?
2. **Silhouette** — distinctive outline from `side` and `three_q` (accessories,
   ears, tails, capes that break the rectangular base body).
3. **Palette** — ≤3 primary hues + 1 trim accent; no clashing.
4. **Proportion** — head/body/leg ratio matches species trope (pig chunky,
   cat slender, humanoid 1:2:1 head-torso-legs).
5. **Detail density** — at least one non-trivial feature per body region
   (head, torso, limbs); inset eyes, color patches, 2-voxel trim rings.

Target ≥4/5 on all five before shipping. Transferable voxel tricks:
2-voxel trim rings, 1-voxel inset eye dots, color-patch fur, layered cape
slabs (each 1 voxel narrower + darker), silhouette accessories (scabbard,
quiver, satchel), asymmetric details (one bandaged arm).

All development code is compiled into the binary but incurs **zero runtime cost** when `--debug-scenario` is not passed — the `active()` guard is checked once per frame with a branch prediction win.

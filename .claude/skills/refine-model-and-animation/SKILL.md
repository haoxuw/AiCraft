---
name: refine-model-and-animation
description: Iterative visual QA workflow for item models and animations. Use when the user wants to inspect, fix, or compare how an item looks in-game (FPS hand, TPS character, RPG/RTS overhead, ground, inventory icon) or wants to evaluate animation quality (swing, equip, idle). Covers scale, orientation, equip transform, slash axis, and the full code→screenshot→analyze→fix loop.
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

```bash
./build/agentica --skip-menu --debug-scenario item_views --debug-item base:sword
```

Produces `/tmp/debug_N_fps.ppm`, `_tps.ppm`, `_rpg.ppm`, `_rts.ppm`, `_ground.ppm`.

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
./build/agentica --skip-menu --debug-scenario animation --debug-item base:sword
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
cmake --build build -j$(nproc) && \
./build/agentica --skip-menu --debug-scenario item_views --debug-item base:sword
```

Re-read the screenshots. Repeat until satisfied.

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
./build/agentica --skip-menu --debug-scenario my_scenario --debug-item base:sword
```

---

## Reference: debug_capture architecture

```
src/development/
  debug_capture.h          ← coordinator; parses scenario name, owns IScenario
  scenario.h               ← IScenario interface + ScenarioCallbacks struct
  item_views_scenario.h    ← fps / tps / rpg / rts / ground shots
  animation_scenario.h     ← progressive swing-frame capture
```

All development code is compiled into the binary but incurs **zero runtime cost** when `--debug-scenario` is not passed — the `active()` guard is checked once per frame with a branch prediction win.

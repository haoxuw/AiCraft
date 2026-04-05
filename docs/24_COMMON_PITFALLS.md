# Common Pitfalls

Bugs we've hit and patterns to avoid. Read this before writing new features.

## 1. One-Shot vs Continuous Actions (The Egg Bug)

**Bug:** A chicken drops 13 eggs at once when startled.

**Cause:** Agent client `decide()` runs at 4Hz but `behaviorToActionProposals()`
ran at 50Hz. A `DropItem` action was converted and sent every tick (0.02s) for
12-13 ticks until the next `decide()` call replaced it.

**Fix:** Split actions into two categories:
- **Continuous** (Move, Idle, Wander, Follow, Flee): sent every tick at 50Hz for
  smooth movement. Safe to repeat — server just updates velocity.
- **One-shot** (DropItem, BreakBlock, Attack): queued in `pendingOneShots` when
  `decide()` fires, sent exactly once, then cleared.

**Rule:** When adding a new BehaviorAction type, decide if it's one-shot or
continuous. If it creates entities, modifies blocks, or deals damage — it's
one-shot. Add it to `extractOneShots()` in `agent/behavior_executor.h`.

## 2. Test Values Left in Production Code

**Bug:** `EGG_CHANCE = 1.0` (100%) and `EGG_COOLDOWN = 20.0` left in peck.py.

**Rule:** Never commit test overrides. If you need test values, use a separate
test config or environment variable, not inline constants with `# TEST` comments.

## 3. ImGui Double-Frame Input Stealing

**Bug:** Entity inspect overlay's combo boxes and buttons were unclickable.

**Cause:** `renderPlaying()` called `m_ui.beginFrame()` / `endFrame()` for its
hotbar/inventory UI. Then `updateEntityInspect()` called `m_ui.beginFrame()`
for a second frame. The first frame consumed all mouse/keyboard events.

**Fix:** `renderPlaying(dt, aspect, true)` — pass `skipImGui=true` when calling
from overlay states so it only renders 3D world without consuming ImGui input.

**Rule:** Only one ImGui frame per render cycle. If an overlay needs ImGui input,
skip the underlying frame's ImGui pass. Use the `skipImGui` parameter.

## 4. Camera Jump on Cursor Mode Change

**Bug:** Camera snaps to a random angle when closing an overlay (inspect, pause).

**Cause:** GLFW tracks mouse position. When cursor switches from NORMAL to
DISABLED, the position can be anywhere on screen. The camera's mouse delta
calculation sees a huge jump.

**Fix:** Call `m_camera.resetMouseTracking()` at every cursor mode transition.

**Rule:** Every place that calls `glfwSetInputMode(GLFW_CURSOR, ...)` must also
call `m_camera.resetMouseTracking()` in the same block.

## 5. Python Global State in Agent Processes

**Bug:** Behavior state (`_was_startled`, `_nap_timer`) as Python module globals
works because each entity runs in its own process. But if we ever batch multiple
entities per process, globals would be shared.

**Rule:** Current design: one process per entity, globals are safe. If batching
is added later, behavior state must move to per-entity dicts (passed via `self`).

## 6. Entity Spawn Position Inside Structures

**Bug:** Entities spawned at small radius from village center end up inside houses
and can't move. The spawn succeeds but the entity is trapped.

**Rule:** Mob spawn radius in world configs should be larger than the village
clearing radius. Minimum ~10 blocks for entities near the village center.

## 7. Header-Only Changes Not Triggering Rebuild

**Bug:** Adding a new entity type in `entities_animals.h` (header-only) didn't
take effect because CMake didn't detect the header change.

**Rule:** After modifying header-only content files, `touch` the `.cpp` that
includes them (e.g., `touch src/content/builtin.cpp`) to force recompilation.
Or add the headers to CMakeLists.txt source lists.

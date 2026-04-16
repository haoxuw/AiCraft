# CivCraft Camera Modes — Detailed Design

Four modes, cycled by **V**: FPS → TPS → RPG → RTS → FPS. One `Camera` struct
(`src/platform/client/camera.h`) owns all four. Each frame `processInput()`
dispatches to one `update*()` based on `mode`. This doc specifies the intended
feel, math, inputs, and transitions for each mode, plus shared behaviors.

Grounded in the current implementation (`camera.cpp`) — flagging where today's
code meets the target and where it needs work.

---

## Shared concepts

### Coordinate conventions
- Yaw 0° = +X, 90° = +Z (right-handed, Y-up). `-90°` = facing −Z ("south"),
  which is the default spawn facing for all modes.
- Pitch: 0° = horizon, +90° = straight up, −90° = straight down.
- `player.yaw` is the **body** yaw (character facing). `lookYaw/lookPitch`
  are the **camera** look direction. In FPS they're locked; in TPS/RPG/RTS
  they decouple.

### Shared smoothing (already in place, keep)
- `smoothVertical()` — asymmetric: gentle climb up (step-up), fast fall down,
  max-lag cap of 1.1 blocks so portal stairs don't leave a long trail. All
  four modes call this on `player.feetPos.y`.
- Orbit-distance smoothing: `dist += (target - dist) * min(dt*10, 1)` — used
  by TPS (`orbitDistance`), RPG (`godDistance`), RTS (`rtsHeight`). Keep the
  rate (`10/s`); it feels right.

### Shared projection
- `fov = 70°` default. **Target:** per-mode overrides (FPS 75°, TPS 70°,
  RPG 60°, RTS 55°). Narrower FOV in strategic views reduces parallax and
  makes the ground plane read as "flat."
- `near = 0.1`, `far = 500`. RTS can safely push near to 1.0 (no close
  geometry), improving depth precision for the terrain/shadow pass. **Make
  per-mode.**

### Mode cycle + preservation
- Cycling preserves: `rtsCenter`, `rtsHeightTarget`, `orbitDistanceTarget`,
  `godDistanceTarget`, `godOrbitYaw`. So switching modes and returning feels
  like resuming, not resetting.
- Cycling resets: `m_firstMouse` (prevents mouse-delta snap). Already done
  in `cycleMode()`.
- **Gap (to add):** 0.25s cinematic blend between modes on `V` press —
  lerp `position` and `front()` instead of snapping. Store pre-swap state,
  lerp toward post-swap state. Skip the lerp during first-frame spawn.

---

## Mode 1 — FPS (First-Person Shooter)

**Intent:** eyes-in-the-character. Head = camera. Hands and held item
visible in front; no body visible. Used for combat, precise block placement,
close-range interaction.

### Geometry
- `position = player.feetPos + (0, smoothed_eye, 0)`, where
  `smoothed_eye = smoothVertical(feetY) + eyeHeight` (1.9 blocks for 2.5-tall
  player — already correct).
- `front()` from `lookYaw/lookPitch`; **`player.yaw` slaved to `lookYaw`**
  (body always faces look direction).
- FOV: **75°** (default 70° is a touch narrow for FPS; 75° matches Minecraft/
  most shooters without fisheye).

### Input
- Mouse X → `lookYaw += dx * sensitivity`.
- Mouse Y → `lookPitch += dy * sensitivity`; clamp `[-89°, 89°]`.
- Scroll: unused (reserved for hotbar).
- WASD → feet velocity in `playerForward()` / `playerRight()`.
- Left click / right click → attack / use item.
- Cursor state: captured (hidden, center-locked).

### Smoothing / feel
- No body visible → no head-tracking offset needed, no bob unless `walk_bob`
  is configured and we want the full Minecraft-style walk sway (off by
  default; optional toggle).
- **Gap:** view-bob during sprint isn't implemented. Recommend: 1cm vertical
  sway at step frequency (already have `walk_speed` per model — reuse).

### Transitions
- From TPS: camera position lerps to eye; `lookYaw/lookPitch` already match
  (TPS uses them), so no spin.
- From RPG/RTS: `lookYaw = orbitYaw` at swap, `lookPitch = 0°`; camera
  glides forward along the ground to the player's eye over 0.25s.

---

## Mode 2 — TPS (Third-Person, over-the-shoulder)

**Intent:** Fortnite/Zelda style. Character visible in front of camera; orbit
with mouse; character turns to face movement direction. Used for general
exploration, cinematic combat, animation critique.

### Geometry
- Orbit anchor: `target = (feetX, smoothedFeetY + eyeHeight * 0.8, feetZ)`
  (slightly below eye — already correct).
- Offset vector of length `orbitDistance` from `orbitYaw / orbitPitch`:
  ```
  offset = (-cos(yaw)·cos(pitch), sin(pitch), -sin(yaw)·cos(pitch)) · distance
  position = target + offset
  ```
  Camera looks back at `target`.
- FOV: **70°**. Orbit distance: **6 blocks** default, zoomable 2–15.
  `distance = 2` is the "shoulder-cam" limit (right before FPS territory).
- Pitch range: `[-60°, +85°]` (already tuned). Negative = camera above
  looking down; positive = camera low looking up (sky).

### Input
- Mouse X → `orbitYaw += dx`.
- Mouse Y → `orbitPitch -= dy` (mouse-up = view aims up, convention matches
  RPG; already correct).
- Scroll → `orbitDistanceTarget` ± step (step = 1 block, clamp `[2, 15]`).
- Player turn: **character yaw follows movement direction, not look
  direction** — handled in `gameplay.cpp` (Fortnite-style smooth turn).
  Camera does *not* set `player.yaw` here. Already correct.
- Cursor captured.

### Smoothing / feel
- Orbit-distance lerp: in place.
- **Gap (important):** camera collision with terrain. If a chunk sits
  between `target` and orbit position, raycast along the offset, clamp
  `orbitDistance` to `hit_distance - 0.3`. No pop-through. Use existing
  raycast from `src/CivCraft/client/raycast.h`. Restore to target distance
  smoothly (ease-in 10/s) once clear.
- **Gap:** occlusion fade — if camera is *inside* an entity's bounding box
  (e.g. player standing under an overhang), fade own-player render to 30%
  alpha. Data-driven test: whenever `orbitDistance` was clamped > 1 block
  under target for ≥2 frames.

### Transitions
- To FPS: zoom in by animating `orbitDistance` to 0 over 0.25s, then snap.
- To RPG: `godOrbitYaw = orbitYaw`, `godDistanceTarget = 20`; camera eases
  up and back.

---

## Mode 3 — RPG (Minecraft Dungeons / ARPG overhead)

**Intent:** fixed angled-down view, mid-range. Character always visible;
screen-relative movement (W = screen-up regardless of facing); used for
build-and-explore with readable surroundings and more situational awareness
than TPS.

### Geometry
- Anchor: same as TPS (`target = feet + eyeHeight*0.8 Y`).
- Elevation `godAngle` default **55°** (above horizontal — already correct).
  Range `[-60°, 85°]` (mouse-Y).
- Orbit yaw `godOrbitYaw` independent of `player.yaw`.
- Distance `godDistance` default **20 blocks**, scroll `[10, 40]`.
- FOV: **60°** (tighter than TPS — ground reads flatter, objects don't
  balloon near the screen edge).

### Input
- Mouse X → `godOrbitYaw += dx`.
- Mouse Y → `godAngle -= dy`, clamped.
- Scroll → `godDistanceTarget`.
- **Movement is camera-relative, not player-relative.** `W/A/S/D` use
  `godCameraForward/Right()` (already implemented; gameplay uses these when
  `mode == RPG`). Character yaw snaps to movement direction (smooth-turn,
  same as TPS).
- Cursor captured.

### Smoothing / feel
- Same collision behavior as TPS (raycast-clamp the offset, restore
  smoothly). **Gap, not yet implemented.**
- **No edge-pan.** The camera always tracks the player — the player *is*
  the focus.

### Transitions
- From TPS: `godOrbitYaw = orbitYaw`, `godDistanceTarget = max(orbitDistance,
  10)`; ease.
- To RTS: `rtsCenter = player.feetPos`, `rtsOrbitYaw = godOrbitYaw`,
  `rtsHeightTarget` picked so vertical screen size matches current RPG
  framing (see RTS Geometry below — choose height so the ground-plane
  coverage at pitch 30° equals RPG coverage at pitch `90°-godAngle`).

---

## Mode 4 — RTS (WoW-style / StarCraft angled overhead)

**Intent:** detach camera from player. Pan freely, select groups, command
units (including your own character). Used for base overview, group
commands, large-scale tactical view.

### Geometry
- `rtsCenter` is a *world* point, not the player. Camera orbits `rtsCenter`.
- Pitch (elevation) `rtsAngle` default **30°** (low-ish, silhouettes read
  per the comment in `camera.cpp`). Range `[15°, 85°]`.
- `rtsHeight` is the camera's Y above `rtsCenter`. `horizontalDist =
  rtsHeight / tan(rtsAngle)` so the elevation tag matches visual feel
  (already fixed — was incorrectly squashed by `0.3f`).
- Yaw `rtsOrbitYaw`, mouse-drag rotates (WoW RMB). Already multiplicative
  zoom via drag-Y (good).
- Zoom range: `[5, 120]` height. FOV **55°** (tighter still — minimizes
  parallax distortion at extreme height).

### Input
- RMB + drag:
  - X delta → `rtsOrbitYaw += dx * 2.5`.
  - Y delta → `rtsHeightTarget *= 0.985^dy` (multiplicative zoom; already
    correct).
- Scroll: **pitch control** (not implemented today — add). Scroll up →
  pitch toward top-down (up to 85°); scroll down → pitch toward horizon
  (down to 15°). Separating pitch (scroll) from zoom (RMB-drag) matches
  WoW and disentangles the two axes.
- Middle mouse → `rtsCenter = player.feetPos` (center on player). Already
  in.
- WASD → pan `rtsCenter` along camera-yaw-relative `fwd/right`. Already in.
- Edge-scroll → pan when cursor within 30px of window edge. Already in.
- Shift → 2.5× pan speed. Already in.
- Cursor **free** (not captured) — RTS needs the pointer for select/command.
  Left click = select, right click = issue move/attack command. The
  existing `gameplay.cpp` already does box-select and click-to-move in this
  mode; camera must not capture the cursor.

### Smoothing / feel
- Height lerp in place.
- **Gap:** center-on-player shouldn't snap — ease `rtsCenter` toward
  `player.feetPos` over 0.3s on MMB. Today it snaps.
- **Gap:** optional "follow selected" mode — if exactly one unit is
  selected and the user double-taps MMB, `rtsCenter` tracks that unit
  until panned. Nice-to-have, land after the rest.
- Shadow pass light bounds: RTS sees far more terrain than FPS/TPS/RPG.
  The shadow map must cover the visible frustum at current
  `rtsHeight` — currently sized for the close modes. **Gap:** scale
  `shadow_ortho_size` with `rtsHeight`.

### Transitions
- From RPG: described above (match coverage at swap).
- To FPS: snap `rtsCenter = player.feetPos` first, then lerp
  `position → eyePos`, `front → playerForward`. 0.4s (longer than other
  transitions since the travel distance is larger).

---

## Work items (focused designs)

Each gap is written as a self-contained design: root cause, proposed
changes keyed to files, what the change preserves, and open questions to
resolve before coding. Land them in the listed order; each is independent.

---

### 1. Per-mode FOV and near/far planes

**Root cause**
`camera.h:35-37` holds a single `fov = 70`, `nearPlane = 0.1`,
`farPlane = 500`. `projectionMatrix()` uses them unconditionally. Same
numbers for eyes-in-head (FPS) and bird's-eye (RTS) produce parallax
distortion at height and feel cramped up close.

**Design**
Per-mode values. FPS favors wider FOV for spatial awareness; RTS tighter
to flatten the ground plane; larger `near` in RTS buys depth precision
because nothing is ever within 1 block of the camera.

**Proposed changes**
1. `camera.h` — add `struct ModeProjection { float fov, near, far; }`, an
   array `m_proj[4]` indexed by `CameraMode`, and `currentProjection()`
   returning the active entry.
2. `camera.cpp` — initialize: FPS `{75, 0.1, 500}`, TPS `{70, 0.1, 500}`,
   RPG `{60, 0.3, 500}`, RTS `{55, 1.0, 600}`. Rewrite
   `projectionMatrix()` to use `currentProjection()`.
3. Mode-swap blend (item 2 below) must also interpolate FOV/near/far so
   zoom doesn't pop.

**What this preserves**
- Aspect handling, shader matrices, frustum culling all unchanged.
- Render order, shadow pass untouched (shadow pass has its own ortho
  projection).

**Open questions**
- Do we want the user to tune FOV in a settings menu? If yes, store per-mode
  deltas on top of defaults instead of absolute values. Default: no, hardcode.

---

### 2. Cinematic blend on mode swap

**Root cause**
`cycleMode()` at `camera.cpp:31-35` changes `mode` instantly. Next frame
`updateX()` writes a wildly different `position` and `lookYaw/Pitch`, so
`V` feels like a teleport.

**Design**
On `V`, capture the pre-swap `{position, lookYaw, lookPitch, fov, near}`
as "from" and compute the first-frame post-swap values as "to". For the
next ~0.3s, overwrite the normal updater's output with a lerped pose.
Then release to the mode's updater.

**Proposed changes**
1. `camera.h` — add `struct SwapBlend { float t, duration; glm::vec3
   posFrom, posTo; float yawFrom, yawTo, pitchFrom, pitchTo, fovFrom,
   fovTo; bool active; }`.
2. `camera.cpp` — `cycleMode()`: record `posFrom/yawFrom/…`, advance mode,
   run the new mode's updater once to compute `posTo/yawTo/…`, set
   `active=true, t=0`.
3. `processInput()` — if `active`, advance `t`; run the mode updater
   normally, then overwrite `position/lookYaw/lookPitch/fov` with lerped
   values (smoothstep easing); clear `active` when `t >= duration`.
4. `viewMatrix()` / `projectionMatrix()` — no change (they read the
   lerped fields).
5. Suggested durations: FPS↔TPS 0.25s; TPS↔RPG 0.3s; RPG↔RTS 0.35s;
   anything↔RTS 0.4s (larger travel).

**What this preserves**
- Mode updaters are unchanged; blend is purely an output filter.
- Mouse-delta reset (`m_firstMouse`) in `cycleMode()` still fires, so
  cursor-locked modes don't snap on resume.
- Server sees no change — camera is client-only.

**Open questions**
- Should WASD input during blend still move the player? Yes — gameplay
  doesn't freeze; only the view eases. User retains control.
- Should yaw wrap around the short way (e.g. −170° → +170° should go the
  30° way, not 340°)? Yes — normalize delta to `[-180°, 180°]` before
  lerping. Trivial, but easy to forget.

---

### 3. TPS + RPG camera terrain collision

**Root cause**
TPS (`camera.cpp:140-160`) and RPG (`161-182`) blindly place the camera
`orbitDistance` / `godDistance` along the offset vector. When the player
walks up to a wall or backs into a corner, the camera sits *inside* the
wall, revealing a view of chunk interiors (black/untextured back-faces)
and popping through.

**Design**
Cast a ray from the anchor (`target`) outward along the offset direction,
length = desired distance. If it hits a solid block closer, clamp the
effective distance to `hit - 0.3` (0.3-block standoff so near-plane
doesn't clip through the surface). When the obstruction clears, ease
back to desired distance (10/s, same as the scroll-zoom lerp).

**Proposed changes**
1. `camera.h` — add `float m_effectiveOrbit, m_effectiveGod;` (cached
   smoothed values).
2. `camera.cpp` — new private helper
   `float clampDistanceByTerrain(const World&, const glm::vec3& anchor,
   const glm::vec3& dir, float desired)`. Needs a `World*` reference — inject
   via a setter called once per frame from `game_render.cpp` before
   `processInput()`.
3. `updateThirdPerson()` / `updateRPGPosition()` — replace the raw
   `orbitDistance` / `godDistance` in the offset calc with
   `m_effectiveOrbit` / `m_effectiveGod`, which each frame eases toward
   `clampDistanceByTerrain(target, dirOutward, requested)`.
4. Raycast reuses `src/CivCraft/client/raycast.h` (existing chunk raycast).
   **Important:** `raycastBlocks` is the primary block ray — it already
   treats open doors as pass-through after the door fix (see `raycast.h`
   open-door design). We want identical pass-through here: camera should
   not clamp on open-door cells.

**What this preserves**
- Orbit yaw/pitch math, scroll zoom, target anchor — unchanged.
- FPS (no orbit) and RTS (top-down; collisions not an issue at typical
  heights) untouched.

**Open questions**
- Standoff distance: 0.3 is a guess. If near-plane is 0.1 (FPS) vs 0.3
  (RPG) per item 1, set standoff = `2 * currentProjection().near` so it
  scales with the projection.
- Should the camera raycast ignore transparent blocks (leaves, glass)? Yes —
  otherwise tree canopy shoves the camera into the player's head. Add a
  `solid_only` flag to the raycast call.

---

### 4. Own-player occlusion fade

**Root cause**
When item 3 clamps TPS orbit to near 0 (e.g. player backs against a wall),
the camera is effectively inside the player's body. The player model fills
the screen and hides everything.

**Design**
Fade the local player's model alpha based on distance from camera. At
full orbit distance: α=1. Below 2 blocks: α lerped to 0.3. At 0: α=0
(fully invisible, FPS-like).

**Proposed changes**
1. `camera.h` — add `float ownPlayerAlpha() const` returning α computed
   from mode + effective orbit distance.
2. `src/platform/client/entity_drawer.cpp` — when rendering the entity
   that matches the local player id, multiply final color α by
   `camera.ownPlayerAlpha()`.
3. `camera.cpp` — implement: FPS → 0 (we never want to see own body in
   FPS); TPS/RPG → `smoothstep(0, 2, m_effectiveOrbit) * 0.7 + 0.3`
   (α ∈ [0.3, 1.0]); RTS → 1.
4. Model rendering path must already support alpha. Check `model.cpp`
   fragment shader; if it's opaque-only, add an alpha uniform + enable
   blend when α<1. Minor shader change.

**What this preserves**
- Other entities render unchanged.
- No impact on hit/attack logic — visibility is purely visual.

**Open questions**
- Should held items / equipped armor fade with the body? Yes, for
  consistency; attach the same α to the equipment draw path.
- Is this worth it in FPS if FPS already sets α=0? Yes — a sprint animation
  with `walk_bob > 0` can momentarily raise the torso into the view
  frustum; the fade hides it cleanly.

---

### 5. RTS scroll wheel controls pitch, not zoom

**Root cause**
Today in `updateRTS` (`camera.cpp:184-239`), zoom is RMB-drag Y
(multiplicative) and pitch `rtsAngle` is not adjustable at runtime at all
— it's fixed at 30°. Scroll wheel is unused. WoW convention is scroll =
zoom, drag = rotate; we diverge deliberately because multiplicative-drag
zoom is already a nice feel worth keeping. So scroll is free, and pitch
is the missing axis.

**Design**
Scroll up → pitch toward top-down (rtsAngle toward 85°). Scroll down →
pitch toward horizon (toward 15°). Per-notch step 3°. Smooth with 10/s
lerp (`rtsAngleTarget` + `rtsAngle`), same pattern as height.

**Proposed changes**
1. `camera.h` — add `float rtsAngleTarget = 30.0f;`.
2. `camera.cpp` `updateRTS()` — add scroll hook (GLFW scroll callback
   already exists in `window.cpp`; route to camera via the same path
   orbit distance uses today). On scroll delta `s`:
   `rtsAngleTarget = std::clamp(rtsAngleTarget + s * 3.0f, 15.0f, 85.0f);`
3. Each frame: `rtsAngle += (rtsAngleTarget - rtsAngle) * min(dt*10, 1)`
   before computing `horizontalDist`.

**What this preserves**
- RMB-drag zoom still multiplicative on height (current feel intact).
- MMB center-on-player, WASD pan, edge-scroll — all unchanged.

**Open questions**
- At `rtsAngle` near 85° (near top-down), camera-relative WASD `fwd` is
  almost parallel to world-down — `fwd = (-cos(yaw), 0, -sin(yaw))` is
  still horizontal, so no issue. Confirm by test.
- Do we want keyboard pitch too (e.g. `[` / `]`)? Nice to have; defer.

---

### 6. RTS center-on-player ease

**Root cause**
`camera.cpp:191-193`: MMB sets `rtsCenter = player.feetPos` immediately.
If camera was 80 blocks away scanning a distant battle, the view jumps.

**Design**
On MMB press, start a 0.3s ease from current `rtsCenter` to
`player.feetPos`. If the user pans with WASD during the ease, cancel it
(their input wins).

**Proposed changes**
1. `camera.h` — add `glm::vec3 m_recenterFrom, m_recenterTo; float
   m_recenterT, m_recenterDuration = 0.3f; bool m_recentering = false;`.
2. `camera.cpp` `updateRTS()` — on MMB-press edge (not hold!), capture
   `m_recenterFrom = rtsCenter`, `m_recenterTo = player.feetPos`,
   `m_recenterT = 0`, `m_recentering = true`.
3. If `m_recentering`, advance `m_recenterT += dt`; set `rtsCenter =
   mix(from, to, smoothstep(0,1,m_recenterT/m_recenterDuration))`. Clear
   when `t >= duration`.
4. If any of WASD or edge-scroll moved `rtsCenter` this frame, set
   `m_recentering = false`.

**What this preserves**
- MMB-held-centering behavior could be a follow mode, but today MMB is
  a discrete press; design keeps it discrete.

**Open questions**
- Should MMB double-press enter a "follow player" mode (camera tracks
  player each frame until any pan input)? Nice to have, separate item —
  defer.

---

### 7. RTS shadow-map scale

**Root cause**
The shadow pass (in `src/CivCraft/client/renderer.cpp`) uses a fixed
orthographic half-extent tuned for FPS/TPS viewing distances (~40 blocks).
In RTS at `rtsHeight = 80`, visible ground extends ~140 blocks along the
view axis — but shadows cut off at ~40, leaving a hard "no shadow" line
across the map.

**Design**
The shadow-caster ortho half-extent must cover the visible ground region.
Compute from the camera mode + zoom: for RTS, scale with `rtsHeight`; for
other modes, use the current fixed value.

**Proposed changes**
1. `camera.h` — add `float desiredShadowHalfExtent() const`. FPS/TPS/RPG
   return current constant (e.g. 48); RTS returns
   `max(48, rtsHeight * 1.5f)`.
2. `src/CivCraft/client/renderer.cpp` — in the shadow pass setup, read
   `camera.desiredShadowHalfExtent()` and pass to `glm::ortho`.
3. Shadow map resolution may need to grow proportionally to avoid
   blurry far shadows. Option: adaptive — increase resolution (1024 →
   2048) when half-extent exceeds 96. Or accept the softening and keep
   one resolution.

**What this preserves**
- Other modes' shadow quality unchanged (same half-extent).
- No Vulkan-specific impact; gate applies to both GL and Vulkan backends.

**Open questions**
- Adaptive resolution or accept softening? Softening is simpler; start
  there and bump if it looks bad on the F2 screenshots.
- Cascaded shadow maps would be the robust answer for all modes, but
  that's a much larger renderer change — separate project.

---

### 8. FPS view-bob (optional)

**Root cause**
None — feature gap. FPS camera is static-relative-to-feet; walking has no
visual tell. Minecraft has this; players notice when it's missing.

**Design**
Vertical and horizontal sway at step frequency, amplitude ∝ movement speed.
Phase driven by `cumulative_distance_moved / stride_length`. Zero when
stationary. Off by default (toggle in settings; A/B against F2 screenshot
baselines).

**Proposed changes**
1. `camera.h` — add `bool viewBobEnabled = false; float m_bobPhase = 0;`.
2. `camera.cpp` `updateFirstPerson()` — accumulate
   `m_bobPhase += horizontalSpeed * dt / strideLength`; compute
   `bobY = sin(m_bobPhase * 2π) * ampY`, `bobX = sin(m_bobPhase * π) *
   ampX`. Add `(bobX * playerRight()) + (0, bobY, 0)` to `position`.
3. Values: `strideLength = 0.9`, `ampY = 0.04`, `ampX = 0.03` (in blocks).
   Tune against recorded walk clips.

**What this preserves**
- Off by default — no regression risk.
- Gameplay raycast origin should be `position - bob_offset` (unbobbed)
  so aim doesn't wobble. Add a `Camera::aimOrigin()` that returns
  `position - bob_offset`; raycast callers use that.

**Open questions**
- Enable by default eventually? Depends on user testing. Ship disabled,
  collect feedback.

---

### 9. Movement-direction mapping (documentation)

**Root cause**
Not a bug — the table is accurate today (verified in `gameplay.cpp`).
Documented here so future edits don't break the convention.

| Mode | WASD basis | Player yaw follows |
|---|---|---|
| FPS | `playerForward() / playerRight()` | `lookYaw` (camera) |
| TPS | `playerForward() / playerRight()` | movement direction (smooth-turn) |
| RPG | `godCameraForward() / godCameraRight()` | movement direction |
| RTS | WASD pans `rtsCenter`, not player | player yaw unchanged; movement from click commands only |

**Proposed changes**
None (documentation only). When touching `gameplay_movement.cpp` or
`camera.cpp`, cross-check against this table.

---

## File impact (aggregate)

All four camera modes are client-local. No protocol, server, or agent
changes — Rule 3 preserved.

- `src/platform/client/camera.h` — per-mode projection, swap-blend state,
  effective-orbit state, recenter state, `desiredShadowHalfExtent()`,
  `ownPlayerAlpha()`, `aimOrigin()`, `viewBobEnabled`.
- `src/platform/client/camera.cpp` — new helpers
  (`clampDistanceByTerrain`, blend logic, recenter, bob); extended
  `processMouse` RTS branch (scroll → pitch); world-ref setter.
- `src/platform/client/entity_drawer.cpp` — own-player alpha multiply.
- `src/platform/client/model.cpp` / model fragment shader — alpha uniform
  + blend enable when α<1 (if not already supported).
- `src/CivCraft/client/renderer.cpp` — shadow half-extent from camera.
- `src/platform/client/game_render.cpp` — inject `World*` into camera
  once per frame for terrain-collision raycast.
- `src/platform/client/gameplay_movement.cpp` — use `Camera::aimOrigin()`
  for raycast origin if view-bob enabled.

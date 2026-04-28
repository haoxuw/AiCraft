# Core Gameplay Features

> **This is the player-facing contract.** If a change here doesn't also ship in
> the client, it isn't done. All four camera modes, all three role fantasies
> (builder / adventurer / commander), and all core NPC loops must work in the
> default world on a fresh install.

---

## 1. Four Camera Modes (one key, one body)

**One player entity, one inventory, one save file.** Pressing **V** cycles
view mode — it does *not* switch characters. The same avatar keeps walking,
the camera just reframes it.

| Mode | Cam position         | Mouse         | LMB               | Movement         |
|------|----------------------|---------------|-------------------|------------------|
| FPS  | Eye-height, inside head | Look         | Swing / break block (short press) | WASD relative to facing |
| TPS  | Over-shoulder orbit, zoomable | Orbit       | Swing / break block              | WASD relative to facing |
| RPG  | High orbit ("god view") | Orbit (LMB hold) | Click-to-move (ground) / attack (enemy) | WASD relative to cam yaw |
| RTS  | Top-down, angled down | Pan edge-scroll + orbit | Box-select (drag) / issue order (click) | WASD pans camera, not body |

Key differences:

- FPS and TPS share identical input — they're the **same mode rendered at
  different distances**. Zoom (`MouseWheel`) slides continuously from TPS
  into FPS, so "which camera am I in" is rarely a decision.
- RPG's mouse cursor is a **ray into the world**, not a crosshair in the
  center. LMB targets whatever the cursor is over (ground = move; enemy =
  attack; item on ground = pickup).
- RTS detaches the camera from the body. The body keeps its last facing;
  the camera pans freely with WASD and edge-scroll.

See `src/platform/client/camera.h` for the shared `Camera` class that owns
all four mode state machines (orbit yaw/pitch, god angle, RTS center).

### Camera collision (shared across all modes)

The camera must **never clip through terrain**. When the orbit position
would land inside a solid block:

1. Cast a short ray from the target (player head) toward the intended camera
   position.
2. If the ray hits a solid block before reaching the target distance, pull
   the camera forward along the ray to just in front of the hit, with a
   small air gap (0.15m) so the near plane doesn't slice voxels.
3. Smoothly interpolate `orbitDistance` back toward its target when the
   obstruction clears, so the camera doesn't snap.

On stairs, ramps, and slopes, the player body steps up to 1.0 block
automatically (`moveAndCollide`'s `stepHeight`). The camera follows the
head position smoothly — step-up transitions should not pop. If you see
a visible camera jerk on a staircase, that's a bug: add smoothing to
`feetPos` on the camera side, not the physics side.

---

## 2. Minecraft-Like Building — FPS & TPS

The builder fantasy. In these modes the player acts on one block at a time,
straight ahead.

### Core verbs

- **LMB (short press)** — swing / attack the entity in front. No target →
  damage nothing, just emit a swing animation.
- **LMB (hold on block)** — break the block in front. Admin mode = instant
  break; survival mode = a hit-count loop in `gameplay_interaction.cpp`
  that accumulates progress across ticks.
- **RMB on solid block** — place the currently-selected hotbar item at the
  adjacent air cell. Consumes one from the hotbar stack. Server rejects
  silently if you don't have the item (or admin-mode bypass if set).
- **MMB on block** — copy the block's type into the hotbar ("eyedropper").
- **Scroll / 1-9, 0** — select hotbar slot.

### What the engine sends

Break and place both compile to `ActionProposal::Convert` (see
`docs/03_ACTIONS.md`):

```
break:  fromItem = "stone"   toItem = "stone"     convertFrom = Container::block(pos)
place:  fromItem = "stone"   toItem = "stone"     convertInto = Container::block(pos)
attack: fromItem = "hp"      toItem = ""          convertFrom = Container::entity(eid)
```

Value-conservation rules ensure the player can't dupe — breaking a block
removes one instance from the chunk and adds one to ground-as-item; placing
does the inverse.

### Inventory & hotbar

- Inventory slots are unlimited in distinct types but capped in
  `totalValue()` (see `docs/31_CARRY_CAPACITY.md`).
- The 10-slot **hotbar** is the persisted subset the player has flagged as
  quick-access. Client sends `C_HOTBAR` when the slot assignment changes;
  server stores it per-character in `inventories.bin`.
- **Tab** opens the full inventory panel (drag-drop between inventory ↔
  hotbar ↔ equipment slots ↔ open chest).

---

## 3. Minecraft Dungeons — RPG Mode

The adventurer fantasy. High orbit camera, click-to-move, ranged and AoE
abilities on the hotbar. The player feels like an action-RPG hero, not a
surveyor.

### Core verbs

- **LMB on ground** — raycast through the cursor; if the hit is a walkable
  cell (solid ground, 2 empty blocks above), send `C_SET_GOAL` with the
  target. The server's greedy steering (`src/server/pathfind.h`) walks the
  player there. If WASD is pressed, the move order is canceled.
- **LMB on entity** — attack target. Server picks the nearest entity under
  the ray within attack range and applies damage via `Convert`.
- **RMB / MMB / 1-9** — trigger the currently-bound abilities (dodge,
  heal, area slam, etc.). Ability artifacts live in
  `artifacts/effects/base/` and bind to hotbar slots like items.
- **Spacebar** — dodge-roll in the direction of WASD input (brief
  invulnerability + displacement).

### Server-side nav

The player entity does **not** get an agent client. Click-to-move uses the
server's simple greedy steering — no A\* search, no waypoint list on the
client. The server sets velocity each tick toward the goal; obstacles are
resolved by `moveAndCollide`'s step-up + slide-along-wall behavior.

For more complex paths (through doors, around houses), Python nav
(`src/Solarium/python/pathfind.py`) runs on NPC agent clients, not on the
player.

---

## 4. RTS — Commander Mode

The commander fantasy. Camera floats independently of the body. The player
orchestrates a squad (or the entire village in admin mode) through orders
painted on the ground.

### Core verbs

- **LMB drag** — box-select all owned Living entities inside the rectangle.
  Admin mode widens the filter to *all* Livings. Mouse must move >2% of
  screen for the drag to count; otherwise it's a click.
- **LMB click (with units selected)** — issue a Walk order at the cursor
  target. All selected units path to the target (client-side plan, driven
  by `rts_executor.h`).
- **LMB hold + click (>`kBuildHoldSec`)** — Build order at the cursor.
  Selected villagers haul materials + place the blueprint.
- **RMB** — cancel active order on the selection.
- **WASD / edge scroll** — pan camera center. **Q/E** — orbit yaw.
  **Mouse wheel** — zoom.
- **Shift + LMB drag** — add to selection instead of replacing.

### Local-plan rule

**RTS plans live on the client, not the server.** Each selected unit gets
a waypoint list computed by `RtsExecutor`; the client drives each unit
via per-tick `ActionProposal::Move` (same path as an agent client would).
The server has no "group goal" concept — it just sees many small Moves.
The local player inside the selection uses a virtual joystick toward the
next waypoint so direct player movement stays responsive.

This keeps Rule 0 intact (server still only accepts the four action
types) while letting the client do rich commander-style planning.

---

## 5. NPCs Run Their Own Logic

**Every living entity has a behavior, and every behavior runs in Python
on a separate process.** No `switch (type)` in C++ decides what a pig does.
See `docs/22_BEHAVIORS.md` for the full behavior index.

### What must work on a fresh install

The default village world (`artifacts/worlds/base/village.py`) spawns:

- **Dogs** that follow the nearest humanoid (`follow.py`). Player walks
  → dog pads behind at `follow_distance`. If the human moves out of
  `give_up_range`, the dog goes back to wandering.
- **Villagers** cutting wood (`woodcutter.py`):
  1. Find nearest `base:log` block within search radius.
  2. Walk to the tree (Python `pathfind.py`).
  3. Chop — `Convert` log → log in own inventory, N times until the
     trunk is harvested.
  4. Walk to the nearest `base:chest` with free capacity.
  5. `StoreItem` — `Relocate` logs from inventory into
     `Container::block(chestPos)`.
  6. Loop.
- **Pigs** in small herds that flee players, graze, and wallow in water.
- **Chickens** that scatter, lay eggs, and roost at dusk.
- **Cats** that prowl and ambush prey.
- **Raccoons, squirrels, bees, owl, beaver** — each with their own
  behavior artifact.

If any of the above breaks on a fresh world, the game is **not shippable**
— those are the minimum-viable signs-of-life that prove the server +
Python + chunk-streaming pipelines are all alive.

### Verifying NPC logic without a window

Use the log-driven verification path (CLAUDE.md §Iterative Development):

```bash
./build/solarium-ui --skip-menu --log-only
# or for VK:
./build/solarium-ui-vk --skip-menu --server --no-validation
# then grep /tmp/solarium_game.log for DECIDE / ACTION / COMBAT
```

A healthy village should emit DECIDE deltas from villagers every few
seconds (MoveTo tree → Convert → MoveTo chest → StoreItem) and MOVE
events from dogs tracking the player.

---

## 6. Smooth Movement

The feel test — if any of these are broken, nothing else matters.

### Must be smooth, never jerky

- **Stairs** — walking into a 1-block step auto-steps the body up. Camera
  interpolates head height so the view rises smoothly, not in a pop.
- **Slopes (future)** — diagonal block edges aren't implemented yet, but
  when they are, the physics must treat them as continuous incline (not a
  staircase of micro-snaps).
- **Crouching under 2-block openings** — same stepHeight rule but inverted
  (duck into a 1-block air gap).
- **Jumping onto a block** — predictable: jumpV + gravity land in the next
  cell for the default `kTune.playerJumpV` setting.
- **Gravity at edges** — walking off a ledge continues the player's
  horizontal velocity for the duration of the fall (no sudden stop).

### Client prediction must not fight the server

The client runs `moveAndCollide` locally for the local player and sends
`clientPos`. If the client's predicted position drifts more than
`CLIENT_POS_TOLERANCE` (8 blocks) from the server, the server rejects
`clientPos` and snaps. A single reject is expected (see memory note on
reject-cooldown); repeated rejects mean client and server physics are
out of sync — investigate `makeMoveParams` and `stepEntityPhysics` for
divergence (see `docs/feedback_client_pos_reject_policy.md`).

### Admin / fly mode

**F12** toggles admin; **F11** toggles fly while admin. In fly mode the
player ignores gravity, moves freely in 3D (Space = up, LCtrl = down,
Shift = boost). Server receives `ActionProposal.fly = true` so its
authoritative physics also skips gravity.

---

## 7. What's Not In Scope (Yet)

Features that are *planned* but not part of the core-gameplay ship gate:

- Chat / emotes (multiplayer social).
- Crafting recipes (discoverable recipe tree).
- Boss encounters and endgame (see `docs/90_ADVANCED_GAMEPLAY.md`).
- Full abilities system for RPG — current implementation is a stub.
- Splash screens, settings menu, difficulty selection.

These are Phase 4+ work; they must not block Phase 3 parity between
`solarium-ui` (GL) and `solarium-ui-vk` (Vulkan).

---

## Cross-References

- `docs/00_OVERVIEW.md` — process model and the four action types
- `docs/03_ACTIONS.md` — MOVE / RELOCATE / CONVERT / INTERACT contracts
- `docs/22_BEHAVIORS.md` — dog, villager, pig, chicken, cat behaviors
- `docs/30_INVENTORY_MANAGEMENT.md` — slot rules, equipment, chests
- `src/platform/client/camera.h` — shared Camera class (all four modes)
- `src/platform/client/gameplay_movement.cpp` — WASD + click-to-move + RTS
- `src/platform/server/pathfind.h` — server-side greedy steering for players
- `src/Solarium/python/pathfind.py` — Python A\* for NPCs

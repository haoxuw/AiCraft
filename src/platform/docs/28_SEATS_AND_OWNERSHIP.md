# Seats, Ownership, and Per-Seat Villages

Status: **planned, not yet implemented.** This is the canonical design reference
for the overhaul; the existing code still uses the older implicit-ownership model.

---

## Motivation

The current architecture implicitly assigns entity ownership via `Prop::Owner`
being set to "whichever client happened to be in the right place," world-gen
spawns NPCs at world creation, and the `AgentClient` discovers what it owns by
periodic sweep. This works but three things are shaky:

1. **Ownership isn't durable.** If a client reconnects from a new machine, or
   the world save is opened on a different install, owner IDs don't survive
   cleanly.
2. **Every joiner shares one village.** The single templated village is owned
   by whoever scoops it first; late joiners have no place of their own.
3. **Server-spawned NPCs have no clear owner** at world-gen time, so the
   "agent client computes every NPC's AI" invariant (Rule 4) only works because
   of post-hoc assignment heuristics.

This plan reworks the model so that every entity has an explicit, stable owner
from the moment it spawns, each player gets their own village, and the world
template is pure geometry.

---

## Design principles (invariants)

1. **Every entity has exactly one owner at all times — `player_id ∈ [1..N]`.**
   `player_id = 0` is reserved as `ENTITY_NONE` and never a valid live owner.
2. **Ownership is tied to a Player Seat, not to a machine.** Seats persist in
   the save; machines come and go.
3. **World template generates geometry only.** No entities at world-gen. A
   zero-client dedicated server is a static landscape, by design.
4. **All entity spawns are client-attributable.** Either a direct client
   request (claim a seat → spawn my village) or an action-triggered reactive
   spawn (break a beenest → bees attributed to the breaker's seat).
5. **Server has no intelligence.** Still Rule 4. The only permitted exception
   is a bounded-scope "idle wander" fallback if ever needed — and right now
   we don't need one, because every entity has an owner.
6. **Each seat gets its own village.** Villages are ≥ 256 and ≤ 512 blocks
   apart, including despawned ones. Each village is marked by a visible
   central tower (reuses the existing arcane monument).
7. **Disconnect ⇒ despawn-with-puff; rejoin ⇒ restore-from-snapshot.**
   Terrain stays. NPCs fade out visually, persist in the save, return on
   reclaim.

---

## Data model

### Entity
No structural change. `Prop::Owner` is reused as the `player_id`. The only
semantic tightening: `0` means "pre-claim / malformed" and is rejected by the
spawn pipeline at runtime.

### Seat Registry (server-side, persisted to save)

```cpp
using PlayerId = uint32_t;
using ClientId = std::array<uint8_t, 16>;  // UUID from the client's keypair file

struct SeatRegistry {
    // Soft binding — default "this keypair auto-reclaims this seat on join",
    // but any unoccupied seat is claimable by any keypair (no access control
    // in v1; Steam-ID enforcement is a future add-on).
    std::unordered_map<PlayerId, ClientId> lastKnownClient;

    // Snapshot of owned entities taken at disconnect time. Keyed by seat, not
    // by client_id, so a new keypair reclaiming an old seat gets the same
    // world state.
    std::unordered_map<PlayerId, std::vector<EntitySnapshot>> offlineSnapshots;
};
```

### Village Registry (server-side, persisted to save)

```cpp
struct VillageRecord {
    uint32_t   villageId;
    PlayerId   ownerSeat;
    glm::ivec2 centerXZ;
    enum Status { Live, Despawned } status;
};

// Registry persists every village ever sited. Despawned villages still reserve
// their footprint so new village placement can't collide with them.
std::vector<VillageRecord> villages;
```

### Client Identity (client-side, local)

Stored in `~/.civcraft/client_id.json`:
```json
{
  "client_id": "550e8400-e29b-41d4-a716-446655440000",
  "public_key":  "…base64 ed25519 pubkey…",
  "private_key": "…base64 ed25519 privkey…"
}
```

Generated on first launch; loaded on subsequent launches. **TODO** when Steam
integration lands: add `"steam_id": "…"` and prefer it when present.

### Save format additions

| File | Purpose |
|------|---------|
| `seats.bin`     | `SeatRegistry` — last-known-client per seat + offline snapshots |
| `villages.bin`  | `std::vector<VillageRecord>` — all villages ever sited |

Existing `inventories.bin` stays keyed by `player_id`.

---

## Protocol additions

| Message | Dir | Body |
|---|---|---|
| `C_CLAIM_SEAT`      | C→S | `client_id`, `requested_seat` (or sentinel "new") |
| `S_SEAT_GRANTED`    | S→C | `player_id`, `is_new` (false ⇒ client should wait for restore) |
| `S_VILLAGE_PLACED`  | S→C | `village_id`, `center`, `owner_seat` (map UI) |
| `S_ENTITY_REMOVE`   | S→C | *extended* — add `reason: died | despawned | owner_offline` |

The client must send `C_CLAIM_SEAT` before any other action. A seat-less
connection can observe but cannot propose actions.

---

## Lifecycle flows

### First-ever join for a new seat

```
C: generates client_id.json if missing
C: connects, sends C_CLAIM_SEAT(client_id, "new")
S: allocates new player_id
S: reject-samples village center (≥256, ≤512 from any registered village)
S: records Live VillageRecord
S: calls generateVillage(center) to place structures (tower + houses + fences)
S: spawns the template mob bundle — every NPC gets player_id = this seat
S: writes seat binding (lastKnownClient[pid] = client_id)
S: replies S_SEAT_GRANTED(pid, is_new=true)
S: broadcasts S_VILLAGE_PLACED and the stream of S_ENTITY for every spawned NPC
C: AgentClient adopts every entity with owner == my seat
```

### Rejoin for existing seat

```
C: sends C_CLAIM_SEAT(client_id, <some seat_id or "new">)
S: finds the seat, restores offlineSnapshots[pid] into live world
S: re-spawns depleted *resources* only (regrown wheat, respawned animals) —
   NOT the full template (that happened once on first-claim)
S: replies S_SEAT_GRANTED(pid, is_new=false)
S: streams S_ENTITY for every restored entity
C: AgentClient adopts as usual
```

Implementation collapse: `generateVillage` is called only on the first-claim
path. The rejoin path diffs the snapshot against template resources and
re-spawns what's missing (same code path as "periodic resource respawn" during
normal play).

### Disconnect

```
S: client socket drops (timeout or orderly)
S: iterates entities where owner == disconnecting seat
S: serializes each → offlineSnapshots[pid]
S: removes from live world
S: broadcasts S_ENTITY_REMOVE(eid, reason=owner_offline) to remaining clients
C (others): on reason=owner_offline → emit puff particle, no death sound
S: village stays in registry with status=Live (village never "despawns" — the
   structures remain; only the NPCs leave)
```

Note: "village despawned" in the registry specifically means the seat has been
garbage-collected (see §6), not that the owner logged off.

### Reactive spawn (beenest → bees)

```
C (breaker): sends ActionProposal(BREAK, beenest_pos)
S: validates, applies
S: beenest-break hook calls spawnEntity(type="bee", origin_seat=<breaker's seat>)
S: bees are Live with owner = breaker's seat, stream S_ENTITY
C (breaker): AgentClient adopts, bee behaviors start running
```

Invariant: **every callsite of `spawnEntity` must pass an `originator_seat`.**
Refuse to spawn if it's `0`. Enforced at compile time (no default parameter)
and runtime (assertion).

---

## Build phases

| # | Phase | Rough size |
|---|---|---|
| 1 | Identity + `C_CLAIM_SEAT` handshake | small |
| 2 | Ownership enforcement on proposals | tiny |
| 3 | Strip world-gen mob spawning | small (deletions) |
| 4 | Village siting + per-seat spawn | **large** |
| 5 | Snapshot + restore on disconnect/rejoin | medium |
| 6 | Reactive-spawn attribution audit | medium |
| 7 | End-to-end tests | small (after each phase) |

**Phases 1–3 are independently landable.** They put the scaffolding in place
without changing gameplay (Phase 3 leaves a world empty of NPCs, but Phase 4
fixes that). Phase 4 is the headline feature. 5–6 can run in parallel after 4.

### Phase 1 — Identity + seat handshake
- `src/platform/client/client_identity.h` — load/generate `client_id.json`.
- `src/platform/net/net_protocol.h` — `C_CLAIM_SEAT`, `S_SEAT_GRANTED`.
- `src/platform/server/seat_registry.h` — new class, `seats.bin` (de)serialize.
- `ClientManager` — new join path routes through the handshake.
- Test: two clients from same machine → same seat; two from different → different.

### Phase 2 — Ownership gate
- `ClientManager::validateProposal` — reject if `actor.owner != sender.seat`.
- `AgentClient::discoverEntities` — filter by `Prop::Owner == mySeatId`.
- Test: cross-seat action proposal → rejected; no agent adopts another seat's NPC.

### Phase 3 — Strip world-gen mobs
- Delete the "spawn mobs from `WorldPyConfig.mobs`" path in world template load.
- Keep the `mobs` list in Python templates — re-interpret as *per-seat spawn bundle*.
- Test: dedicated server with 0 clients has 0 entities after boot.

### Phase 4 — Village siting + per-seat spawn
- `src/platform/server/village_registry.h` — persisted `std::vector<VillageRecord>`.
- `src/platform/server/village_siter.h` — reject-sample siting within
  `[256, 512]` of the nearest registered village.
- Add a central tower to `worlds/base/village.py` (reuse the arcane monument
  blueprint; anchor it at the exact village center).
- Extend the spawn path: on `C_CLAIM_SEAT(new)`:
  1. Pick location.
  2. Record Live village.
  3. `generateVillage(center)`.
  4. Spawn mob bundle with `owner = new_seat`.
- Test: three sequential seat claims → three villages, pairwise distance ≥ 256.
- Test: first claim, disconnect, second claim → second village avoids the
  despawned footprint.

### Phase 5 — Snapshot + restore
- On disconnect: walk `m_entities`, serialize owned ones, write to
  `offlineSnapshots[pid]`, remove from live world, broadcast
  `S_ENTITY_REMOVE(reason=owner_offline)`.
- Client: particle burst at entity position on `owner_offline`, skip death SFX.
- On `C_CLAIM_SEAT(existing)`: restore, then diff against template resources
  and re-spawn what's missing.
- Test: disconnect → reconnect round-trip, entity count and positions match.
- Test: break wheat before disconnect → on rejoin, wheat has respawned.

### Phase 6 — Reactive-spawn attribution
- Thread `PlayerId originatorSeat` through every `spawnEntity` callsite.
- Callsites to audit: beenest break, TNT fuse, farmland harvest, every
  Python-triggered effect, any spawner blocks.
- No default parameter; compile-time enforcement.
- Test: break beenest → spawned bees have owner == breaker's seat.

### Phase 7 — Test matrix

| Test | Covers |
|---|---|
| `t_seat_claim_new_vs_existing` | P1 |
| `t_ownership_gate_cross_seat`  | P2 |
| `t_worldgen_no_mobs`           | P3 |
| `t_villages_spaced_≥256`       | P4 |
| `t_village_avoids_despawned_footprint` | P4 |
| `t_disconnect_snapshot_rejoin_restore` | P5 |
| `t_puff_animation_on_owner_offline`    | P5 (visual — manual/scenario) |
| `t_reactive_spawn_attributes_to_breaker` | P6 |

---

## Open questions — resolved

| Q | Decision |
|---|---|
| Max seats per world? | Soft cap 64, with timed GC: seats inactive for 30 real-time days are reclaimable, their snapshots discarded, their village `status=Despawned`. Village footprint stays reserved. |
| Village tower shape? | Reuse the existing arcane monument as the town-center tower — place one at every village's `centerXZ`. No new structure type. |
| Siting bound — from nearest or from world origin? | **Nearest.** Villages grow outward from the first one; no arbitrary world bound. |
| Seat-to-client binding — sticky or loose? | **Sticky by default, override via UI.** Same keypair auto-reclaims last seat; "play as someone else" menu allows claiming any unoccupied seat. |

---

## Deferred — explicit TODOs

- **Steam ID** binding layer on top of `client_id`. Replaces "trust the local keypair" with "trust Steam's identity." Seat registry gains a `steam_id` field; when present, seat claim requires matching Steam ID.
- **Ownership transfer** — taming, gifting, release-to-wild. Data model already supports it (`Prop::Owner` is writable). Not built in v1.
- **Access control beyond friend-coop** — anti-griefing enforcement on seat claim, action-rate limits, etc. Deferred until there's a public-server use case.
- **Template version drift on rejoin** — if the Python template changes between sessions, should newly-added template entries spawn on the old seat's rejoin? Currently the answer is "no, rejoin restores snapshot; new template entries never spawn" — revisit when it actually bites someone.
- **Per-seat village customization** — let the template vary by seat (first mayor gets the big village, later seats get smaller ones). Config knob, not architectural.

---

## Related docs and memories

- `src/platform/docs/04_SERVER.md` — server tick loop and validation.
- `src/platform/docs/06_NETWORKING.md` — protocol message layout.
- `src/platform/docs/21_BEHAVIOR_API.md` — what agent clients compute.
- Project memory `project_owned_entity_persistence.md` — the precursor for
  Phase 5; update it when Phase 5 lands.
- Project memory `project_entity_architecture_overhaul.md` — the broader
  context; update it when Phases 1–4 land.

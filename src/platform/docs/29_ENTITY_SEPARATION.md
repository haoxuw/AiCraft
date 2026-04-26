# Entity Separation (Client-Side Soft Collision)

Status: **design — implementation pending.**

Soft entity-vs-entity repulsion that runs on the client and feeds the regular
`TYPE_MOVE` path. The server stays authoritative; clients only ever bias the
`desiredVel` they were already going to submit.

---

## Why client-side

Rule 0: the server accepts four action types. `TYPE_MOVE` carries a
`desiredVel`. There is no "push" action and we are not adding one.

Rule 4: all intelligence lives on agent clients. NPC steering already runs
there; player steering runs on the GUI client. Both paths terminate in a
single line — the moment before `desiredVel` becomes a `MoveTo` proposal — and
that is the only place separation needs to plug into.

Rule 6: physics is unified. The server still runs `moveAndCollide`; the
client just nudges its requested velocity. No new physics path, no
reconciliation, no special server logic. Visual jitter from a one-frame
desync is acceptable per the visual-only-races rule.

The result: separation is a pure function `(self, neighbors, walls) → Δv`
applied at exactly two callsites:

- `agent.h::sendMove` — NPCs (line ~981)
- `game_vk_playing.cpp` — player input (~line 483)

---

## Goals (from the design conversation)

1. **Entity-space spacing.** Use each entity's `collision_box` radius, not a
   fixed constant. Two big entities sit further apart than two small ones.
2. **Group-stable.** Three+ agents pushing into a wedge must not blow up.
3. **Hard walls win.** Two agents pressing each other into a wall must not
   tunnel into the wall — the wall's contribution dominates.
4. **Symmetric for NPCs and players.** No "player gets a special pushrank"
   path. The asymmetry is moving-vs-stationary, not creature-vs-character.
5. **Stationary entities yield.** A walking player must be able to push past
   a sleeping NPC or an idle other-player.
6. **SC2-smooth.** Anticipatory (TTC-based), not just penetration-based, so
   agents slide past each other before they ever touch.

---

## Notation

Per pair (self `i`, neighbor `j`), in 2D — the y-axis is gravity-only and
plays no part in separation:

| Symbol | Meaning |
|---|---|
| `p_i, p_j ∈ ℝ²` | XZ position |
| `v_i, v_j ∈ ℝ²` | current `desiredVel` (intended velocity, not actual `velocity`) |
| `r_i, r_j ∈ ℝ` | XZ-projected radius of `def.collision_box` (max of x/z half-extent) |
| `R = r_i + r_j + ε` | combined radius with slack `ε = 0.05 m` |
| `d = p_i − p_j` | offset, self-from-other |
| `v = v_i − v_j` | relative velocity |
| `w_i ∈ [0, 1]` | self's share of the avoidance burden (asymmetric — see §5) |

---

## 1. Time-to-Collision (TTC)

Two disks of combined radius `R` collide when `|p_i + v_i·t − p_j − v_j·t| = R`.
Substituting `d` and `v`:

```
|d + v·t|² = R²
(v·v) t² + 2(d·v) t + (d·d − R²) = 0
```

Coefficients:

```
a = v·v
b = d·v
c = d·d − R²
```

Cases:

- `c ≤ 0` → already overlapping, set `τ = 0` and use the **penetration**
  branch (§4).
- `a < ε_a` (essentially parallel motion) → `τ = ∞`, no force.
- `b ≥ 0` (diverging) → `τ = ∞`, no force.
- Otherwise:
  ```
  disc = b² − a·c
  τ = (−b − √disc) / a    if disc ≥ 0, else ∞
  ```

We only emit a force while `τ < τ_max`. Beyond that, we don't care yet.

---

## 2. Anticipatory steering (Karamouzas–Berseth–Guy 2014)

The collision normal at the moment of impact is

```
n_τ = (d + v·τ) / R
```

This is the unit vector from `j` to `i` *at the predicted contact point*,
not the current offset. Steering along `n_τ` moves us to the side we'd be
on when we collide — that's why this is "anticipatory" rather than just
"away from the current center."

Magnitude rolls off with τ:

```
g(τ) = max(0, 1/max(τ, τ_min) − 1/τ_max)
```

`τ_min` clips the singularity at τ=0. `τ_max` is the look-ahead horizon.

Per-pair velocity correction:

```
Δv_anticip(i, j) = k_anticip · w_i · g(τ) · n_τ
```

The `1/τ` shape is what makes it feel like SC2: weak at long range, smooth
ramp-up, but never so stiff that two close-passing agents lock together.

---

## 3. Head-on tie-breaker

When two agents head straight at each other, `n_τ` is parallel to `v`,
both sides have equal claim on "go around", and they jitter. Detect with

```
v̂ = v / |v|
parallel = |n_τ · v̂| > 0.95
```

When parallel, add a deterministic lateral component:

```
side = (eid_i & 1) ? +1 : −1     // stable per-entity tie-break
perp = rotate(n_τ, 90°)
Δv_anticip += 0.5 · k_anticip · w_i · side · perp
```

Both agents independently pick a *different* parity bit, so they slide past
on opposite sides. (If both parities collide — same eid LSB — one will win
the wall test instead, which is acceptable.)

---

## 4. Penetration fallback

If `c ≤ 0` we're already overlapping; TTC is zero or undefined. Push out
along the current offset, scaled by overlap depth:

```
overlap = R − |d|
Δv_pen(i, j) = k_pen · w_i · overlap · (d / |d|)
```

This is a stiff spring — it has to overcome whatever shoved them together
this frame, but it's only active during overlap so it doesn't ring.

---

## 5. Asymmetric weights (the moving-yields-to-stationary rule)

`w_i` decides how much of the correction `i` shoulders. We pick by a 2×2
table on intent — *intent* meaning `|v_i| > ε_v`, not measured velocity:

| self moving? | other moving? | `w_i` | Note |
|:---:|:---:|:---:|:---|
| no | no | 0.0 | both idle: do nothing |
| no | yes | 0.0 | I'm stationary, they're walking — they go around me |
| yes | no | 1.0 | they're stationary, I dodge entirely |
| yes | yes | 0.5 | both moving — split, à la ORCA |

Two consequences worth naming:

- **Player pushes sleeping NPC.** Player has `|v|>0`, NPC has `|v|=0`, so
  `w_player = 1.0` and `w_npc = 0.0`. The player dodges *around* — but
  because the dodge is anticipatory, the NPC ends up a few cm out of the
  way without ever submitting a Move. (See §7 for the optional reactive
  hook that lets the NPC actually shuffle.)
- **No creature/character distinction.** Behavior-driven walk and player
  walk produce the same `desiredVel`. The split is purely "are you moving
  this frame."

---

## 6. Wall projection (hard constraint)

Soft pushes must not cross blocks. After summing `Δv = Σ_j Δv(i, j)` over
all neighbors:

```
v_intent_new = v_i + Δv

for each cardinal axis ax ∈ {+x, -x, +z, -z}:
    probe = p_i + ax_unit · (r_i + 0.1)
    if isPositionBlocked(probe):
        component = v_intent_new · ax_unit
        if component > 0:
            v_intent_new -= component · ax_unit   // zero-out into the wall
```

`isPositionBlocked` reads the same `LocalWorld` (client) or `World`
(server-side equivalent) used by `moveAndCollide`. Per Rule 6, no second
chunk store.

This is what enforces "two agents pushing each other into a wall" — the
side closest to the wall has its push-into-wall component zeroed, while
the far side still receives a full push *away* from the wall. Net force
in the pair is no longer balanced, so they separate along the wall
instead of through it.

---

## 7. Reactive hook for sleeping NPCs (optional, Phase B)

A purely-stationary NPC produces no `sendMove`, so even though `w` says
"the walker dodges around you," the NPC still occupies its tile. For
SC2-feel "shuffle out of the way," `AgentClient::tick` runs a cheap
neighbor scan even while no behavior is firing:

```
for each neighbor j with |v_j| > ε_v and τ(self, j) < τ_react:
    emit a one-shot Move with desiredVel = small_kick · n_τ
    mark this NPC physicsAwake (server-side wake fires on resolveMoveAction)
```

This is *not* AI — it's a reflex. No planning, no goal-text change. The
behavior tree's next `decide()` overrides whatever the reflex picked.

Skip implementing this until §1–6 are validated; group stability is
visible without it.

---

## 8. Final velocity output

Per agent, per frame:

```
Δv = Σ_j Δv_anticip(i, j) + Σ_j Δv_pen(i, j)
Δv = clamp_magnitude(Δv, 0.6 · walk_speed)        // never overpower intent

v_intent = v_i + Δv
v_intent = wall_project(p_i, v_intent)            // §6
v_out    = clamp_magnitude(v_intent, 1.5 · walk_speed)

submit MoveTo with desiredVel = v_out
```

The 0.6/1.5 caps stop a bad neighbor configuration from teleporting an
agent: `Δv` can't exceed 60% of intent magnitude, and the final velocity
can't exceed 150%. Both numbers are tuning, not invariants.

---

## 9. Tuning constants (initial)

```
τ_max         = 2.0 s        // look-ahead horizon
τ_min         = 0.1 s        // 1/τ singularity clip
τ_react       = 0.5 s        // §7 reflex threshold
k_anticip     = 4.0          // anticipatory gain
k_pen         = 8.0          // penetration spring
ε  (slack)    = 0.05 m       // padding on R
ε_v (idle)    = 0.05 m/s     // intent-vel threshold for the asymmetric table
ε_a (parallel)= 1e-4         // |v_rel|² floor
neighbor_R    = 8.0 m        // spatial-hash query radius
```

These are starting values. Group-stability and SC2-feel tuning happens
empirically with `make game TERMINATE_AFTER_S=300 --villagers N`.

---

## 10. Complexity

Naïve pair loop is O(n²). With a flat XZ spatial hash (cell = `2·neighbor_R`),
expected O(n) for typical densities. The hash is rebuilt per frame from
`forEachEntity`; no incremental maintenance.

---

## 11. Telemetry

Hooked into `perf_registry` like everything else:

```
client.steering.separation_pairs        // pairs evaluated this frame
client.steering.separation_emit         // pairs that produced nonzero Δv
client.steering.separation_blocked      // axis components zeroed by walls
client.steering.separation_force_max    // max |Δv| this frame, for spikes
```

If `separation_pairs` × frames-per-second blows past expectations, the
hash's cell size is wrong, not the algorithm.

---

## 12. References

- Karamouzas, Berseth, Guy. *Universal Power Law Governing Pedestrian
  Interactions* (PRL 2014). Source of the `1/τ − 1/τ_max` shape.
- Reynolds. *Steering Behaviors for Autonomous Characters* (1999). The
  canonical separation force we degrade to in §4.
- van den Berg, Lin, Manocha. *Reciprocal Velocity Obstacles* (ICRA
  2008). Where the `w = 0.5` half-share comes from when both agents move.
- Blizzard, *StarCraft II* unit pushing (GDC 2010 talk). Asymmetric
  moving-vs-stationary weights and the 1/τ ramp.

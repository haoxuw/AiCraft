# CreatureForge — Staged Build Plan

Each stage ends with one commit and one screenshot under
`docs/screenshots/stage_N.png`. If the screenshot doesn't match the
stage's visual goal, the stage isn't done.

## Design pillars (what we commit to beating Spore on)

| Spore limit | Our answer |
|---|---|
| Blob-only bodies | Hybrid implicit + hard-surface body |
| Cartoon scripted IK | Physics-driven PD-controller gait |
| 3-layer paint | Composable material-graph paint stack |
| Static palette | Hot-reloadable Part DataAssets |
| Flat-plane gait | Terrain-adaptive (climb/swim/fly) |
| Fixed part silhouettes | Per-instance taper/bend/twist/thickness |
| Mirror-or-nothing | Asymmetry slider [0,1] |
| Black-box stats | Morphology-inferred capabilities |
| No behavior | Node-graph behavior editor |
| Flat stage | Instant ecosystem drop w/ predator + food |

## Stages

### 0 — Schema + scaffolding (done)
- `MorphologyGraph.h` — spine, parts, paint, behavior.
- `CreaturePartAsset.h` — palette entries.
- `BodyCompiler.h` — incremental-recompile interface.
- `.uproject`, `Build.cs`, module bootstrap.

### 1 — Spine + implicit body mesh
Drag a `USplineComponent` in the editor; the body tube regenerates as
a `UDynamicMesh` via marched SDF. Target: <30 ms recompile on a
spine edit. Deliverable: screenshot of a deformable tube.

### 2 — Part placement (single part type: leg)
Drop a leg part onto the body; raymarch from spine outward, snap to
surface normal, re-weight skin. Mirror toggle works. Deliverable:
screenshot of a body with 4 legs attached.

### 3 — Auto-rig + static pose
Runtime `USkeletalMesh` emitted from spine samples + leg IK anchors.
Geodesic skin weights. Creature stands on ground.

### 4 — Physics-driven walk (flat ground)
Chaos articulated body + PD controllers. Phase-offset gait cycle
distributed by leg count. Test button → creature walks. This is the
hardest single stage; scope it to 4-leg only.

### 5 — Part library expansion
Mouth, eyes, tail, wing, spike parts. Palette enumerates parts via
Asset Registry. Deliverable: something that reads as a "creature."

### 6 — Material layer paint stack
Render-target masks + material-graph blending. Grooming brushes.
Procedural patterns that survive deformation.

### 7 — Capability inference + swim/fly modes
Post-compile pass sums wing area, fin area, leg reach. Locomotion
mode picked from morphology. Swim/fly use alternate Control Rigs.

### 8 — Asymmetry + editable part silhouettes
Per-instance deform handles; asymmetry slider mixes mirror↔free.

### 9 — Behavior graph editor
Small node editor (sensors → states → actions) compiling to
StateTree at test time.

### 10 — Ecosystem drop
One biome level per element. Spawn + 3 food + 1 predator. 15 s
recording is the shippable deliverable.

## Known risks (re-read before each stage)

- **Runtime skeletal-mesh construction** is finicky in UE 5.3.
  Start Stage 3 with a pooled fixed-topology skeleton (N_max bones)
  and variable skin weights; only build truly runtime skeletons
  once everything else is stable.
- **Physics gait divergence.** Start with target-pose PD control
  (deterministic, stable). Defer learned controllers.
- **Mesh rebuild hitches.** Incremental recompile is not optional —
  the hash-diff scaffolding in `BodyCompiler.cpp` is already in
  place for this reason.
- **Part schema changes.** Lock `UCreaturePartAsset` fields early;
  add, never rename. Schema drift is the #1 modder complaint.

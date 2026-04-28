# Far-Terrain LOD — Five Zoom Tiers

> **Status:** Design proposal, not yet implemented.
> **Goal:** Let the player zoom from voxel detail all the way to "blue marble"
> Earth view, without paying full chunk cost beyond the playable radius.

---

## Today

| Knob | File:Line | Value |
|---|---|---|
| Client render radius | `src/platform/client/game_vk.cpp:370` | 12 chunks (~192 blocks) |
| Server priority stream | `src/platform/server/client_manager.h:120` | `STREAM_R = 11` |
| Server far stream | `src/platform/server/client_manager.h:121` | `STREAM_FAR_R = 20` (~320 blocks) |
| Distance fog | `src/platform/client/game_vk_renderer_world.cpp:537` | `fogNear=140, fogFar=320` |

There is no LOD / imposter / heightmap-far system. Distant terrain is hidden
by fog. Hard horizon at ~320 blocks.

---

## The Five Tiers

| Tier | What it is | Source data | Renderer | Editable? |
|------|-----------|-------------|----------|-----------|
| **1** | Full voxel chunks (today) | existing `S_CHUNK` | existing chunk mesher | yes |
| **2** | Downsampled voxels (2× / 4× per axis) | server bakes on chunk save | same mesher, smaller meshes | no (rebake on dirty) |
| **3** | Building imposters | per-structure baked impostor | textured billboard or low-poly mesh | no (authored only) |
| **4** | Heightmap + biome color tiles | per-region summary | clipmap mesh (Distant-Horizons style) | no (rebake on dirty) |
| **5** | Earth-from-space | `src/python/voxel_earth/` real-world data | sphere with texture, separate scene | n/a |

**Tiers 1–2 share a renderer. Tier 5 is a different scene** (player avatar
sub-pixel, no tick simulation makes sense). So this is really
**3 rendering paths + 1 skybox**, not 5 separate engines.

---

## Per-Tier Radius / Bandwidth (target)

| Tier | Radius (chunks) | Per-unit cost | Total bandwidth target |
|------|-----------------|---------------|-----------------------|
| 1    | 0–12            | full chunk    | unchanged              |
| 2    | 12–32           | ~1/8 chunk    | ~current / 8           |
| 3    | structure-tagged within 64 | one impostor mesh | KB-class                |
| 4    | 32–256+         | ~200 B / chunk | KB-class               |
| 5    | global          | one Earth tex | one-time               |

Each tier owns its own message type and priority queue. Today's `S_CHUNK` is
Tier 1 only.

---

## Concerns, Ranked

### 1. Tier 3 has a hidden hard problem: "what is a building?"

Solarium worlds are emergent block piles. Two options:

- **(a) Authored structures only** — buildings registered in
  `artifacts/structures/` get pre-baked imposters. Clean, tractable,
  player-built castles get nothing. **Recommended.**
- **(b) Runtime block clustering** — find connected non-terrain block groups
  at runtime, mesh-decimate them. Expensive, error-prone, blinky. Avoid.

There is no third clean option.

### 2. Tier 2 is the weakest link

A 2×-downsampled voxel chunk is still a voxel chunk — you pay meshing + GPU
upload, just slightly less. The big wins are when you go **non-voxel**
(Tier 4). Skip Tier 2 in v1; only add it if the Tier 1↔Tier 4 transition
band is visibly bad.

### 3. Editability invalidation

Only Tier 1 is mutable. When a player breaks a block, Tier 2/3/4 summaries
covering that chunk go stale. Server needs a *"rebake on chunk dirty +
debounce"* path.

- Tier 4 (heightmap): cheap — re-extract column max + dominant block color.
- Tier 2 (downsampled voxels): full re-mesh of the downsampled chunk.
- Tier 3 (impostor): only invalidates if the structure itself was edited.

### 4. Bandwidth is bounded but the protocol grows

Each tier adds a message type, radius constant, and priority queue. Plan the
tier scheduler once, not ad-hoc per tier:

```
ChunkStreamScheduler
  enum Tier { FULL=0, DOWN=1, IMPOSTOR=2, HEIGHT=3 }
  per-tier { radius, prio, max_per_tick, dirty_set }
```

### 5. Tier 5 is a UX decision, not a rendering one

When zoomed to orbit:
- Does sim keep running at full speed? Pause? Fast-forward?
- Can the player click a region to drop back in (RTS-style map mode)?
- Does **V** cycle into it, or is it a separate key (e.g. **M** for map)?

Without an answer, Tier 5 is a screensaver. With one, it's a 5th camera mode
and probably the highest-leverage new gameplay surface in the proposal.

### 6. Real-world coordinate sanity for Tier 5

`src/python/voxel_earth/` already pulls real Earth data. The space view
should show the actual globe with the played region highlighted. Verify
lat/lon bounds are tracked end-to-end; otherwise zooming out from "Toronto"
shows a generic sphere.

---

## Suggested Ship Order

1. **Tier 4 first** — heightmap clipmap, Distant-Horizons-style. Biggest
   visible win, smallest new infrastructure, no "what is a building"
   question. Gets you ~1000+ block visibility immediately.
2. **Tier 5 as a camera mode** — presentation feature. No tick changes,
   just render-mode swap that draws an Earth sphere. Cheap to prototype,
   answers UX questions before they become coupling.
3. **Tier 3 (authored-structure imposters only)** — after a handful of
   hand-authored structures exist that are worth seeing from far away.
4. **Tier 2** — only if Tier 1↔Tier 4 transition is visibly bad.

---

## Reference Implementations Worth Studying

- **Distant Horizons** (Minecraft mod, 2021+) — heightmap LOD streamed
  separately from chunks, fades into fog at ~32–512 chunks.
  Repo: `gitlab.com/jeseibel/distant-horizons`.
- **Veloren** — open-source voxel MMORPG; per-chunk
  `(height, avg_color, sun_shadow_azimuth)` summaries; cleanest
  architecture-level reference for a server-authoritative voxel game.
- **Vintage Story** Farseer / ChunkLOD mods — same recipe shipped in
  another voxel game.
- **GPU Geometry Clipmaps** (Hoppe, NVIDIA GPU Gems 2 ch. 2) — the
  standard reference for nested-ring heightmap LOD; how the Tier 4
  client-side renderer should be structured.
- **Google Earth** — the canonical "space → street" continuous zoom;
  see Appendix A.

---

## Appendix A — How Google Earth Is Built (and What Maps to Our Tiers)

Google Earth is the canonical reference for continuous space-to-street
zoom. The architecture has been remarkably stable since Keyhole's
EarthViewer (2001) — the data formats are now open standards, and most
of it ports directly onto our 5-tier plan.

### Core architecture (one paragraph)

A **quadtree tile pyramid** addressed by `(level, x, y)`. Level 0 = one
tile covering the globe; each level subdivides 2×2, so level N has 4^N
tiles. Effective depth ~28–30 (≈1 cm ground resolution). **Imagery**
(orthophotos, JPEG/WebP, 256×256) and **terrain** (quantized-mesh TIN
with adaptive vertex density) stream as **separate** tile layers off the
same quadtree. **3D buildings** (post-2012, photogrammetric meshes) are
delivered as **3D Tiles** (OGC standard, glTF 2.0 inside `B3DM`
containers) with their own quadtree refinement (REPLACE / ADD). Above
~8,000 km altitude the renderer **swaps projection**: it stops using the
flat Mercator quadtree and renders a textured WGS84 ellipsoid with the
lowest-LOD imagery as diffuse. Atmosphere (Rayleigh + Mie) is part of
the main pass, not a post-process; it switches between in-space and
in-atmosphere paths at the camera-altitude boundary.

### Mapping onto our tiers

| Our tier | Google Earth analogue | What to copy |
|---|---|---|
| **Tier 1** (full voxels) | high-zoom 3D Tiles (`B3DM` glTF) | tile addressing only — voxel meshes don't need glTF |
| **Tier 2** (downsampled voxels) | mid-zoom 3D Tiles with REPLACE refinement | child-replaces-parent semantics |
| **Tier 3** (building imposters) | low-zoom 3D Tiles | exactly the use case `B3DM` was designed for; consider literally adopting |
| **Tier 4** (heightmap + biome) | quantized-mesh terrain layer | format is open, well-specified, supported by Cesium |
| **Tier 5** (Earth from space) | projection swap to WGS84 ellipsoid | separate scene above altitude threshold; lowest-LOD tile as diffuse |

### Things worth copying

1. **Quadtree addressing** — even though our underlying storage is
   3D-chunked (octree-shaped), the **renderer** should address tiles by
   `(level, x, z)` for the surface representation in Tiers 3–4. This
   gives O(1) lookup and trivial frustum culling.

2. **Imagery and terrain are separate streams.** Validates our
   "Tier 4 + Tier 3 are independent message types" decision. GE proves
   this scales to planet level.

3. **Quantized-mesh format** (Cesium / open spec at
   `github.com/CesiumGS/quantized-mesh`) — TIN with vertex deltas in
   16-bit quantized integers, 88-byte header carrying bbox + min/max
   elevation. Either adopt directly for Tier 4 or take it as the
   reference for our own.

4. **3D Tiles refinement modes** —
   - `REPLACE` = child tiles replace parent on load (Tier 1 ↔ Tier 2 ↔ Tier 4)
   - `ADD` = children augment parent (e.g. instanced trees over terrain)
   We need both. Naming the modes explicitly will save design churn.

5. **Camera-trajectory prefetch.** Today `STREAM_R` is pure radius. GE
   prefetches along the camera path, with priority = (distance to camera,
   tile size, frustum margin). Worth a follow-up after Tier 4 ships.

6. **Projection swap above altitude threshold.** Confirms our intuition
   that Tier 5 is a *different scene*, not yet-another-LOD-step. Below
   threshold: clipmap heightmap. Above: textured ellipsoid + atmosphere.
   The swap is visual-only — no geometric discontinuity.

7. **Atmosphere is in the main pass.** When we build Tier 5 polish,
   don't reach for a post-process pass; do scattering inline so the
   in-atmosphere → in-space transition is one shader branch.

### Things that don't transfer

1. **GE is 2.5D heightfield** for terrain; we're volumetric. Tier 1/2
   must be octree / SVO indexed, not quadtree. Quadtree only applies to
   Tiers 3–5 where we've collapsed to a surface.

2. **GE is static.** Their tiles are pre-baked once globally. Our tiles
   are **server-baked on chunk dirty + debounce**. The bake pipeline is
   ours to invent; nothing in GE helps.

3. **GE renders pre-decimated meshes.** They never simplify on the
   client. We should follow this — bake LODs server-side, not at runtime.

4. **GE serves from CDN.** We serve from the authoritative game server.
   Tile cache is per-client, not shared. Reasonable working-set size
   for a 256-radius Tier 4 cone is small (KB, not MB).

### Concrete formats to evaluate

- **Quantized-mesh** — `github.com/CesiumGS/quantized-mesh` —
  open spec, exactly the right shape for Tier 4. ~tens of bytes per
  vertex.
- **3D Tiles 1.1** — `github.com/CesiumGS/3d-tiles` — OGC community
  standard. Tileset JSON describes the refinement tree; content is glTF.
  Adopting it for Tier 3 buys us free tooling (Cesium viewers, format
  validators).
- **glTF 2.0** — universal mesh transport. Probably overkill for Tier 1
  voxel meshes (we already have a custom format), but a sensible choice
  for Tier 3 imposters if we want them inspectable in standard tools.

### Bottom line

The 5-tier plan **is essentially Google Earth, restricted to a single
voxel game world.** The big architectural decisions (quadtree
addressing, separate imagery/terrain layers, projection swap to
ellipsoid above threshold, REPLACE vs ADD refinement) are all already
solved in open standards. The voxel-specific work is:

- Server-side bake-on-dirty pipeline (no GE analogue).
- Volumetric Tier 1/2 storage (octree / SVO, not quadtree).
- Custom mesh format for Tier 1 voxels (we already have it).

Everything else — Tiers 3, 4, 5, the protocol design, the projection
swap, the atmosphere shader — has a battle-tested open-source path.

---

## Cross-References

- `docs/solarium_legacy/100_CORE_GAMEPLAY_FEATURE.md` — current camera
  modes (V cycles FPS / TPS / RPG / RTS); Tier 5 would be the 5th mode.
- `src/platform/client/game_vk.cpp:370` — `kRenderChunkRadius`.
- `src/platform/server/client_manager.h:120` — `STREAM_R` / `STREAM_FAR_R`.
- `src/platform/client/game_vk_renderer_world.cpp:537` — fog config.
- `src/python/voxel_earth/` — real Earth data source feeding Tier 5.

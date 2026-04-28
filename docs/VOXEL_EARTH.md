# Voxel Earth

Pipeline that turns Google Photorealistic 3D Tiles (real-world textured
meshes) into Solarium voxel regions you can walk around in.

## Layers

```
Google 3D Tiles API ──► ~/.voxel/google/glb/<sha1>.glb
                                  │
                          solarium-voxel-bake
                                  │
                                  ▼
                  ~/.voxel/regions/<name>/blocks.bin   (VEAR — RGBA per voxel)
                                  │
            ConfigurableWorldTemplate (terrain.type = "voxel_earth")
                                  │
                                  ▼
                       Chunks generated on demand
                       (RGB → BlockId via palette.h)
```

Two cached layers, both keyed deterministically:

| Layer       | Path                                       | Key            | Cost       |
|-------------|--------------------------------------------|----------------|------------|
| Tile GLBs   | `~/.voxel/google/glb/<sha1>.glb`           | OBB sha1       | Google API |
| Region file | `~/.voxel/regions/<name>/blocks.bin`       | Region name    | local CPU  |

Re-running `download` is free if the OBB cache is warm.
Re-running `voxel-bake` is purely local — no network calls.

## Rebuilding without Google calls

Most workflow changes (palette tweaks, height-aware classification, voxel
size, smoothing) **don't need** to re-download or even re-bake. Classification
runs at chunk-load time, reading raw RGBA from `blocks.bin`. Just rebuild
C++ and reconnect.

Re-bake only when you've changed:
  * voxel size (`--voxel-size`)
  * voxelizer logic (surface extraction, draco decode, etc.)
  * the GLB set on disk (e.g. larger download radius)

Re-download only when you've changed location, radius, or height — and
the new search volume reaches OBBs we haven't pulled before.

## Palette

`src/platform/server/voxel_earth/palette.h` is the single source of truth
for RGB → BlockId mapping. It's split into two tiers:

* **`kBuildingPalette`** — allowed at any height. Materials a building
  can plausibly be made of: stone variants (Stone / Granite / Sandstone /
  Marble / Cobblestone), Glass, Wood, Log, Leaves.

* **`kGroundPalette`** — only candidates within `kGroundHeightBlocks` of
  the region floor. Natural materials: Dirt, Grass, Sand, Water, Snow.

This stops the classic "sand block in mid-air" failure mode: a beige
office facade is closest to (220, 200, 160) which used to map to Sand.
Now it maps to Sandstone, while a literal beige voxel at y=ground+1 can
still become Sand.

To tune:

1. Edit RGB constants in `kBuildingPalette` / `kGroundPalette`.
2. Adjust `kGroundHeightBlocks` if the ground band needs to be taller
   (currently 3 blocks → about 3 metres at the default 1m voxel).
3. Rebuild C++. Reconnect to the world. **No re-bake needed.**

The classifier is a plain Euclidean nearest-neighbour in linear RGB. If
the city looks too uniform, spread the entries; if it looks too noisy,
pull entries closer in colour.

## Caches and where they live

```
~/.voxel/
  api_key                 single-line file (set via `python -m voxel_earth set-key <KEY>`)
  google/
    glb/<sha1>.glb         tile mesh; sha1 of OBB → deterministic, dedups across sessions
    session.json           session token (3-hour TTL)
  discover/
    <lat>_<lng>_xz<R>_h<H>.json   BFS result for one search volume
  geocode/<query>.json     text → lat,lng
  elevation/<lat>_<lng>.json
  regions/<name>/blocks.bin   baked VEAR region
```

`<sha1>` is computed from the OBB rounded to 1mm, so it's stable across
session tokens — re-running the BFS won't redownload tiles already on
disk.

## Toronto demo

```
python -m voxel_earth set-key <YOUR_GOOGLE_KEY>
python -m voxel_earth download --location "Toronto" --radius 800 --height 1200
solarium-voxel-bake --glb-dir ~/.voxel/google/glb \
                    --out     ~/.voxel/regions/toronto/blocks.bin \
                    --voxel-size 1.0
make toronto
```

* `--radius 800` reaches Lake Ontario shore from the CN Tower; smaller
  radii give a tighter slab.
* `--height 1200` is enough for the CN Tower spire (553 m) plus margin.
* The bake uses the ECEF origin from the first GLB; later runs reuse it
  by passing `--origin` if you ever want a stable region across re-bakes.

## Code map

```
src/python/voxel_earth/         CLI: init, set-key, geocode, download
src/platform/server/voxel_earth/
  glb_loader.{h,cpp}           Assimp GLB loader (Draco-aware)
  rotate.{h,cpp}               ECEF → ENU quaternion rotate
  texture.{h,cpp}              JPG/PNG decode + bilinear sample (stb_image)
  voxelizer.{h,cpp}            2.5D dominant-axis scan converter
  region.{h,cpp}               VEAR file format + spatial index
  palette.h                    RGB → BlockId classifier (this doc)
  voxel_bake.cpp               solarium-voxel-bake CLI
  voxel_smoke.cpp              solarium-voxel-smoke (one-tile sanity)
src/artifacts/worlds/base/toronto.py    Toronto world template (--template 6)
```

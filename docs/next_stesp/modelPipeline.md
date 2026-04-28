# Model Pipeline (image → mesh → voxel → rigged entity) — Progress & Next Steps

End-to-end pipeline that turns an uploaded image into a fully animated in-game
entity. Covers an InstantMesh sidecar (Apache 2.0), a rig-template engine, and
the eventual voxelize bridge + in-game editor. Full design lives at
`docs/MODEL_PIPELINE.md`; this doc is the living handoff for what's shipped vs
what's next.

Branch: `feat/zone-aware-world`
Latest commits this stack:
```
fbce71a client: rig pose review fixes — alloc, ordering, dup, dead check
ba568e8 client: bone-driven flatten path (model pipeline P1, scaffolding)
e9a0692 client: rig templates + image-to-3D sidecar (model pipeline P0+P1)
```

---

## What's landed

### Phase 0 — InstantMesh sidecar

- **Make targets**: `imageto3d_setup` / `imageto3d_smoke` / `imageto3d_stop` /
  `imageto3d_clean`. Slotted between `tts_*` and `ai_setup` in the Makefile to
  match existing sidecar layout. Idempotent — re-running setup skips clone /
  venv / pip install if already present.
- **Setup is defensive**:
  - Detects venv-in-venv bug (`bin/activate` missing because `python3 -m venv`
    inherits from an active parent venv) and rebuilds; unsets `VIRTUAL_ENV` /
    `PYTHONHOME` for the subshell to dodge the Python stdlib quirk.
  - Pre-flight checks for `nvcc` and `/usr/bin/g++-12` with clear "install
    with: sudo apt install …" error messages.
  - Defaults to **cu126 PyTorch wheel** — keeps Pascal `sm_61` support that
    cu128/cu130 dropped. Override via `INSTANTMESH_TORCH_INDEX`.
  - Freezes torch versions to `.torch_constraints.txt` after install, then
    runs `pip install -c …` for `requirements.txt` so transitive deps (e.g.
    `accelerate`, `bitsandbytes`) don't upgrade torch behind our back.
  - Pins `accelerate==0.24.0` (Oct 2023, contemporary with InstantMesh's
    `transformers==4.34.1` which forces hub 0.17 and breaks newer accelerate).
  - Installs `onnxruntime` explicitly (rembg's runtime backend, omitted from
    InstantMesh's requirements.txt).
  - Compiles `nvdiffrast` with `CUDAHOSTCXX=/usr/bin/g++-12` (CUDA 12.0's nvcc
    rejects g++ ≥ 13; Ubuntu 24.04 ships g++-13 as default).
- **Smoke target**:
  - Defaults to `instant-nerf-base` config — `instant-mesh-large` wants 15 GiB
    at FlexiCubes (>1080 Ti), `instant-mesh-base` peaks ~11 GiB (too tight),
    `instant-nerf-base` fits ~10 GiB.
  - Sets `PYTORCH_CUDA_ALLOC_CONF=expandable_segments:True` to reduce
    fragmentation.
  - `--export_texmap` disabled by default (adds 2-3 GiB peak; voxelizer
    averages texels into voxels anyway). Opt in via `INSTANTMESH_TEXMAP=1`.
  - Success detection uses `find` (bash `**/*.glb` doesn't glob without
    `globstar`); accepts `.glb` / `.obj` / `.ply`.
- **Verified end-to-end on GTX 1080 Ti**:
  `examples/hatsune_miku.png` → `/tmp/imageto3d/smoke/instant-nerf-base/meshes/hatsune_miku.obj`
  (23 MB, 206K vertices / 412K triangles, vertex colors baked, ~13 min
  inference one-time per input).

### Phase 1 — engine data layer

Additive — existing 16 creatures unchanged because their models don't set the
new fields.

| File | Role |
|------|------|
| `client/rig.h` (new) | `Bone`, `KeyframeRot`, `KeyframeChannel`, `KeyframeClip`, `Rig` types. Helpers: `evalKeyframeChannel` (lerp + clean loop close), `findBone` (linear scan name → idx), `computeRigPose` (single forward pass with `nameToIdx` map; warns once per (rigId, bone) on forward / unresolved parent refs; output by reference for thread-local reuse). |
| `client/rig_loader.h` (new) | `loadRigFile(path, Rig&)` reuses `model_loader::Parser` + `dictGet` / `toVec3` helpers. Looks for `template = {…}` instead of `model = {…}`; treats `parent: None` as empty string. |
| `client/rig_registry.h` (new) | `loadAll(<root>)` walks `<root>/rigs/<ns>/*.py`. `find("base:humanoid") → const Rig*`. Owns the rig instances; pointers valid until next `loadAll`. |
| `client/rig_smoke.cpp` (new) + `solarium-rig-smoke` target | Loads humanoid.py, dumps bones + clips, spot-checks lerp eval. |
| `client/box_model.h` | Added `BodyPart::bone` (string, empty = no bone) and `BoxModel::rigId` (string, empty = no rig). |
| `client/model_loader.h` | Parses the new `rig:` (model-level) and `bone:` (per-part) fields. |
| `client/box_model_flatten.h` | `appendBoxModel` takes optional `const Rig*`. When non-null AND a part has `bone:` set, the part follows the bone's accumulated parent-chain transform via `computeRigPose`. Hand-frame capture + head-tracking + emit run as a shared post-transform block (gated by `if (!boneDriven) { …legacy block… }`). One-shot warning on bone-name typos before falling back. |
| `artifacts/rigs/base/humanoid.py` (new) | First template: 16 bones (root + pelvis + torso + head + 2 arm chains + 2 leg chains), 9 stock clips (`idle, walk, run, attack_swing, attack_thrust, wave, sit, sleep, hurt`). First-pass values; refine after editor lands. |

### Tier-1 cleanup that rode along

- Removed unused `import math` from 14 model artifacts (none of them used
  `math.*`; literal `3.1416` instead).
- Promoted brass / fill colors to `ui_kit::color::{kBrassDeep, kBrassMid,
  kBrassHi, kPanelFill}`. Renderers in `panel`, `entity_ui`, `inventory`,
  `menu`, `hud` now use `const auto& brass = ui::color::…` aliases — call
  sites unchanged, source of truth single.
- Deleted empty `docs/99_todo.md`.

### Review-driven fixes (`fbce71a`)

Three parallel review agents (reuse / quality / efficiency) flagged six items
worth fixing this turn — all addressed:

| Fix | Where |
|---|---|
| Heap alloc per render call → `computeRigPose` writes into `vector<mat4>&`; caller parks a `thread_local` pose | `rig.h`, `box_model_flatten.h` |
| O(N²) parent-chain scan → one `nameToIdx` pass per call | `rig.h` |
| Silent forward-parent / unresolved-parent fallback → `[rig] %s: bone '%s' has %s parent '%s' — treated as root` warning, dedup'd per (rigId, bone) | `rig.h` |
| Bone-name typo silent fallback → same dedup'd warning before legacy fallback | `box_model_flatten.h` |
| Dead `boneIdx < rigPose.size()` check → removed | `box_model_flatten.h` |
| Hand-capture + head-tracking duplicated across both branches → restructured: `if (!boneDriven) { legacy block }` + shared post-transform | `box_model_flatten.h` |

Tier-B items (deferred — see Next steps below):
- `parseNamedDict` shared between `model_loader` / `rig_loader`
- Cache `const Rig*` on `BoxModel` at load time (resolve rigId once)
- Replace `BodyPart::bone` string with bone idx (bigger restructure; bundle
  with the cache above)
- `screen_shell.h` / `dialog_panel.h` still embed local brass arrays —
  separate cleanup pass
- `RigRegistry` discovery could share with `ArtifactRegistry::discoverNamespaces`

---

## Next steps

### Phase 1 finish — wire it up so guy walks via the rig (~3 days)

1. **Renderer integration** — resolve `model.rigId → const Rig*` once at model
   load time (or first-render lazy cache on `BoxModel`), pass through to
   `appendBoxModel`. Today the bone branch in flatten is dormant because no
   call site passes a `rig` pointer. Touchpoints: probably
   `client/game_vk_renderer_world.cpp` and wherever `MODEL_LOADER::loadModelFile`
   is called.
2. **Convert `artifacts/models/base/guy.py`** to use the humanoid rig: add
   `rig: "base:humanoid"` at the model level and `bone: "head" / "torso" /
   "l_shoulder" / "r_shoulder" / "l_leg_upper" / "r_leg_upper"` per part. The
   part `offset:` becomes bone-local — subtract the bone's accumulated default
   position to convert.
3. **Visual verification** via the `refine-model-and-animation` skill:
   capture FPS / TPS / RPG / RTS angles + walk + attack + idle clips. Diff
   against pre-rig screenshots. **This is the proof point — the bone path
   activates only here.**
4. **Iterate humanoid clips** based on what looks wrong. The first-pass values
   in `humanoid.py` are starter quality — expect to refine angles, durations,
   easing.

### Phase 2 — voxelize bridge (~2 days)

Lets us spawn the InstantMesh OBJ as a static entity in-world, no rig yet.

1. Reuse `server/voxel_earth/voxelizer.cpp` + `glb_loader.cpp`. Today they're
   wired for `Glb` from Photorealistic 3D Tiles (with Draco decode); InstantMesh
   produces vanilla OBJ. Two options:
   - Add OBJ support to the existing loader (Assimp already loads OBJ — should
     be a small flag flip in `glb_loader.cpp`).
   - Convert OBJ → GLB via Assimp's exporter as a one-shot in the smoke
     target.
2. Write a small CLI `solarium-mesh-to-model <input.obj> <output.py> [--res 32]`
   that voxelizes and emits a Solarium box-list `.py` model file. Reuses
   `voxelize()` from `voxel_earth/voxelizer.h`; box-list emission is new.
3. Drop the result at `artifacts/models/base/<id>.py` and verify hot-reload
   shows the voxelized Miku as a placeable entity.

### Phase 3 — Web UI shell (~3 days)

Standalone Vite + TS + Three.js app rendered as a CEF route
(`civ://ui/model_studio/`). See `docs/next_stesp/webUI.md` for the existing
CEF UI conventions.

1. Set up `src/web_apps/model_studio/` (peer to legacy `src/model_editor/`):
   `package.json`, `vite.config.ts`, `tsconfig.json`, two-tab shell (Editor
   / ImageTo3D).
2. POST_BUILD step in `CMakeLists.txt` copies `build/ui/model_studio/` next
   to other CEF assets.
3. Image-upload tab → `cefQuery({request: "imageto3d.generate", body: …})`
   wraps the sidecar; preview pane shows the GLB in Three.js.

### Phase 4-6 — editor + rig binding UI + spawn (~14 days)

Per `docs/MODEL_PIPELINE.md` — drag bones to mesh, paint box→bone binding,
scrub clip preview, save back to `.py`, hot-reload as Living artifact.

### Phase 7 — more rig templates (~20 days)

`quadruped` (~12 bones), `bird` (~10), `fish` (~6), `slime` (~3). Each needs
~8 stock clips. Long pole. Deferred until phase 1-6 prove out.

---

## Known issues / open questions

| | Status |
|---|---|
| **`src/platform/client/path_executor.cpp` does not compile** on this branch (`Unit::openedDoors` not declared, `reached` not in scope) | Pre-existing in-flight work outside model pipeline. Blocks the full `solarium-ui-vk` link. My header changes compile in isolation across all three flatten consumers. Needs separate fix. |
| **InstantMesh inference: ~13 min on 1080 Ti** | Acceptable for "create then refine" workflow; sub-30s on RTX 30/40 series. Not a perf bug. |
| **`instant-mesh-large` gives best visual quality** but OOMs at 15 GiB on 11-GiB cards | Default to `nerf-base` for ≤ 11 GiB GPUs; allow `INSTANTMESH_CONFIG=instant-mesh-large` override on bigger hardware. |
| **`accelerate==0.24.0` pin is brittle** | Newer accelerate breaks against InstantMesh's old `transformers` / `huggingface-hub` pins. If we ever upgrade transformers, revisit accelerate. Documented inline in Makefile and `MODEL_PIPELINE.md` failure-modes table. |
| **Voxelize-by-default loses photoreal texture** | Intentional — Solarium's identity is blocky. Per-vertex colors survive voxelization fine. Modders who want raw mesh rendering would need a new Vulkan path (out of scope). |
| **No auto-rigging in 2026 OSS** for arbitrary meshes | Designed-around: manual binding in editor (drag bones to keypoints + paint box-to-bone) is the long-term plan. Templates (humanoid first) constrain the problem. |
| **Hand-frame capture for held items still hardcodes `name == "right_hand"` / `"left_hand"`** | Works for legacy parts; for bone-driven parts the same names must appear on the bone-bound box. When `guy.py` gets converted, the existing `right_hand` / `left_hand` part names stay, only `bone:` is added. |
| **Rig template for `humanoid` has 16 bones (not 15)** as the design doc says | Doc says "~15", code has 16 (root + pelvis is the extra split). Update doc when we author the next template. |
| **Tier-B simplify items** | Listed in "Review-driven fixes" above. Worth one focused commit during the renderer integration pass. |

---

## How to pick up

If you've never touched this stack:

1. **Read** `docs/MODEL_PIPELINE.md` for the architecture, then this doc.
2. **Build** the rig smoke: `cmake --build build -j1 --target solarium-rig-smoke`.
   Run `./build/solarium-rig-smoke` — should print 16 bones + 9 clips + lerp
   sample.
3. **Try the sidecar** if you have a CUDA GPU: `make imageto3d_setup` (~5 min
   download), then `make imageto3d_smoke` (~13 min on Pascal, faster on newer
   hardware). Output lands at `/tmp/imageto3d/smoke/instant-nerf-base/meshes/`.
4. **Pick a "Next steps" item** above. The Phase 1 finish (renderer wiring +
   guy.py binding) is the smallest; Phase 2 (voxelize bridge) gives the
   biggest visible payoff.

# Model Pipeline — Image → Mesh → Voxel → Rigged Entity

Status: **draft, MVP (Phase 0) landing**
Owner: in-tree (`src/web_apps/model_studio/`, `src/platform/server/voxel_earth/`, `llm/imageto3d/`)
Date: 2026-04-27

End-to-end pipeline that turns a user-uploaded image into a fully animated
in-game entity, with no closed-source dependencies.

```
photo.jpg ──► InstantMesh sidecar ──► textured.glb ──► voxelizer ──► box-list model.py
                                                                            │
                                                              user picks rig template
                                                              drags bones to mesh
                                                              paints box→bone binding
                                                                            │
                                                                            ▼
                                                              animated entity in world
```

## Why this exists

Current model authoring is `src/model_editor/modelcrafter.py` — matplotlib,
headless snapshots, manual Python dict editing. Workflow gaps:

1. **No on-ramp for non-engineers** — every model is hand-written Python.
2. **Animation is sin-driven only** — `attack_anim.h` has hardcoded keyframes
   in C++ that drift from `attack_clips.py` (already flagged).
3. **No geometry source other than human imagination** — modders can't import
   anything, can't sculpt, can't generate.

Image-to-3D AI (InstantMesh, Apr 2024) closes all three: photos in,
voxelized models out, dropped onto a fixed rig template with stock clips that
already work.

## Mandatory invariants (do not break)

* **CEF route, not standalone web app.** UI is `civ://ui/model_studio/`.
  Reuses the CEF infrastructure from `docs/CEF_UI.md`.
* **Voxelize-by-default.** Solarium's identity is blocky. Raw triangle-mesh
  rendering stays out of scope. AI-generated meshes get voxelized via the
  existing `voxel_earth/voxelizer.cpp`.
* **Rig templates are artifacts.** They live in `src/artifacts/rigs/<ns>/`
  and follow the same hot-load rules as everything else. Modders can add new
  templates by dropping a `.py` file (Rule 1: "Python is the game").
* **Existing models keep working unchanged.** The rig+keyframe system is
  additive — a model without a `rig:` field uses the old sin-driver path.
* **Sidecar pattern.** Image-to-3D is a fourth sidecar under `llm/`,
  matching `llama.cpp` / `whisper.cpp` / `piper`. Per-request subprocess
  (no long-lived FastAPI) since 8 GB VRAM is too much to pin.

## Architecture

### Process layout

```
solarium-ui-vk (browser process)
├── CEF render thread → civ://ui/model_studio/  (Vite-built TS app)
├── On image upload: cefQuery("imageto3d.generate", {image_b64})
│   └── browser process spawns:
│       solarium-imageto3d <input.png> <output.glb>
│         └── python3 llm/imageto3d/run_solarium.py
│             └── InstantMesh inference (CUDA, ~10 s on 4090)
│             └── writes textured.glb
├── On voxelize: cefQuery("imageto3d.voxelize", {glb_path, resolution})
│   └── reuses src/platform/server/voxel_earth/voxelizer.cpp in-process
└── On save: cefQuery("model.save", {ns, id, model_dict})
    └── writes src/artifacts/models/<ns>/<id>.py (tokenizer-friendly Python)
```

### Data flow

```
[ image (PNG) ]
     │ ~10 s, GPU
     ▼
[ textured GLB ] ─── debug-saved at /tmp/imageto3d/<sha1>.glb
     │ ~1 s, CPU
     ▼
[ voxel grid (32³ default) ]   ← reuses voxel_earth voxelizer
     │ ~50 ms
     ▼
[ box-list model dict ]
   { id, parts: [{offset,size,color}, ...] }
     │
     ▼
[ user picks rig template + binds boxes→bones in editor ]
     │
     ▼
[ model.py with rig + bone_map fields ]
     │ artifact hot-reload
     ▼
[ Living artifact references model id → spawned entity animates ]
```

## Data model

### Rig template (`src/artifacts/rigs/<ns>/<id>.py`)

```python
template = {
    "id": "humanoid",
    "bones": [
        {"name": "root",        "parent": None,   "default_pos": [0, 0, 0]},
        {"name": "torso",       "parent": "root", "default_pos": [0, 1.0, 0]},
        {"name": "head",        "parent": "torso","default_pos": [0, 0.9, 0]},
        {"name": "l_arm_upper", "parent": "torso","default_pos": [-0.5, 0.7, 0]},
        {"name": "l_arm_lower", "parent": "l_arm_upper", "default_pos": [0, -0.6, 0]},
        {"name": "l_hand",      "parent": "l_arm_lower", "default_pos": [0, -0.5, 0]},
        # … r_arm_upper/lower/hand, l_leg_upper/lower/foot, r_leg_*
    ],
    "clips": {
        "idle":   {"duration": 2.0, "channels": [...]},
        "walk":   {"duration": 1.0, "channels": [...]},
        "run":    {"duration": 0.7, "channels": [...]},
        "attack_swing":  {"duration": 0.4, "channels": [...]},
        "attack_thrust": {"duration": 0.5, "channels": [...]},
        "wave":   {"duration": 1.2, "channels": [...]},
        "sit":    {"duration": 0.5, "channels": [...]},
        "sleep":  {"duration": 4.0, "channels": [...]},
        "hurt":   {"duration": 0.3, "channels": [...]},
    },
}
```

A clip channel is keyframe rotation per bone:

```python
{"bone": "l_leg_upper",
 "axis": [1, 0, 0],
 "keys": [{"t": 0.0, "deg": 30},
          {"t": 0.5, "deg": -30},
          {"t": 1.0, "deg": 30}]}
```

### Model with rig binding (`src/artifacts/models/<ns>/<id>.py`)

```python
model = {
    "id": "guy_v2",
    "rig": "base:humanoid",            # ← NEW: references template
    "parts": [
        {"name": "head_box",  "offset": [...], "size": [...], "color": [...],
         "bone": "head"},               # ← NEW: which bone this box follows
        {"name": "torso_box", "offset": [...], "size": [...], "color": [...],
         "bone": "torso"},
        # …
    ],
    # No clips here — they live in the rig template, shared across all
    # creatures bound to the same template. Per-creature overrides allowed
    # via "clip_overrides": {"walk": {...}}.
}
```

Backward-compat: if `rig:` is absent, the existing sin-driver code path runs
exactly as before. Existing 16 living creatures unchanged.

### Template library to ship

| Template | Bones | Stock clips | Covers |
|----------|-------|-------------|--------|
| `humanoid`  | ~15 | idle, walk, run, attack-swing, attack-thrust, wave, sit, sleep, hurt | guy, mage, villager, skeleton, giant, custom NPCs |
| `quadruped` | ~12 | idle, walk, run, eat, sit, sleep, hurt, attack-bite | dog, cat, beaver, raccoon, pig |
| `bird`      | ~10 | idle, walk, fly-hover, fly-flap, peck, sleep, hurt | chicken, owl |
| `fish`      | ~6  | idle-hover, swim, dart, hurt | new aquatic |
| `slime`     | ~3  | idle-jiggle, hop, attack-pounce, hurt | new amorphous |

Modders ship more by dropping `.py` files in `rigs/<their_ns>/`.

## Engine changes

All additive. No file is rewritten; all touched files keep their existing
behavior when the new fields are absent.

| File | Change | Effort |
|------|--------|--------|
| `logic/box_model.h` | Add optional `bone:` field on parts; add `Rig` struct | 1 d |
| `client/box_model_flatten.h` | Compose bone parent chain into part transform | 1 d |
| `client/model_loader.h` | Parse `rig:` and `bone:` fields (tokenizer already supports any key) | 0.5 d |
| `logic/rig_template.h` (new) | Rig + clip data types | 0.5 d |
| `logic/rig_loader.h` (new) | Load `src/artifacts/rigs/<ns>/<id>.py` (reuses model_loader tokenizer) | 1 d |
| `client/clip_eval.h` (new) | Lerp keyframe rotations per channel; replaces `attack_anim.h` | 2 d |
| `python_bridge.cpp` | Surface rig id from Living artifact to client | 0.5 d |

**Total: ~6 days**.

The new keyframe `clip_eval.h` also subsumes the hand-written keyframe data in
`src/platform/client/attack_anim.h` (and its drift-prone Python mirror in
`src/model_editor/attack_clips.py`) — closes the drift risk we already
flagged.

## Sidecar — `imageto3d`

Fourth sibling under `llm/`, following the existing pattern:

```
llm/
├── llama.cpp/        ← cloned, builds llama-server (LLM)
├── whisper.cpp/      ← cloned, builds whisper-server (STT)
├── piper/            ← prebuilt binary (TTS)
└── imageto3d/        ← NEW: cloned TencentARC/InstantMesh
    ├── (InstantMesh repo contents)
    ├── weights/      ← ~5 GB HF download cache
    ├── venv/         ← isolated Python env (PyTorch + xformers pinned)
    └── run_solarium.py  ← thin CLI wrapper we write
```

### Why per-request subprocess, not long-lived server

* InstantMesh holds **8 GB of VRAM** when loaded. llama-server already holds
  4–7 GB. On a 16 GB card this fits but leaves no headroom.
* Image-to-3D is **occasional** (user uploads a picture once, iterates on
  the result for minutes). Keeping the model warm is wasted memory.
* Spawning a Python process and loading the model takes ~5 s on top of ~10 s
  inference — acceptable for a workflow that already takes minutes total.

### Make targets

```
make imageto3d_setup    # clone, venv, pin torch/xformers/CUDA, download weights
make imageto3d_smoke    # run on llm/imageto3d/examples/hatsune_miku.png
                        # writes /tmp/imageto3d/smoke.glb
make imageto3d_clean    # rm -rf llm/imageto3d/{venv,weights}
```

No `imageto3d_server` target — there is no daemon. The game spawns
`solarium-imageto3d <in.png> <out.glb>` per request.

### Hardware requirements

* CUDA-capable GPU with **≥ 8 GB VRAM**, **compute capability sm_50 or newer**
  * Verified working on Pascal sm_61 (GTX 1080 Ti) via cu126 wheel; sm_75
    and newer also work via cu126 or any newer wheel.
  * Setup auto-defaults to the cu126 wheel which keeps Pascal support.
    Override with `INSTANTMESH_TORCH_INDEX=https://download.pytorch.org/whl/cu128`
    if you have RTX 20-series or newer and want the latest kernels.
* Disk: ~6 GB for weights + ~5 GB for venv
* CPU: any (inference is GPU-bound)
* System packages required for the nvdiffrast compile step:
  * `nvidia-cuda-toolkit` (provides `nvcc`)
  * `g++-12` (CUDA 12.0's nvcc rejects g++ ≥ 13; setup uses g++-12 only
    for the nvdiffrast invocation, system default g++ remains untouched)

If GPU is missing, `imageto3d_smoke` should print a clear "no CUDA, skipping"
message rather than crashing.

### Failure modes & mitigations

| Failure | Cause | Mitigation |
|---------|-------|-----------|
| Pip resolver upgrades torch | InstantMesh's `requirements.txt` has unpinned deps (`accelerate`, `bitsandbytes`) that pull latest torch | Setup writes `.torch_constraints.txt` from `pip freeze` after the cu126 install, then uses `pip install -c …` for requirements.txt — pin survives the dep resolution |
| Pascal sm_61 unsupported by torch | cu128/cu130 wheels dropped sm_61 (Apr 2025) | Setup defaults to **cu126** wheel index (still ships `sm_50/60/70/75/80/86/90` PTX, covers 1080 Ti via forward-compat) — override with `INSTANTMESH_TORCH_INDEX` for newer GPUs |
| nvdiffrast compile rejects host g++ | Ubuntu 24.04 ships g++-13 by default; CUDA 12.0's nvcc requires g++ < 13 | Setup checks for `/usr/bin/g++-12` and exports `CUDAHOSTCXX=/usr/bin/g++-12` for the nvdiffrast install only |
| `accelerate` import fails on `split_torch_state_dict_into_shards` | InstantMesh pins `transformers==4.34.1` which forces hub 0.17.x; modern accelerate needs hub ≥ 0.23 | Setup pins `accelerate==0.24.0` (Oct 2023, contemporary with the rest of InstantMesh's pins) |
| `rembg` import fails on `onnxruntime` | InstantMesh's requirements.txt omits onnxruntime (rembg's runtime backend) | Setup explicitly installs onnxruntime |
| First-run weights download stalls | HuggingFace rate limit / network | Resume-on-fail; cache to `~/.cache/huggingface/` (default HF cache, not a custom path) |
| InstantMesh OOM | 8 GB VRAM not actually available (other process using GPU) | Setup target runs `nvidia-smi`; smoke target warns if `llama-server` is running |
| `run_solarium.py` raises on bad input | Background-busy image, multi-subject | Sidecar wrapper preprocesses with `rembg`; on failure returns non-zero + writes diagnostic to `/tmp/imageto3d/<sha1>.err` |

## Web UI — `civ://ui/model_studio/`

```
src/web_apps/model_studio/                ← NEW: peer to src/model_editor/
├── package.json          (vite + typescript + three + zustand)
├── tsconfig.json
├── vite.config.ts        outDir → build/ui/model_studio/
├── index.html
└── src/
    ├── main.ts                    bootstrap, route, hotkeys
    ├── tabs/
    │   ├── editor.ts              Blockbench-style box editor
    │   └── imageto3d.ts           drop-image → preview pane
    ├── viewport/
    │   ├── scene.ts               Three.js scene (shared by both tabs)
    │   ├── modelRenderer.ts       Ports clip math from C++ clip_eval.h
    │   ├── rigOverlay.ts          Bone wireframe overlay on selected mesh
    │   └── gizmos.ts              TransformControls per selected box / bone
    ├── panels/
    │   ├── outliner.ts            Bone tree, drag-reparent, box-to-bone paint
    │   ├── properties.ts          Numeric inputs, color picker, texture picker
    │   └── timeline.ts            Keyframe lanes per bone, scrub, named clip
    ├── state/
    │   ├── modelStore.ts          zustand: { parts, bones, clips, undo[] }
    │   └── serializer.ts          → tokenizer-safe model.py text
    └── bridge/
        └── cef.ts                 Typed wrapper over window.cefQuery
```

POST_BUILD step in `CMakeLists.txt` copies `build/ui/model_studio/` next to
the existing CEF assets so the `civ://ui/` scheme handler can resolve it.

### CEF actions added

| Action | Direction | Body | Result |
|--------|-----------|------|--------|
| `imageto3d.generate` | JS→C++ | `{image_b64, quality}` | Returns `{glb_path, ms}` after subprocess completes |
| `imageto3d.voxelize` | JS→C++ | `{glb_path, resolution}` | Returns `{model_dict}` (calls `voxel_earth/voxelizer.cpp` in-process) |
| `model.list` | JS→C++ | `{ns?}` | Enumerates `src/artifacts/models/**/*.py` |
| `model.load` | JS→C++ | `{ns, id}` | Returns raw `.py` text |
| `model.save` | JS→C++ | `{ns, id, model_dict}` | Writes tokenizer-friendly `.py` |
| `rig.list`   | JS→C++ | — | Enumerates `src/artifacts/rigs/**/*.py` |
| `rig.load`   | JS→C++ | `{ns, id}` | Returns rig template dict |
| `model.spawn`| JS→C++ | `{model_id, pos}` | Spawns live entity via `ActionProposal` pipeline |

## Phased plan

| Phase | Days | Proof point |
|-------|------|-------------|
| **0** | 1 | InstantMesh sidecar setup + smoke test. **`make imageto3d_smoke` writes `/tmp/imageto3d/smoke.glb` from `examples/hatsune_miku.png`.** ← *this is the MVP* |
| **1** | 6 | Engine: bone hierarchy + keyframe clip eval + rig template loader. Existing 16 creatures still work unchanged |
| **2** | 4 | Author **humanoid** template by hand: 15 bones, 9 clips. Apply to existing `guy` (manual binding, ad-hoc since editor doesn't exist yet). Proof: `guy` walks/attacks/waves with the new clip system |
| **3** | 4 | Web UI shell: Vite/TS/Three scaffold; image-to-3D tab; drop image → see voxelized result spinning |
| **4** | 5 | "Spawn in world" — voxelized mesh becomes a static entity, no animation yet |
| **5** | 9 | Editor: bone-overlay UX, drag-to-place, box-to-bone painting, scrub preview |
| **6** | 4 | Save binding, hot-reload as Living artifact, full animation in-world |
| **7** | ~20 | Author the other 4 templates (quadruped, bird, fish, slime). Long pole — clip authoring is the bottleneck |

**~33 days for full pipeline + first template. +~20 days for the other four.**

Phases 0–4 are demo-shippable. Phases 5–6 are needed for end-to-end.
Phase 7 is incremental content.

## MVP scope (today)

Landed:
* This document.
* **Sidecar (Phase 0)**: `make imageto3d_setup` / `imageto3d_smoke` /
  `imageto3d_stop` / `imageto3d_clean`. Setup is idempotent, detects
  venv-in-venv bug, pre-flight checks for `nvcc` and `g++-12`, defaults
  to cu126 PyTorch wheel for Pascal compatibility, freezes torch
  versions to a constraints file so transitive deps don't upgrade,
  pins `accelerate==0.24.0` for hub 0.17 compat, installs onnxruntime
  for rembg, compiles nvdiffrast with `CUDAHOSTCXX=/usr/bin/g++-12`.
* **Phase 1 engine data layer** (additive, non-breaking — existing 16
  creatures unchanged):
  * `src/platform/client/rig.h` (new): `Bone`, `KeyframeRot`,
    `KeyframeChannel`, `KeyframeClip`, `Rig` data types + helpers:
    `evalKeyframeChannel` (lerp + loop), `findBone` (name → idx),
    `computeRigPose` (forward pass, world-from-bone-origin transforms).
  * `src/platform/client/box_model.h`: `BodyPart::bone` and
    `BoxModel::rigId` (both empty by default → legacy path).
  * `src/platform/client/model_loader.h`: parses `rig:` and `bone:`.
  * `src/platform/client/rig_loader.h` (new): parses rig templates from
    `<root>/rigs/<ns>/*.py` via the same tokenizer as `model_loader`.
  * `src/platform/client/rig_registry.h` (new): `loadAll()` scans
    artifacts; `find("base:humanoid") → const Rig*`.
  * `src/platform/client/rig_smoke.cpp` (new) + CMake target
    `solarium-rig-smoke`: loads + dumps + spot-checks lerp eval.
  * `src/artifacts/rigs/base/humanoid.py` (new): first template — 16
    bones (root + pelvis + torso + head + 2 arm chains + 2 leg chains),
    9 clips (idle, walk, run, attack-swing, attack-thrust, wave, sit,
    sleep, hurt). First-pass values; refine after editor lands.

Smoke test passes: `./build/solarium-rig-smoke` loads humanoid.py,
prints all 16 bones with correct parent chains and 9 clips with right
channel/key counts. Lerp eval verified by hand on `hurt.torso` channel.

Next session:
* `client/box_model_flatten.h` — bone hierarchy composition: when
  `BodyPart::bone` is set and a `const Rig*` is provided by the caller,
  use `computeRigPose` to drive the part's transform via the bone's
  parent-chain accumulated matrix. Parts without `bone:` continue down
  the unchanged sin-driver / clip / attack-special-case path.
* Convert `src/artifacts/models/base/guy.py` to use the humanoid rig:
  add `rig: "base:humanoid"` and `bone: <bone_name>` per part. Visual
  diff against current guy in-game to validate the new path.
* Resolve rig id → `const Rig*` at the renderer's entry point (one
  registry lookup per draw call; or cache on `BoxModel*` first-touch).

**Not in MVP**: any C++ change, any web UI, any rig template, any artifact
format extension. The MVP only validates that the AI model produces usable
GLB output on this machine before we invest in everything else. If the smoke
test fails (CUDA mismatch, OOM, bad output quality on a known-good input),
we learn early and adjust before sinking time into the engine and editor.

## Risk register

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|-----------|
| InstantMesh quality below "blocky cat is recognizable" floor after voxelization | Med | High — kills the value prop | Smoke test in Phase 0 measures this on representative inputs before committing to phases 1+ |
| GPU contention with llama-server | High when both are loaded | Med | Per-request subprocess for image-to-3D; sidecar checks `nvidia-smi` and unloads llama if needed |
| `xformers` / CUDA version dance | Med | Low (well-trodden) | Pin known-good triple; document expected versions |
| Authoring the 5 templates' clip libraries dwarfs all other work | High | Med | Accept; phase 7 lasts a month; consider porting Mixamo rigs if license fits |
| Bone-mesh mismatch (humanoid clips on quadruped mesh) | Med | Low (visual only) | Editor pre-flight: warn if rig has bones with no boxes assigned |
| Image rights | Low | Low | One-line ToS in upload UI; no server-side storage of user images |

## Non-goals

* **Raw triangle-mesh rendering in the world.** Voxelize-by-default. No
  PBR shaders, no skinning weights, no GLTF runtime loader for entities.
  Reconsider only if Solarium's identity goal changes.
* **Auto-rigging.** Manual binding only (drag bones to mesh, paint
  box→bone). No OSS auto-rigger meets the bar in 2026.
* **Auto-animation for arbitrary skeletons.** Clips are template-owned; new
  templates require hand-authored clips.
* **Real-time iteration.** 10 s per generation is OK for a "create then
  refine" workflow, not for live editing.
* **Cloud APIs.** No Meshy, no Tripo3D, no Stability cloud. OSS only.
* **Multi-image / video input to InstantMesh.** Single image only for v1.
* **Texture detail beyond voxel-color average.** No per-face UV maps in v1.
* **Mobile / web-deploy.** Native CEF only.

## Open questions

1. **GPU policy when llama-server is loaded.** Hard-stop and tell user, or
   automatic unload-then-reload? Phase 0 smoke test will surface this.
2. **Where does `solarium-imageto3d` get exec'd from?** Currently planning
   on the browser process spawning it directly. Alternative: route through
   `process_manager.h` like the other sidecars.
3. **Stock-clip authoring style.** Hand-author with reference video, port
   from Mixamo (license check), or generate first-pass with an animation
   model and refine. Defer until phase 2.
4. **Rig template authoring.** Should there be an editor mode for
   *creating* templates (not just binding to existing ones)? Defer to
   phase 7+.

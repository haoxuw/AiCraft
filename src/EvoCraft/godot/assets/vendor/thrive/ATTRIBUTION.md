# Thrive asset attribution

These files are imported from **Thrive** — the open-source evolution game by
Revolutionary Games Studio.

- Source:  https://github.com/Revolutionary-Games/Thrive
- Commit:  (see the clone from which these were copied; pinned on import)
- License: **GPL-3.0** — see `LICENSE-GPL3.txt` in this directory.

Because of the GPL-3 viral clause, the entire EvoCraft game, as long as it
ships any of these files, is distributed under GPL-3. Strip this directory
(and avoid the Thrive shader math we referenced) if you ever need a more
permissive license.

## Imported files

| Our path                          | Thrive source path                                | Purpose in EvoCraft |
|-----------------------------------|---------------------------------------------------|---------------------|
| `organelles/Flagellum.glb`        | `assets/models/organelles/Flagellum.glb`          | Tail mesh for swim animation |
| `organelles/Cilia.glb`            | `assets/models/organelles/Cilia.glb`              | Side-oar mesh for NPC microbes |
| `organelles/SlimeJet.glb`         | `assets/models/organelles/SlimeJet.glb`           | Extra organelle for predator species |
| `organelles/GooglyEyeCell.glb`    | `assets/models/easter_eggs/GooglyEyeCell.glb`     | Stand-in hero cell (whole microbe w/ eye) |
| `organelles/GooglyEyeCell_*.png`  | (embedded texture siblings, imported alongside)   | GooglyEyeCell material textures |
| `particles/bubble3.png`           | `assets/textures/bubble3.png`                     | GPUParticles3D bubble texture |
| `particles/background_particle_fuzzy.png` | `assets/textures/background_particle_fuzzy.png` | Light mote texture |
| `particles/blurry_circle.png`     | `assets/textures/blurry_circle.png`               | Soft glow / food halo |

All textures and meshes are © Revolutionary Games Studio & Thrive
contributors, used under GPL-3.0.

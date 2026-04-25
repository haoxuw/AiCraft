# Footstep sound attribution

All sources are **CC0 / Public Domain** — no attribution required, but credits
are listed for traceability. Files were peak-normalized to ~0.95 and re-encoded
as OGG Vorbis at the source sample rate (44.1 kHz, stereo).

## Sources

| Pack | Author | Surfaces used | License |
|------|--------|---------------|---------|
| [Fantozzi's Footsteps](https://opengameart.org/content/fantozzis-footsteps-grasssand-stone) | Fantozzi | Stone, Sand | CC0 |
| [Different steps on wood, stone, leaves, gravel and mud](https://opengameart.org/content/different-steps-on-wood-stone-leaves-gravel-and-mud) | TinyWorlds | Wood, Stone, Gravel, Mud, Leaves | CC0 |
| [4 dry snow steps](https://opengameart.org/content/4-dry-snow-steps) | Iwan "qubodup" Gabovitch | Snow | CC0 |

## Per-file mapping

| Engine file                  | Source                              |
|------------------------------|-------------------------------------|
| `footstep_concrete_00[0-4]`  | Fantozzi-Stone L1, L2, L3, R1, R2   |
| `footstep_grass_00[0-4]`     | Fantozzi-Sand L1, L2, L3, R1, R2    |
| `footstep_wood_00[0-2]`      | TinyWorlds wood01, wood02, wood03   |
| `footstep_wood_003`          | Fantozzi-Stone R3                   |
| `footstep_wood_004`          | TinyWorlds stone01                  |
| `footstep_snow_00[0-3]`      | qubodup snow_step_dry-01..04        |
| `footstep_snow_004`          | qubodup snow_step_dry-01 (cycled)   |
| `footstep00`                 | Fantozzi-Sand R3                    |
| `footstep01`                 | TinyWorlds gravel                   |
| `footstep02`                 | TinyWorlds mud02                    |
| `footstep03–04`              | TinyWorlds leaves01, leaves02       |
| `footstep05–09`              | Fantozzi-Sand L1, L2, L3, R1, R2    |

## Engine wiring

The audio loader at `src/platform/client/audio.cpp:146-151` maps prefix → group:

| Group        | Prefix              | Variants |
|--------------|---------------------|----------|
| `step_stone` | `footstep_concrete` | 5        |
| `step_grass` | `footstep_grass`    | 5        |
| `step_wood`  | `footstep_wood`     | 5        |
| `step_snow`  | `footstep_snow`     | 5        |
| `step_dirt`  | `footstep0`         | 10       |
| `step_sand`  | `footstep0`         | 10 (alias of dirt) |

The previous `footstep_carpet_*` files were removed — the carpet group is not
wired up in `audio.cpp`.

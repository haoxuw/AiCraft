# Resources

Game assets: textures, models, and sounds. Downloaded from free sources or created from scratch.

## Directory Structure

```
resources/
  textures/    Block textures, entity skins, UI sprites (PNG) — empty, pending
  models/      3D models for entities and objects (OBJ, glTF) — empty, pending
  sounds/      Sound effects and music (OGG, WAV) — 332 files from Kenney
    blocks/      75 files — block break/place impacts
    footsteps/   35 files — surface-specific walking sounds
    combat/      36 files — punch, sword, shield impacts
    items/       41 files — coins, leather, books, cloth
    ui/         145 files — clicks, switches, scrolls, confirmations
    creatures/   placeholder — needs dedicated animal recordings
    ambient/     placeholder — needs nature/wind loops
```

## Current Assets

### Sounds (332 OGG files, CC0)
All sound effects are from **Kenney** (kenney.nl) — CC0 licensed, no attribution required.

| Source Pack | Files | URL |
|---|---|---|
| Impact Sounds | 75 | https://kenney.nl/assets/impact-sounds |
| RPG Audio | 50 | https://kenney.nl/assets/rpg-audio |
| UI Audio | 50 | https://kenney.nl/assets/ui-audio |
| Interface Sounds | 100 | https://kenney.nl/assets/interface-sounds |

See `sounds/MANIFEST.md` for detailed group mappings and upgrade instructions.

## How Resources Work

1. Sound files live in `resources/sounds/` organized by category
2. The AudioManager (`src/client/audio.h`) loads all sounds at startup
3. Sounds are grouped (e.g. `dig_stone` = 5 random variants of mining impacts)
4. Game events trigger sound groups (block break, footsteps, creature ambience)
5. Resource definitions in `artifacts/resources/base/*.py` appear in the Handbook's Resources tab

## Sources Guide

See `ASSET_SOURCES.md` for a complete mapping of every built-in game object to a free asset pack.
See `17_RESOURCE_GUIDE.md` in the project root for the full open-source resource catalog.

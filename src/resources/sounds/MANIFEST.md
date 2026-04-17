# Sound Asset Manifest

All sounds are **CC0 (Creative Commons Zero)** — no attribution required, free for any use.

## Sources

| Pack | Files | URL |
|------|-------|-----|
| Kenney Impact Sounds | 75 | https://kenney.nl/assets/impact-sounds |
| Kenney RPG Audio | 50 | https://kenney.nl/assets/rpg-audio |
| Kenney UI Audio | 50 | https://kenney.nl/assets/ui-audio |
| Kenney Interface Sounds | 100 | https://kenney.nl/assets/interface-sounds |

**Total: 332 OGG files, ~3.6 MB**

## Directory Layout

```
sounds/
  blocks/       75 files — block break/place impacts
  footsteps/    35 files — surface-specific walking sounds
  combat/       36 files — punch, sword, shield impacts
  items/        41 files — coins, leather, books, cloth
  ui/          145 files — clicks, switches, scrolls, confirmations
  creatures/     0 files — placeholder (needs dedicated animal recordings)
  ambient/       0 files — placeholder (needs nature/wind loops)
```

## Sound Group Mappings

These groups are used by the game engine's AudioManager:

### Block Interaction
| Group | Files | Sound Type |
|-------|-------|------------|
| `dig_stone` | `blocks/impactMining_*.ogg` | Stone/ore mining |
| `dig_dirt` | `blocks/impactSoft_medium_*.ogg` | Dirt/grass digging |
| `dig_sand` | `blocks/impactSoft_heavy_*.ogg` | Sand/gravel digging |
| `dig_wood` | `blocks/impactWood_medium_*.ogg` | Wood chopping |
| `dig_leaves` | `blocks/impactSoft_medium_*.ogg` | Leaf breaking |
| `dig_snow` | `blocks/impactSoft_heavy_*.ogg` | Snow digging |
| `dig_glass` | `blocks/impactGlass_medium_*.ogg` | Glass breaking |
| `dig_metal` | `blocks/impactMetal_medium_*.ogg` | Metal block mining |
| `place_stone` | `blocks/impactGeneric_light_*.ogg` | Placing stone blocks |
| `place_wood` | `blocks/impactWood_light_*.ogg` | Placing wood blocks |
| `place_soft` | `blocks/impactSoft_heavy_*.ogg` | Placing soft blocks |

### Footsteps
| Group | Files | Surface |
|-------|-------|---------|
| `step_stone` | `footsteps/footstep_concrete_*.ogg` | Stone, cobblestone |
| `step_dirt` | `footsteps/footstep0*.ogg` | Dirt, generic |
| `step_grass` | `footsteps/footstep_grass_*.ogg` | Grass blocks |
| `step_wood` | `footsteps/footstep_wood_*.ogg` | Wood planks |
| `step_snow` | `footsteps/footstep_snow_*.ogg` | Snow |

### Combat
| Group | Files | Use |
|-------|-------|-----|
| `hit_punch` | `combat/impactPunch_medium_*.ogg` | Unarmed hits |
| `hit_sword` | `combat/knifeSlice*.ogg` | Sword attacks |
| `hit_shield` | `combat/impactPlate_light_*.ogg` | Shield blocks |

### Items
| Group | Files | Use |
|-------|-------|-----|
| `item_pickup` | `items/handleCoins*.ogg` | Picking up items |
| `item_equip` | `items/leather*.ogg` | Equipping gear |
| `item_book` | `items/bookFlip*.ogg` | Opening books/menus |

### Creatures (Placeholder)
| Group | Files | Animal |
|-------|-------|--------|
| `creature_pig` | `blocks/impactSoft_medium_*.ogg` | Pig ambient |
| `creature_chicken` | `blocks/impactGeneric_light_*.ogg` | Chicken ambient |
| `creature_dog` | `blocks/impactPlank_medium_*.ogg` | Dog ambient |
| `creature_cat` | `blocks/impactSoft_heavy_*.ogg` | Cat ambient |

### UI
| Group | Files | Use |
|-------|-------|-----|
| `ui_click` | `ui/click_*.ogg` | Button clicks |
| `ui_select` | `ui/select_*.ogg` | Menu selection |
| `ui_confirm` | `ui/confirmation_*.ogg` | Action confirmation |
| `ui_open` | `ui/open_*.ogg` | Menu/panel open |
| `ui_close` | `ui/close_*.ogg` | Menu/panel close |
| `ui_scroll` | `ui/scroll_*.ogg` | List scrolling |
| `ui_switch` | `ui/switch_00*.ogg` | Toggle switches |
| `ui_error` | `ui/error_*.ogg` | Error feedback |

## Upgrading

To improve creature sounds, download dedicated animal recordings:
- **Kenney Animal Sounds Essentials**: pig oinks, chicken clucks, dog barks
- **OpenGameArt rubberduck 80 Creature SFX**: https://opengameart.org/content/80-cc0-creature-sfx
- **Freesound.org CC0 search**: individual high-quality recordings

Place new files in `sounds/creatures/` and update the group mappings in `src/client/audio.cpp`.

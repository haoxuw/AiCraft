# Asset Sources for Built-in Handbook Content

Maps every built-in entry to a specific downloadable asset pack.
All packs listed are free. CC0 preferred; CC-BY-SA noted where unavoidable.

---

## Textures (Block Faces)

**Primary pack: Pixel Perfection** (16x pixel art, complete block+item coverage)
- Repo: https://github.com/minetest-texture-packs/Pixel-Perfection
- License: CC-BY-SA-4.0 (attribution + share-alike required)
- Covers: dirt, grass, stone, cobblestone, granite, sand, red_sand, gravel, clay,
  snow, ice, bedrock, obsidian, coal_ore, iron_ore, wood_oak, wood_birch,
  planks_oak, leaves_oak, leaves_birch, tnt, wheat, carrot, potato, wire

**Fallback (CC0): ambientCG** — downsample PBR to 16x or use as painting reference
- URL: https://ambientcg.com
- License: CC0

| Block ID | Pixel Perfection File | Notes |
|---|---|---|
| `base:dirt` | `default_dirt.png` | |
| `base:grass` | `default_grass.png` + `default_grass_side.png` | Top/side variants |
| `base:stone` | `default_stone.png` | |
| `base:cobblestone` | `default_cobble.png` | |
| `base:granite` | — | Hand-paint from stone variant |
| `base:podzol` | — | Recolor dirt |
| `base:sand` | `default_sand.png` | |
| `base:red_sand` | `default_desert_sand.png` | |
| `base:gravel` | `default_gravel.png` | |
| `base:clay` | — | Recolor from sand/dirt |
| `base:snow` | `default_snow.png` | |
| `base:ice` | `default_ice.png` | |
| `base:bedrock` | — | Dark stone recolor |
| `base:obsidian` | `default_obsidian.png` | |
| `base:coal_ore` | `default_stone.png` overlay with `default_mineral_coal.png` | |
| `base:iron_ore` | `default_stone.png` overlay with `default_mineral_iron.png` | |
| `base:wood_oak` | `default_tree.png` + `default_tree_top.png` | |
| `base:wood_birch` | `default_aspen_tree.png` + `default_aspen_tree_top.png` | |
| `base:planks_oak` | `default_wood.png` | |
| `base:leaves_oak` | `default_leaves.png` | |
| `base:leaves_birch` | `default_aspen_leaves.png` | |
| `base:tnt` | `tnt_top.png` + `tnt_side.png` | |
| `base:wheat` | `farming_wheat_*.png` | Growth stages 1-8 |
| `base:carrot` | `farming_carrot_*.png` | Growth stages |
| `base:potato` | `farming_potato_*.png` | Growth stages |
| `base:wire` | — | Simple 2px red line on gray |
| `base:power_source` | — | Yellow glow on gray |
| `base:nand_gate` | — | Gate symbol on dark gray |

---

## Creature Models

**Primary: Quaternius Ultimate Animated Animals** (CC0, FBX/glTF, 12+ anims each)
- URL: https://quaternius.com/packs/ultimateanimatedanimals.html
- License: CC0
- Animations: idle, walk, run, attack, death, eat, jump, gallop, sit

**Supplement: Kenney Cube Pets** (CC0, voxel-style dog+cat)
- URL: https://kenney.nl/assets/cube-pets
- License: CC0

| Creature | Source Pack | Model Name | Notes |
|---|---|---|---|
| `base:pig` | Quaternius Animals | Pig | Idle, walk, run, eat |
| `base:chicken` | Quaternius Animals | Chicken | Idle, walk, peck, flap |
| `base:dog` | Quaternius Animals | Dog | Idle, walk, run, sit |
| `base:dog` (alt) | Kenney Cube Pets | Dog | Voxel-style alternative |
| `base:cat` | Quaternius Animals | Cat | Idle, walk, run, sit, pounce |
| `base:cat` (alt) | Kenney Cube Pets | Cat | Voxel-style alternative |
| `base:villager` | *See Characters below* | — | Use humanoid model |

---

## Character Models

**Primary: KayKit Adventurers** (CC0, rigged+animated, FBX/glTF)
- URL: https://kaylousberg.itch.io/kaykit-adventurers
- License: CC0
- 5 characters, walk/run/idle/attack/death animations

**KayKit Skeletons** (CC0, 4 skeleton variants + 10 weapons)
- URL: https://kaylousberg.itch.io/kaykit-skeletons
- License: CC0

**Kenney Blocky Characters** (CC0, 20 animated blocky humanoids)
- URL: https://kenney.nl/assets/blocky-characters
- License: CC0

**Quaternius Animations** (CC0, 120+ retargetable animations)
- URL: https://quaternius.com/packs/universalanimationlibrary.html
- License: CC0

| Character | Source Pack | Model | Notes |
|---|---|---|---|
| `base:knight` | KayKit Adventurers | Knight/Warrior | Heavy armor character |
| `base:mage` | KayKit Adventurers | Mage | Robed character |
| `base:skeleton` | KayKit Skeletons | Skeleton Warrior | Undead + weapons |
| `base:crewmate` | Kenney Blocky Characters | Astronaut-style | Closest match |
| `base:giant` | Quaternius + scale up | Large humanoid | Scale 2-3x |
| `base:villager` | Kenney Blocky Characters | Villager/NPC | Civilian clothing |

---

## Item Models & Icons

**3D Models: Quaternius Medieval Weapons** (CC0)
- URL: https://quaternius.com/packs/medievalweapons.html
- Covers: sword, shield, dagger, bow, hammer, axe

**3D Props: KayKit Dungeon Remastered** (CC0, 200+ props)
- URL: https://kaylousberg.itch.io/kaykit-dungeon-remastered
- Covers: torch, chest, barrel, potion bottle, bucket

**2D Icons: Game-Icons.net** (CC-BY 3.0, 4000+ SVGs)
- URL: https://game-icons.net
- Covers: every item type imaginable

| Item | 3D Source | Icon Source | Notes |
|---|---|---|---|
| `base:sword` | Quaternius Medieval Weapons | game-icons.net `broadsword` | |
| `base:shield` | Quaternius Medieval Weapons | game-icons.net `round-shield` | |
| `base:helmet` | KayKit Adventurers (equipped) | game-icons.net `viking-helmet` | |
| `base:boots` | KayKit Adventurers (equipped) | game-icons.net `boots` | |
| `base:cape` | — | game-icons.net `cape` | Simple cloth mesh |
| `base:torch` | KayKit Dungeon Remastered | game-icons.net `torch` | |
| `base:bucket` | KayKit Dungeon Remastered | game-icons.net `bucket` | |
| `base:potion` | KayKit Dungeon Remastered | game-icons.net `potion-ball` | |

---

## Sound Effects

All CC0 unless noted.

### Block Interaction
| Sound | Source Pack | URL |
|---|---|---|
| Block break (stone) | rubberduck 75 Breaking/Hit | https://opengameart.org/content/75-cc0-breaking-falling-hit-sfx |
| Block break (wood) | rubberduck 100 Metal & Wood | https://opengameart.org/content/100-cc0-metal-and-wood-sfx |
| Block place | rubberduck 100 CC0 SFX | https://opengameart.org/content/100-cc0-sfx |
| Mining/digging | rubberduck 100 Metal & Wood | (hammer, tools subcategory) |
| TNT explosion | rubberduck 100 CC0 SFX | (explosions subcategory) |

### Footsteps
| Sound | Source Pack | URL |
|---|---|---|
| Stone/wood/leaves/gravel/mud | Steps on Surfaces (CC0) | https://opengameart.org/content/different-steps-on-wood-stone-leaves-gravel-and-mud |
| Snow/dirt/grass | Walking Steps (CC0) | https://opengameart.org/content/foot-walking-step-sounds-on-stone-water-snow-wood-and-dirt |

### Combat
| Sound | Source Pack | URL |
|---|---|---|
| Sword swing/hit | rubberduck 80 RPG SFX | https://opengameart.org/content/80-cc0-rpg-sfx |
| Damage taken | rubberduck 80 RPG SFX | (creature hurt subcategory) |
| Knockback impact | Kenney Impact Sounds | https://kenney.nl/assets/impact-sounds |
| Shield block | rubberduck 100 Metal & Wood | (metal hit subcategory) |

### Creature Sounds
| Sound | Source Pack | URL |
|---|---|---|
| Pig grunt/oink | rubberduck 80 Creature SFX | https://opengameart.org/content/80-cc0-creature-sfx |
| Chicken cluck | rubberduck 80 Creature SFX | (cute subcategory) |
| Dog bark | rubberduck 80 Creature SFX | (barking subcategory) |
| Cat meow | Freesound.org CC0 search | https://freesound.org (filter: CC0, "cat meow") |
| Villager hmm | rubberduck 80 Creature SFX | (grunt subcategory) |

### Items & Effects
| Sound | Source Pack | URL |
|---|---|---|
| Item pickup | 51 UI Sound Effects (CC0) | https://opengameart.org/content/51-ui-sound-effects-buttons-switches-and-clicks |
| Potion drink | rubberduck 40 Water/Splash | https://opengameart.org/content/40-cc0-water-splash-slime-sfx |
| Heal effect | rubberduck 80 RPG SFX | (spell subcategory) |
| Haste buff | rubberduck 80 RPG SFX | (spell subcategory) |
| Poison tick | rubberduck 80 RPG SFX | (creature slime subcategory) |
| Water splash | rubberduck 40 Water/Splash | (splash subcategory) |

### UI
| Sound | Source Pack | URL |
|---|---|---|
| Menu click | Kenney UI Audio | https://kenney.nl/assets/ui-audio |
| Hotbar select | Kenney Interface Sounds | https://kenney.nl/assets/interface-sounds |
| Inventory open/close | 51 UI Sound Effects | (switches subcategory) |

### Particles
| Asset | Source Pack | URL |
|---|---|---|
| Smoke, sparks, stars, flares | Kenney Particle Pack | https://kenney.nl/assets/particle-pack |

---

## Download Priority

Get these first — they cover the most built-in content per download:

1. **Pixel Perfection** — all block textures in one clone (CC-BY-SA-4.0)
2. **Quaternius Ultimate Animated Animals** — pig, chicken, dog, cat (CC0)
3. **KayKit Adventurers + Skeletons** — knight, mage, skeleton + weapons (CC0)
4. **Kenney Blocky Characters** — crewmate, villager, giant base (CC0)
5. **Quaternius Medieval Weapons** — sword, shield, melee items (CC0)
6. **KayKit Dungeon Remastered** — torch, bucket, potion props (CC0)
7. **rubberduck SFX bundle** (all 7 packs) — 575 sounds covering everything (CC0)
8. **Kenney Impact + RPG + UI Audio** — 280 more sounds (CC0)

Total: ~8 downloads to cover all ~70 built-in handbook entries.

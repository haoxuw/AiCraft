# Minetest Game - Complete Reconstruction Specification

This document describes every aspect of the Minetest Game in enough detail to reconstruct a new game from scratch on the Luanti engine.

---

## Table of Contents

1. [Top-Level Structure](#1-top-level-structure)
2. [Mod List & Dependency Graph](#2-mod-list--dependency-graph)
3. [Core Mod: `default`](#3-core-mod-default)
4. [Player System: `player_api`](#4-player-system-player_api)
5. [Inventory System: `sfinv`](#5-inventory-system-sfinv)
6. [Creative Mode: `creative`](#6-creative-mode-creative)
7. [Craft Guide: `mtg_craftguide`](#7-craft-guide-mtg_craftguide)
8. [Beds & Sleeping: `beds`](#8-beds--sleeping-beds)
9. [Boats: `boats`](#9-boats-boats)
10. [Death & Bones: `bones`](#10-death--bones-bones)
11. [Bucket System: `bucket`](#11-bucket-system-bucket)
12. [Doors, Trapdoors, Gates: `doors`](#12-doors-trapdoors-gates-doors)
13. [Dye System: `dye`](#13-dye-system-dye)
14. [Wool: `wool`](#14-wool-wool)
15. [Farming: `farming`](#15-farming-farming)
16. [Fire: `fire`](#16-fire-fire)
17. [Flowers & Mushrooms: `flowers`](#17-flowers--mushrooms-flowers)
18. [Stairs & Slabs: `stairs`](#18-stairs--slabs-stairs)
19. [TNT & Explosions: `tnt`](#19-tnt--explosions-tnt)
20. [Vessels: `vessels`](#20-vessels-vessels)
21. [Walls: `walls`](#21-walls-walls)
22. [Glass Panes & Bars: `xpanes`](#22-glass-panes--bars-xpanes)
23. [Screwdriver: `screwdriver`](#23-screwdriver-screwdriver)
24. [Keys: `keys`](#24-keys-keys)
25. [Map & Minimap: `map`](#25-map--minimap-map)
26. [Binoculars: `binoculars`](#26-binoculars-binoculars)
27. [Initial Stuff: `give_initial_stuff`](#27-initial-stuff-give_initial_stuff)
28. [Spawn System: `spawn`](#28-spawn-system-spawn)
29. [Chat Commands: `game_commands`](#29-chat-commands-game_commands)
30. [Home Teleport: `sethome`](#30-home-teleport-sethome)
31. [Environmental Sounds: `env_sounds`](#31-environmental-sounds-env_sounds)
32. [Butterflies: `butterflies`](#32-butterflies-butterflies)
33. [Fireflies: `fireflies`](#33-fireflies-fireflies)
34. [Minecarts: `carts`](#34-minecarts-carts)
35. [Dungeon Loot: `dungeon_loot`](#35-dungeon-loot-dungeon_loot)
36. [Weather: `weather`](#36-weather-weather)
37. [Asset Naming Conventions](#37-asset-naming-conventions)
38. [Model & Schematic Files](#38-model--schematic-files)
39. [Settings Reference](#39-settings-reference)

---

## 1. Top-Level Structure

A Luanti game is a directory under `games/` with this layout:

```
mygame/
  game.conf            -- metadata (title, description, min engine version)
  minetest.conf        -- default engine setting overrides (can be empty)
  settingtypes.txt     -- game-specific settings exposed in the settings menu
  menu/
    header.png         -- main menu header image
    icon.png           -- game icon
  mods/
    modname/
      mod.conf         -- mod metadata (name, description, depends, optional_depends)
      init.lua         -- entry point, executed on server start
      textures/        -- PNG textures
      sounds/          -- OGG sound files
      models/          -- B3D/OBJ 3D models
      schematics/      -- MTS schematic files for structures
      locale/          -- translation files
```

### game.conf

```
title = Minetest Game
description = A basic exploration, mining, crafting, and building sandbox game...
min_minetest_version = 5.8
textdomain = game_description
```

### .luacheckrc

Declares global tables and read-only engine globals for Lua linting:

- Globals: `default`
- Read globals: `DIR_DELIM`, `core`, `minetest`, `dump`, `vector`, `VoxelManip`, `VoxelArea`, `PseudoRandom`, `PcgRandom`, `ItemStack`, `Settings`, `unpack`, `table`, `math`, `player_monoids`, `pova`

---

## 2. Mod List & Dependency Graph

Minetest Game contains **34 mods**. The `default` mod is the foundation; most other mods depend on it.

| Mod | Hard Depends | Optional Depends |
|-----|-------------|-----------------|
| **default** | _(none)_ | player_api |
| **player_api** | _(none)_ | _(none)_ |
| **sfinv** | _(none)_ | _(none)_ |
| **creative** | sfinv | _(none)_ |
| **mtg_craftguide** | sfinv | _(none)_ |
| **dye** | _(none)_ | _(none)_ |
| **wool** | default, dye | _(none)_ |
| **beds** | default, wool, spawn | player_monoids, pova |
| **binoculars** | default | _(none)_ |
| **boats** | default, player_api | _(none)_ |
| **bones** | default | _(none)_ |
| **bucket** | default | dungeon_loot |
| **butterflies** | default, flowers | _(none)_ |
| **carts** | default, player_api | dungeon_loot |
| **doors** | default | screwdriver |
| **dungeon_loot** | default | _(none)_ |
| **env_sounds** | default | _(none)_ |
| **farming** | default, wool, stairs | dungeon_loot |
| **fire** | default | _(none)_ |
| **fireflies** | default, vessels | _(none)_ |
| **flowers** | default | _(none)_ |
| **game_commands** | _(none)_ | _(none)_ |
| **give_initial_stuff** | default | _(none)_ |
| **keys** | default | _(none)_ |
| **map** | default, dye | _(none)_ |
| **screwdriver** | _(none)_ | _(none)_ |
| **sethome** | _(none)_ | _(none)_ |
| **spawn** | default | _(none)_ |
| **stairs** | default | _(none)_ |
| **tnt** | default, fire | _(none)_ |
| **vessels** | default | dungeon_loot |
| **walls** | default | _(none)_ |
| **weather** | _(none)_ | _(none)_ |
| **xpanes** | default | doors |

### Dependency Graph (simplified)

```
                  (none)
                    |
    +-------+-------+-------+-------+
    |       |       |       |       |
 default  sfinv  player_api  dye  screwdriver, sethome, game_commands, weather
    |       |       |         |
    +---+---+   +---+---+    |
    |   |   |   |       |    |
  wool beds boats ...  carts |
    |                        |
  farming                    |
    |                        |
    +---- flowers -- butterflies
```

---

## 3. Core Mod: `default`

The `default` mod provides the foundation: terrain nodes, ores, trees, tools, crafting recipes, the furnace, chests, torches, and all core game mechanics.

### 3.1 Global Table & Constants

```lua
default = {}
default.LIGHT_MAX = 14
```

### 3.2 GUI Setup

On player join, the mod sets:
- **Formspec prepend**: `bgcolor[#080808BB;true]`, listcolors, background `gui_formbg.png`
- **Hotbar images**: `gui_hotbar.png` (bar), `gui_hotbar_selected.png` (selection)

Helper functions:
- `default.get_hotbar_bg(x, y)` -- returns 8 `gui_hb_bg.png` images for formspec background
- `default.gui_survival_form` -- standard 8x8.5 formspec with 3x3 craft grid + 4x8 inventory

### 3.3 Sound Definition Functions

Each returns a table with `footstep`, `dig`, `dug`, `place` fields for use in node definitions:

| Function | Key Sounds |
|----------|-----------|
| `default.node_sound_defaults(tbl)` | `default_dug_node`, `default_place_node_hard` |
| `default.node_sound_stone_defaults(tbl)` | `default_hard_footstep` |
| `default.node_sound_dirt_defaults(tbl)` | `default_dirt_footstep`, `default_dig_crumbly`, `default_place_node` |
| `default.node_sound_sand_defaults(tbl)` | `default_sand_footstep`, `default_place_node` |
| `default.node_sound_gravel_defaults(tbl)` | `default_gravel_footstep`, `default_gravel_dig`, `default_gravel_dug` |
| `default.node_sound_wood_defaults(tbl)` | `default_wood_footstep`, `default_dig_choppy` |
| `default.node_sound_leaves_defaults(tbl)` | `default_grass_footstep`, `default_place_node` |
| `default.node_sound_glass_defaults(tbl)` | `default_glass_footstep`, `default_break_glass` |
| `default.node_sound_ice_defaults(tbl)` | `default_ice_footstep`, `default_ice_dig`, `default_ice_dug` |
| `default.node_sound_metal_defaults(tbl)` | `default_metal_footstep`, `default_dig_metal`, `default_dug_metal`, `default_place_node_metal` |
| `default.node_sound_water_defaults(tbl)` | `default_water_footstep` |
| `default.node_sound_snow_defaults(tbl)` | `default_snow_footstep`, `default_place_node` |

### 3.4 Helper & API Functions

| Function | Purpose |
|----------|---------|
| `default.cool_lava(pos, node)` | Lava source -> obsidian; flowing lava -> stone. Registered as ABM: interval=2, chance=2 |
| `default.get_inventory_drops(pos, inventory, drops)` | Collects all items from a node's inventory into drops table |
| `default.grow_cactus(pos, node)` | Grows cactus on sand, max height 4, needs light >= 13. ABM: interval=12, chance=83 |
| `default.grow_papyrus(pos, node)` | Grows papyrus on dirt near water, max height 4. ABM: interval=14, chance=71 |
| `default.dig_up(pos, node, digger, max_height)` | Digs a column of same-type nodes upward (used by cactus/papyrus) |
| `default.register_fence(name, def)` | Registers a connected-nodebox fence + craft recipe (output 4) |
| `default.register_fence_rail(name, def)` | Registers fence rail variant (output 16) |
| `default.register_mesepost(name, def)` | Registers mese post light (glass + mese + material, output 4) |
| `default.after_place_leaves(pos, ...)` | Sets param2=1 to mark player-placed leaves (prevents decay) |
| `default.register_leafdecay(def)` | `{trunks, leaves, radius}` -- timer-based leaf decay system |
| `default.register_craft_metadata_copy(ingredient, result)` | Shapeless craft that copies metadata (e.g. written book duplication) |
| `default.log_player_action(player, ...)` | Logs player actions to server log |
| `default.set_inventory_action_loggers(def, name)` | Adds move/put/take action loggers to a node definition |
| `default.can_interact_with_node(player, pos)` | Checks owner, protection_bypass privilege, or key match |

### 3.5 Active Block Modifiers (ABMs)

| ABM | Nodenames | Interval | Chance | Effect |
|-----|-----------|----------|--------|--------|
| Grass spread | `group:spreading_dirt_type` | 6 | 50 | Converts nearby dirt to grass/snow variant |
| Grass covered | `group:spreading_dirt_type` | 8 | 50 | Reverts to dirt when in darkness |
| Moss growth | `default:cobble` | 16 | 200 | Converts cobble near water to mossycobble |
| Lava cooling | `default:lava_source`, `default:lava_flowing` | 2 | 2 | Source->obsidian, flowing->stone near water |
| Cactus growth | `default:cactus` | 12 | 83 | Grows upward, max height 4 |
| Papyrus growth | `default:papyrus` | 14 | 71 | Grows upward near water, max height 4 |

### 3.6 All Registered Nodes

#### Stone Variants

Each stone type has 4 forms: raw, cobble, brick, block.

| Base Stone | Raw Node | Cobble | Brick | Block | Groups |
|-----------|----------|--------|-------|-------|--------|
| Stone | `default:stone` (drops cobble) | `default:cobble` | `default:stonebrick` | `default:stone_block` | cracky=3, stone=1 |
| Desert stone | `default:desert_stone` (drops desert_cobble) | `default:desert_cobble` | `default:desert_stonebrick` | `default:desert_stone_block` | cracky=3, stone=1 |
| Sandstone | `default:sandstone` | _(none)_ | `default:sandstonebrick` | `default:sandstone_block` | crumbly=1, cracky=3 |
| Desert sandstone | `default:desert_sandstone` | _(none)_ | `default:desert_sandstone_brick` | `default:desert_sandstone_block` | crumbly=1, cracky=3 |
| Silver sandstone | `default:silver_sandstone` | _(none)_ | `default:silver_sandstone_brick` | `default:silver_sandstone_block` | crumbly=1, cracky=3 |
| Obsidian | `default:obsidian` | _(none)_ | `default:obsidianbrick` | `default:obsidian_block` | cracky=1, level=2 |
| Mossy cobble | `default:mossycobble` | _(same)_ | _(none)_ | _(none)_ | cracky=3, stone=1 |

#### Terrain & Dirt Nodes

| Node | Groups | Special |
|------|--------|---------|
| `default:dirt` | crumbly=3, soil=1 | Base soil |
| `default:dirt_with_grass` | spreading_dirt_type=1 | Drops dirt; grass spread ABM |
| `default:dirt_with_grass_footsteps` | not_in_creative_inventory=1 | Temporary footstep variant |
| `default:dirt_with_dry_grass` | spreading_dirt_type=1 | Dry grass overlay |
| `default:dirt_with_snow` | spreading_dirt_type=1, snowy=1 | Snow overlay |
| `default:dirt_with_rainforest_litter` | spreading_dirt_type=1 | Rainforest floor |
| `default:dirt_with_coniferous_litter` | spreading_dirt_type=1 | Pine forest floor |
| `default:dry_dirt` | crumbly=3 | No grass |
| `default:dry_dirt_with_dry_grass` | spreading_dirt_type=1 | Dry variant |
| `default:permafrost` | cracky=3 | Frozen ground |
| `default:permafrost_with_stones` | cracky=3 | Frozen with stones |
| `default:permafrost_with_moss` | cracky=3 | Frozen with moss |
| `default:sand` | crumbly=3, falling_node=1, sand=1 | Falls with gravity |
| `default:desert_sand` | crumbly=3, falling_node=1, sand=1 | Desert variant |
| `default:silver_sand` | crumbly=3, falling_node=1, sand=1 | Silver variant |
| `default:gravel` | crumbly=2, falling_node=1 | 1/16 chance drops flint |
| `default:clay` | crumbly=3 | Drops 4x clay_lump |
| `default:snow` | crumbly=3, falling_node=1, snowy=1 | Half-height nodebox slab |
| `default:snowblock` | crumbly=3, cools_lava=1, snowy=1 | Full block |
| `default:ice` | cracky=3, cools_lava=1, slippery=3 | Slippery surface |
| `default:cave_ice` | cracky=3, cools_lava=1 | Underground ice |

#### Trees (5 species)

Each species has: trunk, planks, leaves, sapling. Saplings grow via node timers (random 300-1500s) using schematics.

| Species | Trunk | Planks | Leaves | Sapling | Special |
|---------|-------|--------|--------|---------|---------|
| Apple | `default:tree` | `default:wood` | `default:leaves` (1/20 sapling drop) | `default:sapling` | `default:apple` (food, heals 2) |
| Jungle | `default:jungletree` | `default:junglewood` | `default:jungleleaves` | `default:junglesapling` | `default:emergent_jungle_sapling` |
| Pine | `default:pine_tree` | `default:pine_wood` | `default:pine_needles` | `default:pine_sapling` | |
| Acacia | `default:acacia_tree` | `default:acacia_wood` | `default:acacia_leaves` | `default:acacia_sapling` | |
| Aspen | `default:aspen_tree` | `default:aspen_wood` | `default:aspen_leaves` | `default:aspen_sapling` | |

**Leaf decay registration** -- leaves disappear when trunk is removed:

| Species | Trunks | Leaves | Radius |
|---------|--------|--------|--------|
| Apple | `tree` | `leaves`, `apple` | 3 |
| Jungle | `jungletree` | `jungleleaves` | 2 |
| Pine | `pine_tree` | `pine_needles` | 3 |
| Acacia | `acacia_tree` | `acacia_leaves` | 2 |
| Aspen | `aspen_tree` | `aspen_leaves` | 3 |

#### Bushes (4 types)

| Type | Stem | Leaves | Sapling | Leaf Decay Radius |
|------|------|--------|---------|-------------------|
| Bush | `default:bush_stem` | `default:bush_leaves` | `default:bush_sapling` | 1 |
| Acacia bush | `default:acacia_bush_stem` | `default:acacia_bush_leaves` | `default:acacia_bush_sapling` | 1 |
| Pine bush | `default:pine_bush_stem` | `default:pine_bush_needles` | `default:pine_bush_sapling` | 1 |
| Blueberry bush | _(uses leaves as trunk)_ | `default:blueberry_bush_leaves` | `default:blueberry_bush_sapling` | 1 |

Blueberry bush has `default:blueberry_bush_leaves_with_berries` that drops `default:blueberries`.

#### Ores (7 types)

| Ore | In-Stone Node | Block Node | Raw Drop | Ingot |
|-----|--------------|------------|----------|-------|
| Coal | `default:stone_with_coal` | `default:coalblock` | `default:coal_lump` | _(burns as fuel)_ |
| Iron | `default:stone_with_iron` | `default:steelblock` | `default:iron_lump` | `default:steel_ingot` |
| Copper | `default:stone_with_copper` | `default:copperblock` | `default:copper_lump` | `default:copper_ingot` |
| Tin | `default:stone_with_tin` | `default:tinblock` | `default:tin_lump` | `default:tin_ingot` |
| Bronze | _(none, crafted)_ | `default:bronzeblock` | _(none)_ | `default:bronze_ingot` |
| Gold | `default:stone_with_gold` | `default:goldblock` | `default:gold_lump` | `default:gold_ingot` |
| Mese | `default:stone_with_mese` | `default:mese` | `default:mese_crystal` | _(crystal)_ |
| Diamond | `default:stone_with_diamond` | `default:diamondblock` | `default:diamond` | _(gem)_ |

#### Plants

| Node | Type | Groups | Behavior |
|------|------|--------|----------|
| `default:cactus` | on_dig: dig_up | choppy=3 | Grows on sand, max height 4 |
| `default:large_cactus_seedling` | sapling | _(timer growth)_ | Uses schematic |
| `default:papyrus` | on_dig: dig_up | snappy=3, flammable=2 | Grows on dirt near water, max height 4 |
| `default:dry_shrub` | plantlike | snappy=3 | Decorative |
| `default:junglegrass` | plantlike, waving | snappy=3, flora=1 | Drops cotton seed 1/8 |
| `default:grass_1` to `grass_5` | plantlike, waving | snappy=3, flora=1 | Drop grass_1; grass_1 drops wheat seed 1/5 |
| `default:dry_grass_1` to `dry_grass_5` | plantlike, waving | snappy=3, flora=1 | Drop dry_grass_1 |
| `default:fern_1` to `fern_3` | plantlike, waving | snappy=3, flora=1 | |
| `default:marram_grass_1` to `marram_grass_3` | plantlike, waving | snappy=3, flora=1 | |
| `default:sand_with_kelp` | plantlike_rooted, waving | snappy=3 | Underwater plant |

#### Corals

`default:coral_green`, `coral_pink`, `coral_cyan`, `coral_brown`, `coral_orange`, `coral_skeleton`

#### Liquids

| Node | Properties |
|------|-----------|
| `default:water_source` / `water_flowing` | alpha=160, liquid_viscosity=1, drowning=1, cools_lava=1 |
| `default:river_water_source` / `river_water_flowing` | Not renewable, liquid_range=2 |
| `default:lava_source` / `lava_flowing` | igniter=1, damage_per_second=4, liquid_viscosity=7, light_source=13 |

#### Crafted & Functional Nodes

| Node | Type | Groups | Function |
|------|------|--------|----------|
| `default:bookshelf` | container | choppy=3, flammable=3 | Holds 8x2 books |
| `default:sign_wall_wood` | wallmounted | choppy=2, attached_node=1 | Writable infotext |
| `default:sign_wall_steel` | wallmounted | cracky=2, attached_node=1 | Writable infotext |
| `default:ladder_wood` | climbable, wallmounted | choppy=2, flammable=2 | |
| `default:ladder_steel` | climbable, wallmounted | cracky=3 | |
| `default:glass` | glasslike_framed | cracky=3, oddly_breakable_by_hand=3 | Transparent |
| `default:obsidian_glass` | glasslike_framed | cracky=3 | Transparent, dark |
| `default:brick` | normal | cracky=3 | |
| `default:meselamp` | glasslike | cracky=3 | light_source=14 |
| `default:mese_post_light` | mesh | choppy=2 | light_source=14 (+ 4 wood variants) |
| `default:cloud` | normal | unbreakable | not_in_creative_inventory=1 |

#### Fences (5 wood types)

`default:fence_wood`, `fence_acacia_wood`, `fence_junglewood`, `fence_pine_wood`, `fence_aspen_wood`

Each has a matching `fence_rail_*` variant.

### 3.7 Tools

#### Hand (Override)

```lua
wield_scale = {x=1, y=1, z=2.5}
groupcaps = {
    crumbly = {times={[2]=3.0, [3]=0.7}, uses=0, maxlevel=1},
    snappy  = {times={[3]=0.4}, uses=0, maxlevel=1},
    oddly_breakable_by_hand = {times={[1]=3.5,[2]=2.0,[3]=0.7}, uses=0}
}
damage_groups = {fleshy=1}
```

#### Tool Tiers

Six materials, four tool types each (pick, shovel, axe, sword):

| Material | Pick `cracky` times | Uses | Shovel `crumbly` times | Axe `choppy` times | Sword `snappy` times | Sword `fleshy` damage |
|----------|-------------------|------|----------------------|-------------------|--------------------|--------------------|
| Wood | [3]=1.60 | 10 | [1]=3.0, [2]=1.6, [3]=0.6 | [2]=3.0, [3]=1.6 | [2]=1.6, [3]=0.4 | 2 |
| Stone | [2]=2.0, [3]=1.0 | 20 | [1]=1.8, [2]=1.2, [3]=0.5 | [1]=3.0, [2]=2.0, [3]=1.3 | [2]=1.4, [3]=0.4 | 4 |
| Bronze | [1]=4.5, [2]=1.8, [3]=0.9 | 20 | [1]=1.65, [2]=1.05, [3]=0.45 | [1]=2.75, [2]=1.7, [3]=1.15 | [1]=2.75, [2]=1.3, [3]=0.375 | 6 |
| Steel | [1]=4.0, [2]=1.6, [3]=0.8 | 20 | [1]=1.5, [2]=0.9, [3]=0.4 | [1]=2.5, [2]=1.4, [3]=1.0 | [1]=2.5, [2]=1.2, [3]=0.35 | 6 |
| Mese | [1]=2.4, [2]=1.2, [3]=0.6 | 20 | [1]=1.2, [2]=0.6, [3]=0.3 | [1]=2.2, [2]=1.0, [3]=0.6 | [1]=2.0, [2]=1.0, [3]=0.35 | 7 |
| Diamond | [1]=2.0, [2]=1.0, [3]=0.5 | 30 | [1]=1.1, [2]=0.5, [3]=0.3 | [1]=2.1, [2]=0.9, [3]=0.5 | [1]=1.9, [2]=0.9, [3]=0.3 | 8 |

#### Tool Craft Patterns

```
Pick:      M M M       Shovel:   M       Axe:  M M       Sword:  M
             S                   S              M S              M
             S                   S                S              S
```
(M = material, S = stick)

Wood tools have `flammable=2` group. All tools play `default_tool_breaks` on break.

### 3.8 Craftitems

| Item | Groups | Special |
|------|--------|---------|
| `default:blueberries` | food_blueberries=1 | Heals 2 HP |
| `default:book` | book=1, flammable=3 | Writable (on_use opens write form) |
| `default:book_written` | book=1, flammable=3 | Has title/text/owner/page_max fields |
| `default:stick` | stick=1 | Craft: 1 wood = 4 sticks |
| `default:paper` | flammable=3 | Craft: 3 papyrus = 4 paper |
| `default:coal_lump` | coal=1, flammable=1 | Fuel burntime=40 |
| `default:steel_ingot` | | Smelted from iron_lump |
| `default:copper_ingot` | | Smelted from copper_lump |
| `default:tin_ingot` | | Smelted from tin_lump |
| `default:bronze_ingot` | | Craft: 8 copper + 1 tin = 9 bronze |
| `default:gold_ingot` | | Smelted from gold_lump |
| `default:mese_crystal` | | Drops from mese ore |
| `default:mese_crystal_fragment` | | 1 mese_crystal = 9 fragments |
| `default:diamond` | | Drops from diamond ore |
| `default:clay_lump` | | Drops from clay node |
| `default:clay_brick` | | Smelted from clay_lump |
| `default:obsidian_shard` | | 1 obsidian = 9 shards |
| `default:flint` | | Drops from gravel (1/16) |
| `default:iron_lump` | | Drops from iron ore |
| `default:copper_lump` | | Drops from copper ore |
| `default:gold_lump` | | Drops from gold ore |
| `default:tin_lump` | | Drops from tin ore |

### 3.9 Key Crafting Recipes

```
Bronze ingots:   8 copper_ingot surrounding 1 tin_ingot = 9 bronze_ingot
Metal blocks:    9 ingots/crystals in 3x3 = 1 block
Block uncraft:   1 block = 9 ingots/crystals
Sandstone:       4 sand in 2x2 = 1 sandstone (works for all 3 sand types)
Sandstone split: 1 sandstone = 4 sand
Paper:           3 papyrus in row = 4 paper
Book:            3 paper vertical = 1 book
Bookshelf:       3 wood + 6 book + 3 wood = 1 bookshelf
Stick:           1 wood plank = 4 sticks
Torch:           1 coal_lump over 1 stick = 4 torches
Ladder (wood):   2 sticks | 3 sticks | 2 sticks = 5 ladders
Ladder (steel):  same pattern with steel_ingot = 15 ladders
Sign (wood):     6 wood + 1 stick = 3 signs
Sign (steel):    6 steel_ingot + 1 stick = 3 signs
Glass:           smelt group:sand
Brick:           4 clay_brick in 2x2 = 1 brick block
Meselamp:        1 glass + 1 mese_crystal = 1 meselamp
```

### 3.10 Key Smelting Recipes

| Input | Output | Cooktime |
|-------|--------|----------|
| `default:iron_lump` | `default:steel_ingot` | 3 |
| `default:copper_lump` | `default:copper_ingot` | 3 |
| `default:gold_lump` | `default:gold_ingot` | 3 |
| `default:tin_lump` | `default:tin_ingot` | 3 |
| `default:clay_lump` | `default:clay_brick` | 3 |
| `default:cobble` | `default:stone` | 3 |
| `group:sand` | `default:glass` | 3 |
| `default:obsidian_shard` | `default:obsidian_glass` | 3 |
| Various tools/blocks | Raw material (steel_ingot, etc.) | 3 |

### 3.11 Torch

Three node variants for placement orientation:
- `default:torch` -- floor (mesh: `torch_floor.obj`)
- `default:torch_wall` -- wall (mesh: `torch_wall.obj`)
- `default:torch_ceiling` -- ceiling (mesh: `torch_ceiling.obj`)

Properties: light_source=12, animated flame, floodable (drops on flood), groups: choppy=2, dig_immediate=3, flammable=1, attached_node=1, torch=1.

On place: auto-selects variant based on wallmounted direction.

### 3.12 Chests

**`default:chest`** -- 8x4 shared inventory. Mesh-based with animated lid open/close.
- Mesh: `chest_open.obj` (open), standard nodebox (closed)
- Sounds: `default_chest_open`, `default_chest_close`
- Craft: 8 wood in ring pattern

**`default:chest_locked`** -- Same but with owner protection. Supports keys.
- Craft: 8 wood + 1 steel_ingot center; or chest + steel_ingot shapeless

### 3.13 Furnace

**`default:furnace`** / **`default:furnace_active`** -- Node-timer based smelting machine.

Inventory: `src` (1 input), `fuel` (1 fuel), `dst` (4 output)

Active variant: animated front texture, light_source=8, plays `default_furnace_active` every 5s.

Craft: 8 cobblestone in ring pattern.

### 3.14 Mapgen (Biomes, Ores, Decorations)

The mapgen system registers all biomes, ore distributions, and decorations. Key biomes:

| Biome | Heat | Humidity | Surface | Depth |
|-------|------|----------|---------|-------|
| Icesheet | 0 | 73 | snowblock | 3 |
| Tundra | 0 | 40 | permafrost_with_stones | 1 |
| Taiga | 25 | 70 | dirt_with_snow | 1 |
| Snowy grassland | 20 | 35 | dirt_with_snow | 1 |
| Grassland | 50 | 35 | dirt_with_grass | 1 |
| Coniferous forest | 45 | 70 | dirt_with_coniferous_litter | 1 |
| Deciduous forest | 60 | 68 | dirt_with_grass | 1 |
| Desert | 92 | 16 | desert_sand | 1 |
| Sandstone desert | 60 | 0 | sandstone | 1 |
| Cold desert | 40 | 0 | silver_sand | 1 |
| Savanna | 89 | 42 | dry_dirt_with_dry_grass | 1 |
| Rainforest | 86 | 65 | dirt_with_rainforest_litter | 1 |

Ore depths (Y ranges, lower = deeper = rarer):

| Ore | Y Range | Clusters |
|-----|---------|----------|
| Coal | -31000 to 64 | scatter, clust_size=3 |
| Iron | -31000 to 2 | scatter, clust_size=3 |
| Copper | -31000 to -16 | scatter, clust_size=3 |
| Tin | -31000 to -32 | scatter, clust_size=3 |
| Gold | -31000 to -64 | scatter, clust_size=3 |
| Mese crystal | -31000 to -64 | scatter, clust_size=3 |
| Mese block | -31000 to -512 | scatter, rare |
| Diamond | -31000 to -128 | scatter, clust_size=3 |

### 3.15 Item Entity Override

Overrides `__builtin:item` to make flammable items burn in lava or near `group:igniter` nodes. Produces smoke particles and plays `default_item_smoke` sound.

---

## 4. Player System: `player_api`

### API Functions

```lua
player_api.register_model(name, def)     -- register player model with animations
player_api.set_model(player, model_name) -- set a player's model
player_api.set_textures(player, textures)-- set player textures
player_api.set_animation(player, anim_name, speed, loop) -- set current animation
player_api.get_animation(player)         -- get current animation data
player_api.player_attached              -- table tracking attached players (suppresses knockback)
```

### Default Model: `character.b3d`

Animation speed: 30

| Animation | Frames | Special Properties |
|-----------|--------|-------------------|
| `stand` | 0-79 | Default |
| `lay` | 162-166 | eye_height=0.3, collisionbox={-0.6,0,-0.6,0.6,0.3,0.6} |
| `walk` | 168-187 | |
| `mine` | 189-198 | |
| `walk_mine` | 200-219 | |
| `sit` | 81-160 | eye_height=0.8, collisionbox={-0.3,0,-0.3,0.3,1.0,0.3} |

Default properties: collisionbox={-0.3, 0.0, -0.3, 0.3, 1.7, 0.3}, stepheight=0.6, eye_height=1.47

---

## 5. Inventory System: `sfinv`

Tab-based inventory framework. Formspec size: 8x9.1.

### API

```lua
sfinv.register_page(name, def)  -- def needs `title` and `get(self, player, context)`
sfinv.override_page(name, def)  -- override existing page
sfinv.set_page(player, pagename)
sfinv.get_page(player)
sfinv.make_formspec(player, context, content, show_inv, size) -- wraps content with tabs + inventory
```

Default page: `sfinv:crafting` -- shows 3x3 crafting grid + output preview.

---

## 6. Creative Mode: `creative`

- Registers `creative` privilege
- Overrides `minetest.is_creative_enabled()` to check privilege
- Creative hand: digs everything instantly (digtime=42ms, maxlevel=256, range=10, fleshy=10)
- `register_on_placenode` returns true (items not consumed on place)
- `handle_node_drops` only picks up items not already in inventory
- Registers creative inventory sfinv page with search + category filtering

---

## 7. Craft Guide: `mtg_craftguide`

Registers sfinv page `mtg_craftguide:craftguide`:
- Shows all recipes with search and pagination (32 items/page)
- Recipe/usage toggle
- Cached recipes computed at mod load time
- Group stereotypes: `dye` -> `dye:white`, `wool` -> `wool:white`, etc.

---

## 8. Beds & Sleeping: `beds`

### API

```lua
beds.register_bed(name, def) -- registers 2-node bed (bottom + top half) with crafting recipe
```

### Registered Beds

| Bed | Craft | Recipe |
|-----|-------|--------|
| `beds:bed` | Simple bed | 3 wool:white top + 3 wood bottom |
| `beds:fancy_bed` | Fancy bed with posts | 1 stick + 3 wool:white + 3 wood |

### Mechanics

- Right-click to sleep; shows formspec with "Leave Bed" button
- Night skip when >50% of players are sleeping
- Day interval: start=0.2 (6:00 AM), finish=0.805 (7:18 PM)
- Respawn at bed position (configurable via `enable_bed_respawn`)
- Night skip configurable via `enable_bed_night_skip`

---

## 9. Boats: `boats`

Entity `boats:boat` with mesh `boats_boat.obj`, texture `default_wood.png`.

- Controls: up/down for speed, left/right for steering, up+down for cruise mode
- Physics: drag, buoyancy in water, gravity out of water
- Punch to pick up, right-click to mount/dismount
- Craft: 5 wood in boat shape (bottom row 3, middle 2 sides)

---

## 10. Death & Bones: `bones`

Node `bones:bones` placed at death location containing player's inventory.

### Modes (setting: `bones_mode`)

| Mode | Behavior |
|------|----------|
| `bones` (default) | Place bones node with inventory |
| `drop` | Drop items as entities |
| `keep` | Player keeps inventory |

### Timers

- `share_bones_time` = 1200s (20 min) before bones become public
- `share_bones_time_early` = 300s (5 min) in protected areas
- Punch to collect all items; formspec for manual retrieval

---

## 11. Bucket System: `bucket`

### API

```lua
bucket.register_liquid(source, flowing, itemname, inventory_image, name, groups, force_renew)
```

### Registered Buckets

| Item | Picks Up | Special |
|------|----------|---------|
| `bucket:bucket_empty` | _(none)_ | Craft: 3 steel_ingot in V shape |
| `bucket:bucket_water` | water_source | |
| `bucket:bucket_river_water` | river_water_source | force_renew=true |
| `bucket:bucket_lava` | lava_source | Fuel burntime=60, returns empty bucket |

---

## 12. Doors, Trapdoors, Gates: `doors`

### API

```lua
doors.register(name, def)           -- 4-variant door (a/b/c/d meshes), hidden top segment
doors.register_trapdoor(name, def)  -- nodebox trapdoor with open/closed states
doors.register_fencegate(name, def) -- mesh-based fence gate
doors.get(pos)                      -- returns object with open/close/toggle/state methods
```

### Registered Doors

| Door | Material | Groups | Protected |
|------|----------|--------|-----------|
| `doors:door_wood` | 6 wood planks | choppy=2, flammable=2 | No |
| `doors:door_steel` | 6 steel_ingot | cracky=1, level=2 | Yes |
| `doors:door_glass` | 6 glass | cracky=3 | No |
| `doors:door_obsidian_glass` | 6 obsidian_glass | cracky=3 | No |

### Trapdoors

| Trapdoor | Material | Protected |
|----------|----------|-----------|
| `doors:trapdoor` | 6 wood (output 2) | No |
| `doors:trapdoor_steel` | 4 steel_ingot | Yes |

### Fence Gates (5 wood types)

`doors:gate_wood`, `gate_acacia_wood`, `gate_junglewood`, `gate_pine_wood`, `gate_aspen_wood`

---

## 13. Dye System: `dye`

### 15 Colors

white, grey, dark_grey, black, violet, blue, cyan, dark_green, green, yellow, brown, orange, red, magenta, pink

Each dye: `dye:<color>`, groups: `{dye=1, color_<name>=1}`

### Dye Sources

- Flowers: 4 dye per flower via `group:flower,color_<name>` shapeless craft
- Coal lump: 4 black dye
- Blueberries: 2 violet dye

### Mixing Recipes (18 combinations)

Examples: red + yellow = orange, red + blue = violet, red + white = pink, white + black = grey, etc.

---

## 14. Wool: `wool`

15 wool nodes: `wool:<color>` for each dye color.

Groups: `snappy=2, choppy=2, oddly_breakable_by_hand=3, flammable=3, wool=1, color_<name>=1`

Craft: `group:dye,color_<name>` + `group:wool` shapeless = recolored wool.

---

## 15. Farming: `farming`

### API

```lua
farming.register_hoe(name, def)    -- tool that converts soil=1 nodes to farmland
farming.register_plant(name, def)  -- registers seed + harvest + growth steps
farming.hoe_on_use(itemstack, user, pointed_thing, uses) -- hoe use callback
farming.place_seed(itemstack, placer, pointed_thing, plantname) -- seed placement
farming.grow_plant(pos, elapsed)   -- timer callback for growth
farming.can_grow(pos)              -- checks light >= 13 for growth
```

### Soil Nodes

| Soil | Source | Wet Variant |
|------|--------|-------------|
| `farming:soil` | dirt | `farming:soil_wet` |
| `farming:dry_soil` | dry_dirt | `farming:dry_soil_wet` |
| `farming:desert_sand_soil` | desert_sand | `farming:desert_sand_soil_wet` |

ABM checks water proximity (radius 3, Y+2) to convert between wet/dry/base states. Interval=15, chance=4.

### Plants

| Plant | Seed | Growth Steps | Fertility | Min Light | Products |
|-------|------|-------------|-----------|-----------|----------|
| Wheat | `farming:seed_wheat` | 8 | grassland | 13 | `farming:wheat` |
| Cotton | `farming:seed_cotton` | 8 | grassland, desert | 13 | `farming:cotton` |

### Wheat Products

- Flour: 4 wheat shapeless
- Bread: cook flour (cooktime=15), heals 5 HP
- Straw: 9 wheat = 3 straw; 1 straw = 3 wheat; has stairs/slabs

### Cotton Products

- 4 cotton = 1 wool:white
- 2 cotton = 2 string (craftitem)

### Seed Sources

- Grass (1-5): drops wheat seed (1/5 chance)
- Junglegrass: drops cotton seed (1/8 chance)
- Wild cotton: decoration in savanna biome

### Hoes

| Hoe | Uses | Material |
|-----|------|----------|
| `farming:hoe_wood` | 30 | wood |
| `farming:hoe_stone` | 90 | stone |
| `farming:hoe_steel` | 500 | steel_ingot |

---

## 16. Fire: `fire`

### Nodes

| Node | Light | Damage | Behavior |
|------|-------|--------|----------|
| `fire:basic_flame` | 13 | 4/s | Timer-based; dies without flammable neighbor |
| `fire:permanent_flame` | 13 | 4/s | Permanent; placed on coalblock |

### Tool

`fire:flint_and_steel` -- 66 uses. Craft: flint + steel_ingot.

### ABMs (only when fire enabled via `enable_fire` setting)

| ABM | Interval | Chance | Effect |
|-----|----------|--------|--------|
| Ignite | 7 | 12 | Flammable near igniter -> basic_flame |
| Burn | 5 | 18 | basic_flame near flammable -> destroys flammable node |

### Sound System

Globalstep every 3s: searches for flames near each player, plays `fire_fire` loop with distance-based gain.

---

## 17. Flowers & Mushrooms: `flowers`

### 8 Flowers

All: plantlike, waving=1, attached_node=1, groups: snappy=3, flower=1, flora=1.

| Flower | Color Group |
|--------|------------|
| `flowers:rose` | color_red |
| `flowers:tulip` | color_orange |
| `flowers:dandelion_yellow` | color_yellow |
| `flowers:chrysanthemum_green` | color_green |
| `flowers:geranium` | color_blue |
| `flowers:viola` | color_violet |
| `flowers:dandelion_white` | color_white |
| `flowers:tulip_black` | color_black |

**Flower spread ABM**: interval=13, chance=300. Max ~7 flora per 9x9 area. Spreads to same soil type.

### 2 Mushrooms

| Mushroom | Heals | Groups |
|----------|-------|--------|
| `flowers:mushroom_red` | -5 (poison) | food_mushroom |
| `flowers:mushroom_brown` | +1 | food_mushroom |

**Mushroom spread ABM**: interval=11, chance=150. Only in darkness (light <= 3). Dies in full sunlight.

### Waterlily

`flowers:waterlily` / `waterlily_waving` -- nodebox on water surface with random facedir rotation.

---

## 18. Stairs & Slabs: `stairs`

### API

```lua
stairs.register_stair(subname, recipeitem, groups, images, desc, sounds, worldaligntex) -- output 8
stairs.register_slab(subname, ...)           -- output 6
stairs.register_stair_inner(subname, ...)    -- inner corner, output 7
stairs.register_stair_outer(subname, ...)    -- outer corner, output 6
stairs.register_stair_and_slab(subname, ...) -- all 4 at once
```

Stair placement uses intelligent rotation based on player position and face clicked.

### Registered For

All stone variants, all wood types, brick, metal blocks, ice, snowblock, glass, obsidian_glass, farming:straw.

---

## 19. TNT & Explosions: `tnt`

### API

```lua
tnt.boom(pos, def)        -- trigger explosion at pos
tnt.burn(pos)             -- ignite TNT at pos
tnt.register_tnt(def)     -- register custom TNT variant
```

### Nodes

| Node | Groups | Behavior |
|------|--------|----------|
| `tnt:tnt` | mesecon=2, tnt=1, flammable=5 | 4 second fuse when ignited |
| `tnt:tnt_burning` | falling_node=1 | Animated, 4s timer |
| `tnt:gunpowder` | raillike, flammable=1 | Connects and propagates ignition |
| `tnt:gunpowder_burning` | _(none)_ | 1s timer, propagates |
| `tnt:boom` | dig_immediate=3 | Temporary explosion marker |

### Explosion Mechanics

- Uses VoxelManip for performance
- Destroys nodes in sphere with configurable `tnt_radius` (default 3)
- `_tnt_loss` probability on nodes controls drop chance
- Replaces flammable nodes with fire
- Entity knockback and damage
- Calls `on_blast` on affected nodes

### Crafts (only when `enable_tnt` is on)

- Gunpowder: coal_lump + gravel = 5 gunpowder
- TNT stick: 6 gunpowder + paper = `tnt:tnt_stick`
- TNT: 9 tnt_sticks = 1 TNT

---

## 20. Vessels: `vessels`

| Item | Craft | Output Count |
|------|-------|-------------|
| `vessels:glass_bottle` | 5 glass | 10 |
| `vessels:drinking_glass` | 7 glass | 14 |
| `vessels:steel_bottle` | 5 steel_ingot | 5 |
| `vessels:shelf` | 6 wood + 3 group:vessel | 1 (holds 8x2 vessels only) |
| `vessels:glass_fragments` | 2 bottles or glasses | recycle (cooks back to glass) |

All vessel items: group `vessel=1`, `dig_immediate=3`, `attached_node=1`.

---

## 21. Walls: `walls`

### API

```lua
walls.register(name, desc, textures, material, sounds) -- connected nodebox, craft: 6 material = 6 walls
```

### Registered

- `walls:cobble` -- from cobblestone
- `walls:mossycobble` -- from mossycobble
- `walls:desertcobble` -- from desert_cobble

Connects to: `group:wall`, `group:stone`, `group:fence`, `group:wall_connected`

---

## 22. Glass Panes & Bars: `xpanes`

### API

```lua
xpanes.register_pane(name, def) -- connected nodebox with flat/connected variants
```

### Registered

| Pane | Material | Output |
|------|----------|--------|
| `xpanes:pane` (glass) | 6 glass | 16 |
| `xpanes:obsidian_pane` | 6 obsidian_glass | 16 |
| `xpanes:bar` (steel bars) | 6 steel_ingot | 16 |

Also registers steel bar door and trapdoor if doors mod is present.

---

## 23. Screwdriver: `screwdriver`

`screwdriver:screwdriver` -- 200 uses. Left-click rotates face, right-click rotates axis.

Handles `facedir`, `4dir`, `wallmounted`, `colorfacedir`, `colorwallmounted` paramtype2 values.

Craft: steel_ingot + stick (vertical).

### API

```lua
screwdriver.ROTATE_FACE = 1
screwdriver.ROTATE_AXIS = 2
screwdriver.handler(itemstack, user, pointed_thing, mode, uses)
screwdriver.disallow      -- callback to prevent rotation
screwdriver.rotate_simple -- callback for simple rotation only
```

Nodes can define `on_rotate` callback. Return `false` to prevent, or use `screwdriver.disallow`/`screwdriver.rotate_simple`.

---

## 24. Keys: `keys`

- `keys:skeleton_key` -- use on lockable node to create a bound key
- `keys:key` -- bound to specific node via secret, group: key=1
- Craft: gold_ingot pattern

Works with locked chests, steel doors, steel trapdoors via `on_skeleton_key_use` / `on_key_use` callbacks in node definitions.

---

## 25. Map & Minimap: `map`

`map:mapping_kit` -- enables minimap HUD when in player's inventory. stack_max=1.

- Creative mode: minimap always on + radar
- Survival: only minimap when kit is in inventory
- Craft: glass + 3 paper + 2 steel_ingot + stick + wood + dye:black

---

## 26. Binoculars: `binoculars`

`binoculars:binoculars` -- enables zoom (10 FOV) when in inventory. Creative mode gets 15 FOV.

Craft: 4 obsidian_glass + 3 bronze_ingot.

---

## 27. Initial Stuff: `give_initial_stuff`

When `give_initial_stuff=true`, gives new players items from `initial_stuff` setting.

Default: `default:pick_steel, default:axe_steel, default:shovel_steel, default:torch 99, default:cobble 99`

### API

```lua
give_initial_stuff.add(stack)         -- add item to initial stuff
give_initial_stuff.clear()            -- clear all initial stuff
give_initial_stuff.add_from_csv(str)  -- add from comma-separated string
```

---

## 28. Spawn System: `spawn`

Searches for suitable spawn biome in spiral pattern from world origin.

Suitable biomes: taiga, coniferous_forest, deciduous_forest, grassland, savanna.

Resolution: 64 nodes. Search area: 128x128 checks. Uses `minetest.get_spawn_level()`.

### API

```lua
spawn.get_default_pos()           -- returns calculated spawn position
spawn.add_suitable_biome(biome)   -- add biome name to suitable list
```

---

## 29. Chat Commands: `game_commands`

Registers `/killme` command -- kills player (sets HP=0) if damage enabled, else calls respawn callbacks.

---

## 30. Home Teleport: `sethome`

- `/sethome` -- saves current position (stored in player meta)
- `/home` -- teleports to saved position
- Requires `home` privilege (not granted to singleplayer by default)

---

## 31. Environmental Sounds: `env_sounds`

Plays ambient sounds based on nearby liquid nodes within radius=8. Cycle every 3.5s.

| Sound | Triggered By |
|-------|-------------|
| `env_sounds_water` | water_flowing, river_water_flowing |
| `env_sounds_lava` | lava_source, lava_flowing |

---

## 32. Butterflies: `butterflies`

3 colors: white, red, violet. Animated plantlike nodes.

- Hide during night (light < 11) via hidden variant
- Group: `catchable=1` (caught by bug net)
- Decoration: placed on dirt_with_grass in grassland/deciduous_forest biomes

---

## 33. Fireflies: `fireflies`

- `fireflies:firefly` -- animated plantlike, light_source=6, hides during day (light > 11)
- `fireflies:bug_net` -- tool for catching `group:catchable` nodes. Craft: 4 farming:string + stick
- `fireflies:firefly_bottle` -- light_source=9, vessel group. Right-click to release. Craft: firefly + glass_bottle
- Decoration: deciduous/coniferous/rainforest biomes

---

## 34. Minecarts: `carts`

Entity `carts:cart` with rail-following physics.

- Speed max: 7, punch speed max: 5
- Rail types with powered/brake variants
- Cart entity handles rail path finding, player attachment, detachment

---

## 35. Dungeon Loot: `dungeon_loot`

Spawns 0-2 chests in engine-generated dungeons with up to 8 random item stacks.

### API

```lua
dungeon_loot.register({
    name = "item:name",
    chance = 0.5,      -- probability 0-1
    count = {1, 3},    -- random count range
    types = {"normal"}, -- dungeon types: "normal", "sandstone", "desert"
    y = {-31000, 0},   -- depth range
})
```

Various mods register their own loot (bucket, carts, farming, vessels).

---

## 36. Weather: `weather`

Uses Perlin noise over game time to vary cloud density/thickness/speed.

Cloud density influenced by biome humidity. Controls:
- Shadow intensity: `0.7 * (1 - density)`
- Bloom intensity
- Volumetric light

Disabled for V6/singlenode mapgens. Uses mod_storage for time offset persistence.

### API

```lua
weather.get(player) -- returns {clouds={density,thickness,speed}, lighting={shadows,bloom,volumetric_light}}
```

---

## 37. Asset Naming Conventions

### Textures

| Category | Pattern | Examples |
|----------|---------|---------|
| Nodes | `<modname>_<nodename>.png` | `default_stone.png`, `default_cobble.png` |
| Side textures | `<modname>_<nodename>_<face>.png` | `default_furnace_top.png`, `default_furnace_front.png` |
| Tools | `default_tool_<material><type>.png` | `default_tool_steelpick.png` |
| Items | `default_<itemname>.png` | `default_diamond.png`, `default_steel_ingot.png` |
| GUI | `gui_<element>.png` | `gui_formbg.png`, `gui_hotbar.png`, `gui_hotbar_selected.png` |
| Animated | `<name>.png` with animation def | `default_furnace_front_active.png` |
| Overlays | Use `^` composition | `default_dirt.png^default_grass_side.png` |

### Sounds

Pattern: `<modname>_<name>.ogg` (Luanti auto-appends `.ogg`)

| Category | Sound Names |
|----------|------------|
| Footsteps | `default_hard_footstep`, `default_dirt_footstep`, `default_sand_footstep`, `default_gravel_footstep`, `default_wood_footstep`, `default_grass_footstep`, `default_snow_footstep`, `default_glass_footstep`, `default_ice_footstep`, `default_metal_footstep`, `default_water_footstep` |
| Digging | `default_dig_crumbly`, `default_dig_choppy`, `default_dig_metal`, `default_ice_dig`, `default_gravel_dig` |
| Dug | `default_dug_node`, `default_dug_metal`, `default_gravel_dug`, `default_ice_dug` |
| Placing | `default_place_node`, `default_place_node_hard`, `default_place_node_metal` |
| Special | `default_cool_lava`, `default_break_glass`, `default_tool_breaks`, `default_chest_open`, `default_chest_close`, `default_furnace_active`, `default_item_smoke` |
| Fire | `fire_flint_and_steel`, `fire_extinguish_flame`, `fire_fire` |
| TNT | `tnt_ignite`, `tnt_explode`, `tnt_gunpowder_burning` |
| Doors | `doors_door_open`, `doors_door_close`, `doors_steel_door_open/close`, `doors_glass_door_open/close`, `doors_fencegate_open/close` |

---

## 38. Model & Schematic Files

### 3D Models

| File | Used By |
|------|---------|
| `character.b3d` | Player model (player_api) |
| `chest_open.obj` | Open chest (default) |
| `torch_floor.obj`, `torch_wall.obj`, `torch_ceiling.obj` | Torch variants (default) |
| `boats_boat.obj` | Boat entity (boats) |
| `door_a.b3d`, `door_b.b3d` | Door variants (doors) |
| `doors_fencegate_closed.obj`, `doors_fencegate_open.obj` | Fence gates (doors) |
| `carts_cart.b3d` | Minecart entity (carts) |

### Schematics (MTS files)

All in `mods/default/schematics/`:

| File | Used By |
|------|---------|
| `apple_tree_from_sapling.mts` | Apple tree growth |
| `jungle_tree_from_sapling.mts` | Jungle tree growth |
| `emergent_jungle_tree_from_sapling.mts` | Tall jungle tree |
| `pine_tree_from_sapling.mts` | Pine tree growth |
| `small_pine_tree_from_sapling.mts` | Small pine variant |
| `snowy_pine_tree_from_sapling.mts` | Snowy pine |
| `snowy_small_pine_tree_from_sapling.mts` | Snowy small pine |
| `acacia_tree_from_sapling.mts` | Acacia tree growth |
| `aspen_tree_from_sapling.mts` | Aspen tree growth |
| `bush.mts` | Bush growth |
| `blueberry_bush.mts` | Blueberry bush growth |
| `acacia_bush.mts` | Acacia bush growth |
| `pine_bush.mts` | Pine bush growth |
| `large_cactus.mts` | Large cactus growth |

---

## 39. Settings Reference

All game-specific settings (defined in `settingtypes.txt`):

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `creative_mode` | bool | false | Enables creative inventory and instant digging |
| `enable_fire` | enum(auto/true/false) | auto | Fire spreading (auto = singleplayer only) |
| `flame_sound` | bool | true | Enable flame crackling sound |
| `enable_lavacooling` | bool | true | Lava converts to obsidian/stone near water |
| `give_initial_stuff` | bool | false | Give starter tools to new players |
| `enable_bed_respawn` | bool | true | Respawn at last-used bed |
| `enable_bed_night_skip` | bool | true | Skip night when all players sleep |
| `enable_fence_tall` | bool | false | Tall fences (prevents jumping over) |
| `enable_tnt` | enum(auto/true/false) | auto | TNT enabled (auto = singleplayer only) |
| `tnt_radius` | int | 3 | TNT blast radius |
| `bones_mode` | enum(bones/drop/keep) | bones | What happens to inventory on death |
| `share_bones_time` | int | 1200 | Seconds before bones become public |
| `share_bones_time_early` | int | 300 | Earlier share time in protected areas |
| `bones_position_message` | bool | false | Tell player where their bones are |
| `enable_stairs_replace_abm` | bool | false | Replace old-format stairs |
| `engine_spawn` | bool | false | Use engine's spawn search instead of mod |
| `river_source_sounds` | bool | false | Play sounds for river water sources |
| `enable_weather` | bool | true | Weather affects clouds and shadows |
| `log_non_player_actions` | bool | false | Log non-player inventory actions |

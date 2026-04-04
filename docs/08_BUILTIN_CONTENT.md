# AgentWorld - Built-in Content

The base set of Objects and Actions that ship with the game. These follow the exact same API as player-created content -- they're just pre-installed.

---

## 1. Built-in Objects

### Terrain Blocks

| Object ID | Type | Groups | Properties |
|-----------|------|--------|------------|
| `base:air` | Passive | — | Invisible, non-solid |
| `base:stone` | Passive | cracky=3, stone=1 | Drops cobblestone |
| `base:cobblestone` | Passive | cracky=3, stone=2 | Craftable from stone |
| `base:dirt` | Passive | crumbly=3, soil=1, spreadable=1 | Has `grass_level`, `moisture` |
| `base:sand` | Passive | crumbly=3, falling=1, sand=1 | Gravity-affected |
| `base:gravel` | Passive | crumbly=2, falling=1 | 10% chance drops flint |
| `base:clay` | Passive | crumbly=3 | Drops 4x clay lump |
| `base:bedrock` | Passive | unbreakable=1 | Indestructible |
| `base:snow` | Passive | crumbly=3, snowy=1 | Half-height |
| `base:ice` | Passive | cracky=3, slippery=3 | Low friction |

### Stone & Ore Variants

| Object ID | Groups | Drop | Depth Range |
|-----------|--------|------|-------------|
| `base:coal_ore` | cracky=3 | coal_lump | -256 to 64 |
| `base:iron_ore` | cracky=3 | iron_lump | -512 to 2 |
| `base:copper_ore` | cracky=3 | copper_lump | -512 to -16 |
| `base:gold_ore` | cracky=2 | gold_lump | -1024 to -64 |
| `base:diamond_ore` | cracky=1 | diamond | -2048 to -128 |
| `base:mese_ore` | cracky=2 | mese_crystal | -1024 to -64 |

### Wood & Plants

| Object ID | Type | Properties |
|-----------|------|------------|
| `base:oak_log` | Passive | choppy=2, tree=1 |
| `base:oak_planks` | Passive | choppy=2, wood=1 |
| `base:oak_leaves` | Passive | snappy=3, leafdecay, 5% drops sapling |
| `base:oak_sapling` | Active | Timer-based growth (5-10 min), needs light |
| `base:birch_log/planks/leaves/sapling` | — | Same pattern |
| `base:pine_log/planks/needles/sapling` | — | Same pattern |
| `base:jungle_log/planks/leaves/sapling` | — | Same pattern |
| `base:cactus` | Active | Grows upward on sand, damages on touch |
| `base:tall_grass` | Passive | snappy=3, flora=1, waving, drops seeds |
| `base:flower_red/yellow/blue/white` | Passive | snappy=3, flower=1, dye source |
| `base:mushroom_brown` | Passive | food, heals 1 |
| `base:mushroom_red` | Passive | poison, heals -5 |

### Liquids

| Object ID | Type | Properties |
|-----------|------|------------|
| `base:water` | Active (Fluid) | Flows, range=8, drowning=1, cools lava |
| `base:lava` | Active (Fluid) | Flows, range=4, damage=4/s, ignites, light=13 |

### Crafted Blocks

| Object ID | Recipe Summary | Properties |
|-----------|---------------|------------|
| `base:glass` | Smelt sand | cracky=3, transparent |
| `base:brick` | 4x clay brick | cracky=3 |
| `base:bookshelf` | 6 planks + 3 books | choppy=3, holds books |
| `base:chest` | 8 planks | Container, 32 slots |
| `base:furnace` | 8 cobblestone | Smelting, fuel+input+output |
| `base:crafting_table` | 4 planks | 3x3 crafting grid |
| `base:torch` | coal + stick | Light=12, wallmounted |
| `base:ladder` | 7 sticks | Climbable, wallmounted |
| `base:door_wood` | 6 planks | Openable |
| `base:door_iron` | 6 iron ingot | Openable, lockable |
| `base:bed` | 3 wool + 3 planks | Sleep, set spawn |
| `base:tnt` | 5 gunpowder + 4 sand | Explosive |

### Animals (Active, Living)

| Object ID | HP | Behavior | Drops |
|-----------|-----|----------|-------|
| `base:pig` | 10 | Wanders, eats grass, flees when hit | raw_pork (1-3) |
| `base:cow` | 15 | Wanders, eats grass, can be milked | raw_beef (1-3), leather (0-2) |
| `base:sheep` | 10 | Wanders, eats grass, regrows wool | wool (1), raw_mutton (1-2) |
| `base:chicken` | 5 | Wanders, lays eggs periodically | feather (0-2), raw_chicken (1) |
| `base:rabbit` | 4 | Wanders, runs fast, shy | rabbit_hide (0-1), raw_rabbit (1) |
| `base:bee` | 3 | Flies near flowers, pollinates, stings if hit | — |

### Monsters (Active, Living)

| Object ID | HP | Behavior | Drops | Spawns |
|-----------|-----|----------|-------|--------|
| `base:zombie` | 20 | Chases players at night, burns in sun | rotten_flesh (0-2) | Dark, surface |
| `base:skeleton` | 15 | Ranged attack (arrows), avoids sun | bone (0-2), arrow (0-2) | Dark, surface |
| `base:spider` | 12 | Neutral in day, hostile at night, climbs | string (0-2), spider_eye (0-1) | Dark |
| `base:slime` | 8 | Bounces toward players, splits on death | slime_ball (0-2) | Underground |

### NPCs (Active, Living)

| Object ID | HP | Behavior |
|-----------|-----|----------|
| `base:villager` | 20 | Wanders in village, trades items |
| `base:merchant` | 20 | Stationary, offers buy/sell trades |

### Items & Materials (Craftitems)

```
Raw materials:   coal_lump, iron_lump, copper_lump, gold_lump,
                 diamond, mese_crystal, clay_lump, flint
Ingots:          iron_ingot, copper_ingot, gold_ingot, bronze_ingot
Processed:       stick, paper, book, brick, glass_bottle
Food:            bread, apple, raw_pork, cooked_pork, raw_beef,
                 cooked_beef, raw_chicken, cooked_chicken
Other:           string, leather, feather, bone, slime_ball,
                 gunpowder, arrow, wool (16 colors), dye (16 colors)
```

### Tools

| Tool | Materials | Tiers (uses / speed) |
|------|-----------|---------------------|
| Pickaxe | wood/stone/iron/gold/diamond | 60/120/250/30/1500 |
| Shovel | wood/stone/iron/gold/diamond | 60/120/250/30/1500 |
| Axe | wood/stone/iron/gold/diamond | 60/120/250/30/1500 |
| Sword | wood/stone/iron/gold/diamond | 60/120/250/30/1500 |
| Hoe | wood/stone/iron | 60/120/250 |
| Bucket | iron | Picks up/places liquids |
| Flint & Steel | flint + iron | 64 uses, starts fire |
| Shears | 2x iron | 200 uses, shears sheep |

---

## 2. Built-in Actions

### Player Input Actions

| Action ID | Trigger | Effect |
|-----------|---------|--------|
| `base:mine` | Left-click block | Break block, drop item |
| `base:place` | Right-click with block | Place block at target |
| `base:attack` | Left-click entity | Deal damage based on held weapon |
| `base:interact` | Right-click entity/block | Open chest, trade with NPC, etc. |
| `base:eat` | Right-click with food | Consume food, restore hunger/hp |
| `base:use_item` | Right-click with tool | Tool-specific action |
| `base:drop_item` | Q key | Drop held item as entity |
| `base:craft` | In crafting UI | Combine items per recipe |
| `base:smelt` | Furnace processes | Cook/smelt with fuel |

### Entity Actions

| Action ID | Trigger | Effect |
|-----------|---------|--------|
| `base:eat_grass` | Mob step | Reduce grass_level on dirt |
| `base:mob_attack` | Mob step | Mob deals damage to target |
| `base:mob_flee` | Mob step | Mob moves away from threat |
| `base:lay_egg` | Chicken step | Spawn egg item every 5-10 min |
| `base:pollinate` | Bee step | Boost flower growth nearby |
| `base:zombie_burn` | Zombie step | Take damage in sunlight |

### World/System Actions

| Action ID | Trigger | Effect |
|-----------|---------|--------|
| `base:grass_spread` | ABM, 6s/50 | Dirt near grass becomes grassy |
| `base:grass_die` | ABM, 8s/50 | Covered grass reverts to dirt |
| `base:leaf_decay` | Timer | Leaves far from trunk disappear |
| `base:sapling_grow` | Timer | Sapling becomes tree (schematic) |
| `base:fire_spread` | ABM, 7s/12 | Fire spreads to flammable neighbors |
| `base:fire_burn` | ABM, 5s/18 | Fire destroys flammable blocks |
| `base:lava_cool` | ABM, 2s/2 | Lava near water -> obsidian/stone |
| `base:liquid_flow` | System, 0.5s | Water/lava spreading |
| `base:cactus_grow` | ABM, 12s/83 | Cactus grows upward |

### Item Actions

| Action ID | Trigger | Effect |
|-----------|---------|--------|
| `base:tnt_ignite` | Flint & steel / fire | Start 4s fuse |
| `base:tnt_explode` | Timer (fuse) | Destroy blocks in radius, damage entities |
| `base:bucket_fill` | Right-click liquid | Pick up liquid source |
| `base:bucket_pour` | Right-click with bucket | Place liquid source |
| `base:shear_sheep` | Right-click sheep | Get wool, sheep loses coat |
| `base:milk_cow` | Right-click cow with bucket | Get milk bucket |

---

## 3. Crafting Recipes

### Tool Recipes (pattern: same as Minetest Game)

```
Pickaxe:   MMM    Shovel:  M     Axe:   MM    Sword:  M
            S              S            MS             M
            S              S             S             S
(M = material, S = stick)
```

### Block Recipes

```
Planks:        1 log = 4 planks
Sticks:        2 planks vertical = 4 sticks
Crafting table: 4 planks in 2x2
Chest:         8 planks ring
Furnace:       8 cobblestone ring
Torch:         coal over stick = 4
Ladder:        H pattern with 7 sticks = 3
Door (wood):   6 planks in 2x3 = 1
Door (iron):   6 iron ingot in 2x3 = 1
Bed:           3 wool top + 3 planks bottom
Glass:         smelt sand
Brick block:   4 bricks in 2x2
Bookshelf:     3 planks + 3 books + 3 planks
Paper:         3 sugar cane in row = 3
Book:          3 paper vertical = 1
TNT:           5 gunpowder (X) + 4 sand (corners)
Gunpowder:     coal + gravel = 2
```

### Smelting Recipes

```
iron_lump    -> iron_ingot     (10s)
copper_lump  -> copper_ingot   (10s)
gold_lump    -> gold_ingot     (10s)
clay_lump    -> brick          (10s)
sand         -> glass          (10s)
cobblestone  -> stone          (10s)
raw_pork     -> cooked_pork    (5s)
raw_beef     -> cooked_beef    (5s)
raw_chicken  -> cooked_chicken (5s)

Fuels:
  coal_lump    = 80s burn time
  planks       = 15s
  stick        = 5s
  coal_block   = 800s
  lava_bucket  = 1000s
```

---

## 4. Biomes & Mapgen

### Biome Table

| Biome | Heat | Humidity | Surface | Trees/Deco |
|-------|------|----------|---------|------------|
| Tundra | 0-15 | 20-50 | snow + permafrost | — |
| Taiga | 15-30 | 50-80 | snow + dirt | Pine trees |
| Grassland | 40-60 | 30-50 | dirt + grass | Oak trees, flowers |
| Forest | 50-70 | 50-70 | dirt + grass | Oak, birch, dense |
| Rainforest | 75-100 | 70-100 | dirt + litter | Jungle trees, dense |
| Savanna | 70-90 | 20-40 | dry dirt | Acacia trees, dry grass |
| Desert | 80-100 | 0-20 | sand | Cactus, dead shrubs |
| Mountains | any | any | stone | Sparse pine at peaks |
| Ocean | any | any | sand/gravel | Kelp, coral |
| Underground | — | — | stone | Caves, ores, lava |

### Ore Distribution

```
Depth (Y)
  64  |  coal ============================
   0  |  iron =====================
 -16  |  copper ================
 -64  |  gold ==========
 -64  |  mese ==========
-128  |  diamond ======
      +------------------------------------
         rarity ->  common          rare
```

---

## 5. Day/Night Cycle

```
time_of_day:  0.0          0.25         0.5          0.75         1.0
              midnight     sunrise      noon         sunset       midnight

Light level:  0            0->14        14           14->0        0
Monster:      spawn        burn/flee    hide         spawn        spawn
Animal:       sleep        wake         active       sleep        sleep
```

Default day length: 1200 real seconds (20 minutes).

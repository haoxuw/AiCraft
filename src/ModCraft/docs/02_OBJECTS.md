# ModCraft - Objects

Everything in the world is an Object. Objects are defined as Python classes with Pydantic models for their attributes.

---

## 1. Object Hierarchy

```
Object (abstract base)
  |
  |-- PassiveObject
  |     Does NOT step. Only reacts when acted upon.
  |     Examples: dirt, stone, wood plank, chest, crafting table
  |
  |-- ActiveObject
        Has a step() method called every server tick.
        Can emit Actions, change its own state, move.
        |
        |-- LivingObject
        |     Has HP, can die. Mobs, players, NPCs.
        |     |
        |     |-- PlayerObject
        |     |     Controlled by a human. Has inventory, hotbar.
        |     |
        |     |-- MobObject
        |     |     AI-driven. Pig, sheep, zombie, dragon.
        |     |
        |     |-- NPCObject
        |           Scripted behavior. Villager, merchant, quest-giver.
        |
        |-- FluidObject
        |     Flows/spreads. Water, lava.
        |
        |-- EffectObject
        |     Temporary. Fire, smoke, potion aura, explosion shockwave.
        |
        |-- ItemEntity
              Dropped item on ground. Ticks (bobbing, despawn timer).
```

---

## 2. Object Definition (Python)

Every object type is a Python file with a class that extends `PassiveObject` or `ActiveObject`.

### 2.1 PassiveObject Example: Dirt Block

```python
# artifacts/objects/dirt.py
from modcraft.api import PassiveObject, BlockPos, Attribute, ObjectMeta

class DirtBlock(PassiveObject):
    """A basic dirt block. Can have grass on top."""

    meta = ObjectMeta(
        id="base:dirt",
        display_name="Dirt",
        category="terrain",
        texture="dirt.png",
        texture_top="dirt_top.png",       # optional per-face textures
        texture_side="dirt_side.png",
        hardness=1.0,                      # time to mine (seconds)
        tool_group="shovel",               # best tool type
        drop="base:dirt",                  # what drops when mined (self by default)
        stack_max=64,
        sounds=SoundSet(
            footstep="dirt_footstep",
            dig="dig_crumbly",
            place="place_node",
        ),
        groups={"crumbly": 3, "soil": 1, "spreadable": 1},
        tags={"natural", "terrain"},
    )

    # --- Attributes (Pydantic fields) ---
    grass_level: float = Attribute(default=0.0, min=0.0, max=1.0,
                                   description="How much grass covers the top")
    moisture: float = Attribute(default=0.5, min=0.0, max=1.0,
                                description="Water content")
    fertility: int = Attribute(default=1, min=0, max=3,
                               description="How well plants grow")

    # --- PassiveObjects have no step(), but can define reactions ---
    def on_place(self, world: WorldView, pos: BlockPos, placer: PlayerObject):
        """Called when a player places this block."""
        pass  # default behavior is fine

    def on_dig(self, world: WorldView, pos: BlockPos, digger: PlayerObject):
        """Called when this block is mined."""
        # Chance to drop a worm
        if self.moisture > 0.7 and random() < 0.05:
            world.spawn_entity(pos.to_vec3(), "base:worm")

    def on_neighbor_changed(self, world: WorldView, pos: BlockPos, neighbor_pos: BlockPos):
        """Called when an adjacent block changes."""
        above = world.get_block(pos.offset(0, 1, 0))
        if above and above.meta.groups.get("opaque"):
            self.grass_level = 0.0  # grass dies under solid block
```

### 2.2 ActiveObject Example: Pig

```python
# artifacts/objects/pig.py
from modcraft.api import (
    LivingObject, ActiveObject, ObjectMeta, Attribute,
    Vec3, WorldView, LootTable, LootEntry
)
import random

class Pig(LivingObject):
    """A passive animal that wanders, eats, and drops pork."""

    meta = ObjectMeta(
        id="base:pig",
        display_name="Pig",
        category="animal",
        model="pig.gltf",
        texture="pig.png",
        collision_box=(-0.4, 0.0, -0.4, 0.4, 0.9, 0.4),
        eye_height=0.7,
        max_hp=10,
        armor=0,
        walk_speed=2.0,
        run_speed=5.0,
        groups={"animal": 1, "flammable": 1},
        loot=LootTable([
            LootEntry("base:raw_pork", count=(1, 3), chance=1.0),
        ]),
        sounds=SoundSet(
            idle="pig_idle",
            hurt="pig_hurt",
            death="pig_death",
        ),
        spawn_biomes=["grassland", "forest"],
        spawn_chance=0.02,
        spawn_light_min=8,
    )

    # --- Attributes ---
    hunger: float = Attribute(default=0.5, min=0.0, max=1.0)
    age: float = Attribute(default=0.0, min=0.0, description="Seconds alive")
    wander_target: Optional[Vec3] = None
    panic_timer: float = 0.0

    # --- Behavior (called every server tick) ---
    def step(self, dt: float, world: WorldView):
        self.age += dt

        # Panic behavior (after being hit)
        if self.panic_timer > 0:
            self.panic_timer -= dt
            self.move_away_from_threat(world, dt)
            return

        # Hunger drives behavior
        self.hunger -= dt * 0.01
        if self.hunger < 0.3:
            self.find_and_eat_grass(world, dt)
        else:
            self.wander(world, dt)

    def wander(self, world: WorldView, dt: float):
        if self.wander_target is None or self.pos.distance(self.wander_target) < 0.5:
            # Pick a random point within 8 blocks
            offset = Vec3(
                random.uniform(-8, 8),
                0,
                random.uniform(-8, 8),
            )
            self.wander_target = self.pos + offset
        self.move_toward(self.wander_target, self.meta.walk_speed, dt)

    def find_and_eat_grass(self, world: WorldView, dt: float):
        # Find nearest grass block
        below = world.get_block(self.pos.to_block_pos().offset(0, -1, 0))
        if below and below.grass_level > 0.3:
            # Eat the grass
            world.emit_action("base:eat_grass",
                eater=self.entity_id,
                block_pos=self.pos.to_block_pos().offset(0, -1, 0))
            self.hunger = min(1.0, self.hunger + 0.3)
        else:
            # Wander toward grass
            grass_blocks = [
                (pos, b) for pos, b in
                world.get_blocks_in_radius(self.pos.to_block_pos(), 8)
                if hasattr(b, 'grass_level') and b.grass_level > 0.3
            ]
            if grass_blocks:
                nearest = min(grass_blocks, key=lambda x: self.pos.distance(x[0].to_vec3()))
                self.move_toward(nearest[0].to_vec3(), self.meta.walk_speed, dt)

    def on_hit(self, world: WorldView, attacker: 'ActiveObject', damage: float):
        """React to being attacked."""
        self.hp -= damage
        self.panic_timer = 5.0  # run for 5 seconds
        self.threat_pos = attacker.pos
        world.play_sound("pig_hurt", self.pos)
        if self.hp <= 0:
            self.die(world)

    def move_away_from_threat(self, world: WorldView, dt: float):
        if self.threat_pos:
            direction = (self.pos - self.threat_pos).normalized()
            target = self.pos + direction * 10
            self.move_toward(target, self.meta.run_speed, dt)
```

### 2.3 ActiveObject Example: Water

```python
# artifacts/objects/water.py
from modcraft.api import FluidObject, ObjectMeta, Attribute, BlockPos, WorldView

class Water(FluidObject):
    """Flowing water. Spreads horizontally, falls with gravity."""

    meta = ObjectMeta(
        id="base:water",
        display_name="Water",
        category="liquid",
        texture="water.png",
        texture_animated=True,
        animation_frames=16,
        alpha=0.6,                  # transparency
        liquid_viscosity=1,
        drowning_damage=1,          # per second when submerged
        groups={"liquid": 1, "water": 1, "cools_lava": 1},
        flow_range=8,               # max blocks from source
        sounds=SoundSet(
            footstep="water_footstep",
        ),
    )

    level: int = Attribute(default=8, min=0, max=8,
                           description="Fluid level, 8=source")
    is_source: bool = True

    def step(self, dt: float, world: WorldView):
        if not self.is_source:
            # Flowing water: check if source still exists
            if not self.has_source_neighbor(world):
                world.set_block(self.pos, None)  # evaporate
                return

        # Try to flow downward first
        below = self.pos.offset(0, -1, 0)
        below_block = world.get_block(below)
        if below_block is None or below_block.meta.id == "base:air":
            world.set_block(below, Water(level=8, is_source=False))
            return

        # Then spread horizontally
        if self.level > 1:
            for dx, dz in [(1,0), (-1,0), (0,1), (0,-1)]:
                neighbor_pos = self.pos.offset(dx, 0, dz)
                neighbor = world.get_block(neighbor_pos)
                if neighbor is None or neighbor.meta.id == "base:air":
                    world.set_block(neighbor_pos,
                        Water(level=self.level - 1, is_source=False))
                # Lava interaction
                elif neighbor.meta.groups.get("lava"):
                    world.set_block(neighbor_pos,
                        world.registry.create("base:obsidian"))
                    world.play_sound("cool_lava", neighbor_pos.to_vec3())
                    world.emit_particles(SteamParticles(neighbor_pos))
```

### 2.4 ActiveObject Example: Player

```python
# artifacts/objects/player.py
from modcraft.api import PlayerObject, ObjectMeta, Attribute, Inventory

class Player(PlayerObject):
    """The player-controlled entity."""

    meta = ObjectMeta(
        id="base:player",
        display_name="Player",
        category="player",
        model="character.gltf",
        texture="character_default.png",
        collision_box=(-0.3, 0.0, -0.3, 0.3, 1.7, 0.3),
        eye_height=1.47,
        max_hp=20,
        walk_speed=4.0,
        run_speed=6.0,
        jump_height=1.2,
    )

    # --- Player-specific attributes ---
    hunger: float = Attribute(default=20.0, min=0.0, max=20.0)
    stamina: float = Attribute(default=100.0, min=0.0, max=100.0)
    xp: int = Attribute(default=0, min=0)
    level: int = Attribute(default=1, min=1)
    inventory: Inventory = Inventory(rows=4, cols=8)  # 32 slots
    hotbar: Inventory = Inventory(rows=1, cols=8)      # 8 slots
    armor_slots: Inventory = Inventory(rows=1, cols=4)  # head, chest, legs, feet

    # Player step is minimal -- most behavior is input-driven via Actions
    def step(self, dt: float, world: WorldView):
        # Hunger drain
        self.hunger -= dt * 0.005
        if self.hunger <= 0:
            self.hp -= dt * 1.0  # starving
        # Stamina regen
        if not self.is_sprinting:
            self.stamina = min(100.0, self.stamina + dt * 5.0)
        # Natural HP regen when well-fed
        if self.hunger > 18.0 and self.hp < self.meta.max_hp:
            self.hp = min(self.meta.max_hp, self.hp + dt * 0.5)
```

---

## 3. Pydantic Attribute System

All Object attributes are Pydantic fields. This gives us:

### 3.1 Validation

```python
class Attribute:
    """Typed, validated, serializable object attribute."""

    def __init__(self, default, min=None, max=None, description="",
                 sync=True, persist=True):
        """
        default:     Default value
        min/max:     Clamping bounds (enforced on set)
        description: For documentation and in-game inspector
        sync:        If True, changes are sent to clients
        persist:     If True, saved to disk
        """
```

### 3.2 Serialization

```
Object state is automatically serializable:

  pig = Pig(hunger=0.8, age=120.5, hp=8)

  # To bytes (for network/disk):
  data = pig.serialize()    # MessagePack binary

  # From bytes:
  pig2 = Pig.deserialize(data)

  # To dict (for inspection/debugging):
  pig.to_dict()
  # {"type": "base:pig", "hp": 8, "hunger": 0.8, "age": 120.5, ...}
```

### 3.3 Delta Tracking

```
Objects track which attributes changed since last sync:

  pig.hunger = 0.3        # marks 'hunger' as dirty
  pig.get_dirty_fields()  # returns {"hunger": 0.3}
  pig.clear_dirty()       # reset after sync

Only dirty fields are sent to clients = minimal bandwidth.
```

---

## 4. Object Meta

ObjectMeta is the static definition -- shared across all instances of a type:

```python
class ObjectMeta(BaseModel):
    # --- Identity ---
    id: str                          # "namespace:name" e.g. "base:dirt"
    display_name: str
    description: str = ""
    category: str                    # terrain, plant, animal, tool, etc.
    author: str = "system"           # player who created this
    version: int = 1

    # --- Visuals ---
    texture: str                     # default texture file
    texture_top: Optional[str]       # per-face overrides (blocks)
    texture_bottom: Optional[str]
    texture_side: Optional[str]
    texture_animated: bool = False
    animation_frames: int = 1
    model: Optional[str]             # 3D model file (entities)
    alpha: float = 1.0               # transparency

    # --- Physics ---
    collision_box: Optional[Tuple[float, ...]]  # (x1,y1,z1,x2,y2,z2)
    eye_height: float = 0.0
    walk_speed: float = 0.0
    run_speed: float = 0.0
    jump_height: float = 0.0
    gravity_scale: float = 1.0       # 0 = no gravity (ghosts, particles)

    # --- Block properties ---
    hardness: float = 1.0            # seconds to mine with bare hands
    tool_group: Optional[str]        # shovel, pickaxe, axe, sword
    drop: Optional[str]              # item dropped when destroyed
    stack_max: int = 64
    light_source: int = 0            # 0-14, how much light this emits
    floodable: bool = False          # destroyed by liquid

    # --- Living properties ---
    max_hp: int = 0
    armor: int = 0

    # --- Sounds ---
    sounds: Optional[SoundSet]

    # --- Groups & tags ---
    groups: Dict[str, int] = {}      # {"cracky": 3, "stone": 1}
    tags: Set[str] = set()           # {"natural", "flammable"}

    # --- Spawning ---
    loot: Optional[LootTable]
    spawn_biomes: List[str] = []
    spawn_chance: float = 0.0
    spawn_light_min: int = 0
    spawn_light_max: int = 15
```

---

## 5. Object Registry

The server maintains a registry of all known object types:

```
ObjectRegistry
  |
  |-- built_in/                    # Ships with the game
  |     dirt.py  -> DirtBlock
  |     stone.py -> StoneBlock
  |     pig.py   -> Pig
  |     ...
  |
  |-- player_uploaded/             # Created by players
  |     custom_1a2b.py -> MagicOre
  |     custom_3c4d.py -> FlyingCat
  |     ...
  |
  |-- register(object_class) -> ObjectDefinition
  |-- unregister(object_id) -> bool
  |-- get(object_id) -> ObjectDefinition
  |-- create(object_id, **attrs) -> Object
  |-- hot_reload(object_id, new_source: str) -> bool
  |-- list_all() -> List[ObjectDefinition]
  |-- list_by_category(cat: str) -> List[ObjectDefinition]
```

### Hot-Reload Flow

```
Player uploads new Object definition
  |
  v
Server receives Python source
  |
  v
Sandbox validator checks:
  - No imports outside modcraft.api whitelist
  - No file I/O, network, subprocess, eval/exec
  - No infinite loops (timeout on step)
  - Class extends PassiveObject or ActiveObject
  - ObjectMeta.id follows "playername:objectname" format
  - Pydantic model validates
  |
  v
Compile to bytecode
  |
  v
Register in ObjectRegistry
  |
  v
Broadcast new ObjectDefinition to all clients
  (clients download texture/model assets)
  |
  v
Object is now usable in the world
  (can be spawned, crafted, placed)
```

---

## 6. Object Interaction Matrix

How objects relate to each other and to Actions:

```
                    Can be      Can be     Can        Has
                    mined?      pushed?    stack?     inventory?
                    --------    --------   --------   ----------
PassiveObject
  DirtBlock         yes         no         yes(64)    no
  StoneBlock        yes         no         yes(64)    no
  Chest             yes         no         yes(1)     yes(32 slots)
  CraftingTable     yes         no         yes(1)     yes(9 craft slots)

ActiveObject
  Water             no          no         no         no
  Fire              yes(stomp)  no         no         no
  Pig               no          yes        no         no
  Player            no          yes        no         yes(inventory)
  ItemEntity        yes(pickup) yes        no         no
  TNT               yes         yes        yes(64)    no

                    step()?     on_hit()?  on_dig()?  on_use()?
                    --------    ---------  ---------  ---------
PassiveObject       no          no         yes        yes
ActiveObject        yes         yes        yes        yes
LivingObject        yes         yes        yes        yes
PlayerObject        yes         yes        no         yes
```

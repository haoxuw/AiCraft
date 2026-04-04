# Agentica - World

The World is the top-level container. It holds every Object, tracks time, and is the single argument passed to every Action.

---

## 1. World Structure

```
World
  |
  |-- grid: ChunkManager              # The voxel grid (blocks/terrain)
  |     |-- chunks: Dict[ChunkPos, Chunk]
  |     |-- get_block(pos) -> BlockObject
  |     |-- set_block(pos, BlockObject)
  |     |-- get_blocks_in_radius(pos, r) -> List[BlockObject]
  |
  |-- entities: EntityManager          # All non-block objects
  |     |-- active: Dict[EntityId, ActiveObject]
  |     |-- spatial_index: SpatialHash
  |     |-- get_entities_in_radius(pos, r) -> List[ActiveObject]
  |     |-- get_entities_by_type(type_name) -> List[ActiveObject]
  |
  |-- players: PlayerManager           # Connected players (subset of entities)
  |     |-- online: Dict[PlayerId, PlayerObject]
  |     |-- get_player(id) -> PlayerObject
  |
  |-- time: WorldTime                  # Day/night cycle, game ticks
  |     |-- tick: u64                  # Monotonic tick counter
  |     |-- time_of_day: f32           # 0.0 = midnight, 0.5 = noon
  |     |-- day: u32                   # Day counter
  |
  |-- weather: WeatherState            # Global weather
  |     |-- type: WeatherType          # clear, rain, snow, storm
  |     |-- intensity: f32             # 0.0 to 1.0
  |     |-- wind: Vec3                 # Wind vector
  |
  |-- registry: ObjectRegistry         # All known object/action definitions
  |     |-- object_types: Dict[str, ObjectDefinition]
  |     |-- action_types: Dict[str, ActionDefinition]
  |
  |-- action_queue: ActionQueue        # Pending actions for this tick
```

---

## 2. Coordinate System

```
Y (up)
|
|   Z (north)
|  /
| /
|/_________ X (east)

One unit = one block = 1 meter
Block position: (x, y, z) as integers
Entity position: (x, y, z) as floats (sub-block precision)
```

---

## 3. Voxel Grid & Chunks

### Chunk Layout

```
Chunk = 16 x 16 x 16 blocks = 4096 blocks

Chunk position = floor(block_pos / 16)

Block (35, -12, 100):
  chunk_pos = (2, -1, 6)
  local_pos = (3, 4, 4)    # block_pos mod 16 (adjusted for negative)
```

### Chunk Data

```python
class ChunkPos(BaseModel):
    x: int
    y: int
    z: int

class Chunk(BaseModel):
    pos: ChunkPos
    blocks: bytes                      # 4096 entries, each is an object_type_id (u16)
    block_params: bytes                # 4096 * N bytes for per-block attributes
    block_entities: Dict[int, ActiveObject]  # blocks with active behavior (furnace, etc.)
    modified: bool = False
    generated: bool = False
    last_accessed: float = 0.0
```

### Chunk Lifecycle

```
                +---------------+
                |  Not Loaded   |
                | (disk / void) |
                +-------+-------+
                        |
           Player enters load range
                        |
                +-------v-------+
                |   Loading     |
                | Generate or   |
                | read from DB  |
                +-------+-------+
                        |
                +-------v-------+
                |    Active     |  <-- ABMs run, entities step,
                | In memory,    |      players can interact
                | fully ticked  |
                +-------+-------+
                        |
           Player leaves active range
                        |
                +-------v-------+
                |    Loaded     |  <-- In memory but not ticked,
                | (idle buffer) |      still renderable by client
                +-------+-------+
                        |
           Idle timeout (30s)
                        |
                +-------v-------+
                |   Unloaded    |
                | Save to disk, |
                | free memory   |
                +-------+-------+
```

### Load Ranges

```
Top-down view around a player (P):

   render_range (client-side, visual only)
   +---------------------------------------+
   |                                       |
   |   active_range (server ticks blocks)  |
   |   +---------------------------+       |
   |   |                           |       |
   |   |   interact_range          |       |
   |   |   +---------------+      |       |
   |   |   |               |      |       |
   |   |   |       P       |      |       |
   |   |   |               |      |       |
   |   |   +---------------+      |       |
   |   |   (4 chunks, ~64 blocks) |       |
   |   +---------------------------+       |
   |   (8 chunks, ~128 blocks)             |
   +---------------------------------------+
   (16 chunks, ~256 blocks)

   interact_range: player can place/dig/hit
   active_range:   ABMs, entities, node timers run
   render_range:   client renders but no simulation
```

---

## 4. World Step

Each server tick, the World advances:

```
World::step(dt: f32)
  |
  |-- 1. Advance time
  |     time.tick += 1
  |     time.time_of_day += dt * day_speed
  |
  |-- 2. Update weather
  |     weather.step(dt)
  |
  |-- 3. Update chunk management
  |     Load/unload chunks based on player positions
  |     Trigger generation for missing chunks
  |
  |-- 4. Process action queue
  |     For each queued Action:
  |       Validate preconditions
  |       Execute action (calls Python action function)
  |       Collect state mutations
  |       Apply mutations to world
  |       Queue triggered/chained actions
  |
  |-- 5. Step active objects
  |     For each ActiveObject in active chunks:
  |       obj.step(dt, world)
  |       Collect emitted actions
  |
  |-- 6. Run periodic systems (accumulated dt thresholds)
  |     ABMs:        every 1.0s -- scan blocks for pattern matches
  |     Liquid flow:  every 0.5s -- water/lava spreading
  |     Light update: every 0.1s -- propagate light changes
  |     Save:         every 5.0s -- persist modified chunks
  |
  |-- 7. Broadcast state deltas to clients
```

---

## 5. World as Action Context

Actions don't get raw access to internal data structures. They receive a `WorldView` -- a sandboxed, scoped interface:

```python
class WorldView:
    """What an Action or ActiveObject sees of the world.
    Scoped to a region around the action's origin for performance and safety."""

    def get_block(self, pos: BlockPos) -> Optional[BlockObject]: ...
    def set_block(self, pos: BlockPos, block: BlockObject) -> None: ...
    def get_blocks_in_radius(self, center: BlockPos, radius: int) -> List[Tuple[BlockPos, BlockObject]]: ...

    def get_entities_in_radius(self, center: Vec3, radius: float) -> List[ActiveObject]: ...
    def spawn_entity(self, pos: Vec3, entity_type: str, **attrs) -> EntityId: ...
    def remove_entity(self, entity_id: EntityId) -> None: ...

    def get_time(self) -> WorldTime: ...
    def get_weather(self) -> WeatherState: ...

    def emit_action(self, action_type: str, **kwargs) -> None: ...
    def emit_particles(self, spec: ParticleSpec) -> None: ...
    def play_sound(self, sound: str, pos: Vec3, gain: float = 1.0) -> None: ...

    # Read-only queries
    def find_nearest(self, pos: Vec3, type_name: str, radius: float) -> Optional[ActiveObject]: ...
    def raycast(self, origin: Vec3, direction: Vec3, max_dist: float) -> Optional[RaycastHit]: ...
    def get_light_level(self, pos: BlockPos) -> int: ...
```

The server enforces scope limits. A WorldView centered at (100, 50, 200) with radius 32 cannot read or write blocks at (500, 50, 200). This prevents runaway scripts from affecting the entire world.

---

## 6. Persistence

```
World save directory:
  world_name/
    world.toml           # World metadata (seed, time, weather, settings)
    chunks/
      chunk_0_0_0.dat    # Compressed chunk data (blocks + block_params)
      chunk_0_0_1.dat
      ...
    entities/
      entities.db        # SQLite: all non-block entities with position + state
    players/
      player_uuid.toml   # Per-player: position, inventory, stats
    artifacts/
      objects/
        dirt.py           # Built-in object definitions
        pig.py
        custom_1a2b.py    # Player-uploaded object definitions
      actions/
        mine.py           # Built-in action definitions
        tnt_explode.py
        custom_3c4d.py    # Player-uploaded action definitions
```

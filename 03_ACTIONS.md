# AiCraft - Actions

Actions are discrete events that modify the World. Every change in the game -- mining a block, a sheep eating grass, TNT exploding -- is an Action.

---

## 1. Why Actions?

```
WITHOUT Actions:                    WITH Actions:
  pig.step():                         pig.step():
    block.grass = 0                     world.emit_action("eat_grass",
    pig.hunger += 0.3                       eater=self, block=pos)
    world.set_block(...)
                                      Server validates, executes, logs,
  Direct mutation.                    broadcasts. Auditable, replayable,
  No validation.                      moddable.
  No logging.
  No undo.
```

Actions are the **only way** to mutate the World. Objects can read state freely, but writes go through Actions. This gives us:

- **Validation**: server checks preconditions before execution
- **Atomicity**: an Action either fully succeeds or fully fails
- **Logging**: every world change is recorded (replay, debugging)
- **Networking**: Actions are the unit of synchronization
- **Moddability**: players can define new Actions and override existing ones
- **Chaining**: Actions can trigger other Actions (TNT -> explosion -> fire -> burn)

---

## 2. Action Definition

Actions are Python functions decorated with metadata:

```python
# artifacts/actions/mine.py
from aicraft.api import (
    Action, ActionMeta, WorldView, PlayerObject, BlockPos,
    Precondition, Effect
)

@Action
class Mine:
    """Player mines (breaks) a block."""

    meta = ActionMeta(
        id="base:mine",
        display_name="Mine",
        category="player",
        trigger="player_input",        # how it's initiated
        cooldown=0.0,                  # seconds between uses
        range=5.0,                     # max distance
        animation="mine",             # player animation to play
        sound="dig_{tool_group}",      # parameterized sound
        particles="block_break",       # particle effect
    )

    # --- Inputs (what the action needs) ---
    actor: EntityId                    # who is mining
    target_pos: BlockPos               # what block to mine
    tool_slot: int = 0                 # which hotbar slot (for tool)

    # --- Preconditions (checked before execution) ---
    def validate(self, world: WorldView) -> bool:
        actor = world.get_entity(self.actor)
        if not isinstance(actor, PlayerObject):
            return False
        block = world.get_block(self.target_pos)
        if block is None:
            return False
        if actor.pos.distance(self.target_pos.to_vec3()) > self.meta.range:
            return False
        return True

    # --- Execution (modifies the world) ---
    def execute(self, world: WorldView):
        actor = world.get_entity(self.actor)
        block = world.get_block(self.target_pos)
        tool = actor.hotbar.get(self.tool_slot)

        # Calculate dig time based on tool vs block
        dig_time = self.calc_dig_time(block, tool)

        # This is an instant action in the spec, but the client
        # handles the digging progress bar. The server receives
        # the Mine action only when digging completes.

        # Drop items
        drop = block.meta.drop or block.meta.id
        world.spawn_entity(
            self.target_pos.to_vec3() + Vec3(0, 0.5, 0),
            "base:item_entity",
            item_type=drop,
            count=1,
        )

        # Notify the block
        block.on_dig(world, self.target_pos, actor)

        # Remove the block
        world.set_block(self.target_pos, None)

        # Wear the tool
        if tool:
            tool.wear += self.calc_wear(block, tool)
            if tool.wear >= tool.meta.max_wear:
                actor.hotbar.remove(self.tool_slot)
                world.play_sound("tool_break", actor.pos)

        # Notify neighbors
        for offset in NEIGHBOR_OFFSETS:
            neighbor_pos = self.target_pos.offset(*offset)
            neighbor = world.get_block(neighbor_pos)
            if neighbor:
                neighbor.on_neighbor_changed(world, neighbor_pos, self.target_pos)

    def calc_dig_time(self, block, tool):
        base_time = block.meta.hardness
        if tool and tool.meta.tool_group == block.meta.tool_group:
            return base_time / tool.meta.tool_speed
        return base_time
```

---

## 3. Action Categories

```
Action
  |
  |-- Player Input Actions (triggered by keyboard/mouse)
  |     Mine, Place, Attack, UseItem, Interact, Jump, Sprint,
  |     OpenInventory, Chat, DropItem, SwapHands
  |
  |-- Entity Actions (triggered by ActiveObject.step())
  |     EatGrass, WanderTo, FleeFrom, AttackTarget,
  |     LayEgg, Grow, Despawn
  |
  |-- World Actions (triggered by the world systems)
  |     LiquidFlow, FireSpread, GrassGrow, LeafDecay,
  |     LavaCool, LightUpdate, WeatherChange
  |
  |-- Item Actions (triggered by item use/activation)
  |     TNTExplode, PotionDrink, FoodEat, BowShoot,
  |     FlintIgnite, BucketFill, MapReveal
  |
  |-- Chained Actions (triggered by other actions)
  |     Knockback (from Attack), Ignite (from FireSpread),
  |     DropLoot (from entity death), XPGain (from mining)
```

---

## 4. Action Lifecycle

```
+------------------------------------------------------------------+
|                     Action Processing Pipeline                    |
+------------------------------------------------------------------+

  Source (player input, entity step, ABM, chain)
       |
       v
  1. Construct Action instance with inputs
       |
       v
  2. Queue in World.action_queue
       |
       v
  3. [Per-tick processing]
       |
       v
  4. Validate preconditions
       |-- FAIL -> discard, notify source
       |
       v (PASS)
  5. Execute action
       |-- Reads world state
       |-- Computes effects
       |-- Applies mutations via WorldView
       |-- May emit chained actions
       |
       v
  6. Collect mutations
       |-- Block changes: [(pos, old_block, new_block), ...]
       |-- Entity changes: [(entity_id, field, old_val, new_val), ...]
       |-- Spawns: [(entity_type, pos, attrs), ...]
       |-- Removals: [entity_id, ...]
       |-- Sounds: [(sound, pos, gain), ...]
       |-- Particles: [(spec, pos), ...]
       |
       v
  7. Apply to authoritative world state (server-side)
       |
       v
  8. Create ActionResult
       |
       v
  9. Broadcast to clients
       |-- Affected clients receive delta updates
       |-- Sounds/particles sent to nearby clients
       |
       v
  10. Log to action history (for replay/debugging)
```

---

## 5. More Action Examples

### 5.1 TNT Explosion

```python
# artifacts/actions/tnt_explode.py

@Action
class TNTExplode:
    """TNT detonates, destroying blocks in a sphere and damaging entities."""

    meta = ActionMeta(
        id="base:tnt_explode",
        display_name="TNT Explosion",
        category="item",
        trigger="timer",                  # triggered by TNT's fuse timer
        sound="tnt_explode",
        particles="explosion_large",
    )

    center: BlockPos
    radius: int = 3
    source_entity: Optional[EntityId] = None  # who placed the TNT

    def validate(self, world: WorldView) -> bool:
        block = world.get_block(self.center)
        return block is not None and "tnt" in block.meta.groups

    def execute(self, world: WorldView):
        r = self.radius

        # Destroy blocks in sphere
        for x in range(-r, r + 1):
            for y in range(-r, r + 1):
                for z in range(-r, r + 1):
                    if x*x + y*y + z*z > r*r:
                        continue
                    pos = self.center.offset(x, y, z)
                    block = world.get_block(pos)
                    if block is None:
                        continue
                    if "unbreakable" in block.meta.groups:
                        continue
                    # Some blocks drop, some don't (blast resistance)
                    blast_resist = block.meta.groups.get("blast_resistance", 0)
                    if random() > blast_resist * 0.25:
                        # Drop item
                        if random() < 0.3:  # 30% drop rate from explosions
                            world.spawn_entity(pos.to_vec3(),
                                "base:item_entity",
                                item_type=block.meta.drop or block.meta.id)
                    # Replace with air (or fire if flammable)
                    if "flammable" in block.meta.groups and random() < 0.3:
                        world.set_block(pos, world.registry.create("base:fire"))
                    else:
                        world.set_block(pos, None)

        # Damage entities in radius
        for entity in world.get_entities_in_radius(self.center.to_vec3(), r * 1.5):
            dist = entity.pos.distance(self.center.to_vec3())
            damage = max(0, (1.0 - dist / (r * 1.5)) * 20)
            if damage > 0:
                world.emit_action("base:damage",
                    target=entity.entity_id,
                    amount=damage,
                    source=self.source_entity,
                    damage_type="explosion")
                # Knockback
                direction = (entity.pos - self.center.to_vec3()).normalized()
                world.emit_action("base:knockback",
                    target=entity.entity_id,
                    force=direction * damage * 0.5)

        # Chain: nearby TNT gets ignited
        for x in range(-r-1, r + 2):
            for y in range(-r-1, r + 2):
                for z in range(-r-1, r + 2):
                    pos = self.center.offset(x, y, z)
                    block = world.get_block(pos)
                    if block and "tnt" in block.meta.groups:
                        world.emit_action("base:tnt_ignite",
                            target_pos=pos, fuse=random() * 1.5)
```

### 5.2 Sheep Eats Grass

```python
# artifacts/actions/eat_grass.py

@Action
class EatGrass:
    """An animal eats grass from a dirt block."""

    meta = ActionMeta(
        id="base:eat_grass",
        display_name="Eat Grass",
        category="entity",
        trigger="entity_step",
        sound="eat_grass",
        animation="eat",
        duration=2.0,                    # takes 2 seconds
    )

    eater: EntityId
    block_pos: BlockPos

    def validate(self, world: WorldView) -> bool:
        eater = world.get_entity(self.eater)
        if eater is None or not isinstance(eater, LivingObject):
            return False
        block = world.get_block(self.block_pos)
        if block is None:
            return False
        return hasattr(block, 'grass_level') and block.grass_level > 0.2

    def execute(self, world: WorldView):
        eater = world.get_entity(self.eater)
        block = world.get_block(self.block_pos)

        # Reduce grass
        eaten = min(block.grass_level, 0.5)
        block.grass_level -= eaten

        # Feed the animal
        if hasattr(eater, 'hunger'):
            eater.hunger = min(1.0, eater.hunger + eaten * 0.6)

        # If grass fully eaten, change texture
        if block.grass_level <= 0.05:
            block.grass_level = 0.0
            # Grass will regrow via a GrassGrow ABM over time
```

### 5.3 Cast Magic Spell (Player-Created Example)

```python
# artifacts/actions/fireball.py
# Created by player "wizardMike" in-game

@Action
class CastFireball:
    """Launch a fireball projectile that explodes on impact."""

    meta = ActionMeta(
        id="wizardMike:cast_fireball",
        display_name="Cast Fireball",
        category="player",
        trigger="player_input",
        cooldown=3.0,                    # 3 second cooldown
        range=50.0,
        cost={"stamina": 25.0},          # costs 25 stamina
        animation="cast",
        sound="fireball_cast",
    )

    caster: EntityId
    direction: Vec3

    def validate(self, world: WorldView) -> bool:
        caster = world.get_entity(self.caster)
        if not isinstance(caster, PlayerObject):
            return False
        if caster.stamina < 25.0:
            return False
        return True

    def execute(self, world: WorldView):
        caster = world.get_entity(self.caster)
        caster.stamina -= 25.0

        # Spawn a fireball projectile entity
        spawn_pos = caster.pos + Vec3(0, caster.meta.eye_height, 0)
        world.spawn_entity(
            spawn_pos,
            "wizardMike:fireball_projectile",
            velocity=self.direction.normalized() * 20.0,
            damage=8.0,
            owner=self.caster,
            lifetime=3.0,
        )
```

---

## 6. Action Meta

```python
class ActionMeta(BaseModel):
    # --- Identity ---
    id: str                              # "namespace:name"
    display_name: str
    description: str = ""
    category: str                        # player, entity, world, item, chain
    author: str = "system"
    version: int = 1

    # --- Trigger ---
    trigger: str                         # player_input, entity_step, timer,
                                         # abm, chain, on_place, on_dig, on_use

    # --- Constraints ---
    cooldown: float = 0.0                # seconds between uses (per actor)
    range: float = 0.0                   # max distance from actor to target
    cost: Dict[str, float] = {}          # attribute costs {"stamina": 10}
    requires_tool: Optional[str] = None  # tool group required
    requires_groups: Dict[str, int] = {} # target must have these groups
    duration: float = 0.0                # how long the action takes (0=instant)

    # --- Feedback ---
    animation: Optional[str]             # animation name on the actor
    sound: Optional[str]                 # sound to play (supports {var} templates)
    particles: Optional[str]             # particle effect name
```

---

## 7. Action Queue & Ordering

```
Each server tick processes actions in priority order:

  Priority 0 (highest): World system actions (physics, lighting)
  Priority 1:           Chained actions (from previous tick's actions)
  Priority 2:           Player input actions (mine, place, attack)
  Priority 3:           Entity actions (mob behavior)
  Priority 4 (lowest):  Deferred actions (ABM results, timed events)

Within the same priority: FIFO order.

Conflict resolution:
  - Two players mine the same block? First one wins, second fails validation.
  - Entity and player both target same entity? Both execute (damage stacks).
  - Action modifies a block that another action already changed this tick?
    Second action sees the POST-first-action state.

Max actions per tick: configurable (default 1000).
  If exceeded, remaining actions roll over to next tick.
```

---

## 8. Action History & Replay

```
Every executed action is logged:

ActionLog entry:
  {
    tick: 14523,
    timestamp: "2026-03-30T10:15:23.456Z",
    action_id: "base:mine",
    inputs: {actor: 42, target_pos: [10, 5, -3], tool_slot: 0},
    mutations: [
      {type: "block_remove", pos: [10, 5, -3], old: "base:stone"},
      {type: "entity_spawn", entity_type: "base:item_entity", pos: [10, 5.5, -3]},
      {type: "attr_change", entity: 42, field: "tool_wear", old: 5, new: 6},
    ],
    result: "success"
  }

Uses:
  - Debug: "Why did my block disappear?" -> check action log
  - Replay: play back a recording of a build session
  - Undo: admin can revert specific actions (future feature)
  - Analytics: track what players do most
```

# Artifact Registry -- TODO

How player-created content enters the game. **Not yet implemented.**

---

## What Players Can Upload

Players extend the base classes to create new content:

```python
# --- Data-only variant (no new class needed) ---
# Just define an ObjectMeta, use an existing class
MY_MARBLE = ObjectMeta(
    id="alice:marble",
    display_name="Marble",
    hardness=5.0,
    tool_group="pickaxe",
    color=(0.90, 0.88, 0.85),
    groups={"cracky": 2, "stone": 1},
)
# Server registers: ArtifactRegistry.register_block_meta(MY_MARBLE, PassiveObject)


# --- New behavior (needs new class) ---
class Repeater(SignalObject):
    """Delays a signal by N ticks."""
    delay_ticks = Property(default=4, min_val=1, max_val=40)
    buffer = Property(default=0)
    tick_counter = Property(default=0)

    def decide(self, world):
        incoming = self.read_neighbor_power(world)
        self.tick_counter += 1
        if self.tick_counter >= self.delay_ticks:
            self.power = self.buffer
            self.buffer = incoming
            self.tick_counter = 0
        return []

REPEATER_META = ObjectMeta(id="alice:repeater", display_name="Repeater", ...)
# Server registers: ArtifactRegistry.register_block_meta(REPEATER_META, Repeater)


# --- New mob ---
class Dragon(MobObject):
    """A dragon that flies and breathes fire."""
    fire_cooldown = Property(default=0.0, tick_rate=-1.0)

    def decide(self, world):
        actions = []
        if self.fire_cooldown <= 0:
            target = self.find_nearest_player(world)
            if target and target.pos.distance(self.pos) < 20:
                actions.append(BreatheFire(caster=self.entity_id, ...))
                self.fire_cooldown = 5.0
        return actions

DRAGON_META = ObjectMeta(id="bob:dragon", display_name="Dragon", max_hp=100, ...)


# --- New action ---
class BreatheFire(Action):
    meta = ActionMeta(id="bob:breathe_fire", ...)
    caster: EntityId
    direction: Vec3

    def execute(self, world):
        # Set blocks on fire in a cone
        ...
```

---

## Upload Flow (to implement)

```
Player writes code in-game editor
       |
       v
Client sends TOSERVER_UPLOAD_ARTIFACT
  {source_code, author, assets: {name: bytes}}
       |
       v
Server: ArtifactRegistry.upload_artifact()
       |
  +----v----+
  | VALIDATE |
  |          |
  | 1. AST scan: reject forbidden imports/builtins
  | 2. Check class extends a known base class
  | 3. Check ObjectMeta.id follows "author:name" format
  | 4. Check resource limits (source size, asset size)
  | 5. Test instantiation with timeout (1s)
  | 6. Run decide()/execute() with mock WorldView (1s)
  +----+----+
       |
       v (pass)
  +----v------+
  | REGISTER   |
  |            |
  | 1. Assign numeric BlockId (if block)
  | 2. Add to BlockRegistry / EntityManager
  | 3. Save .py to artifacts/ directory
  | 4. Save assets to assets/ directory
  +----+------+
       |
       v
  +----v------+
  | BROADCAST  |
  |            |
  | 1. Send TOCLIENT_NEW_CONTENT to all clients
  | 2. Clients download assets on demand
  | 3. Object/action is now live
  +------------+
```

---

## Implementation Checklist

### Phase 1: Basic Registration (no upload, no sandbox)
- [ ] `ArtifactRegistry` stores built-in + user artifacts
- [ ] `register_block_meta()` assigns BlockId, adds to C++ BlockRegistry
- [ ] `register_action()` adds to action dispatch
- [ ] `create_instance()` instantiates correct class + meta
- [ ] Startup: register all built-ins from `aicraft.objects` and `aicraft.actions`

### Phase 2: Upload Pipeline
- [ ] Server receives TOSERVER_UPLOAD_ARTIFACT packets
- [ ] Source code stored in `artifacts/{author}/` directory
- [ ] Asset files stored in `assets/{author}/` directory
- [ ] `upload_artifact()` validates, compiles, registers
- [ ] `hot_reload()` replaces class, migrates instances

### Phase 3: Sandbox
- [ ] AST validator: reject `os`, `sys`, `subprocess`, `eval`, `exec`, `open`
- [ ] Import whitelist: only `aicraft.api`, `math`, `random`, `typing`, `enum`
- [ ] CPU timeout: `signal.alarm` or thread-based timeout on decide()/execute()
- [ ] Memory limit: `resource.setrlimit` per player
- [ ] WorldView scoping: radius limit around action origin

### Phase 4: Network Integration
- [ ] pybind11 bridge: C++ server calls Python registry
- [ ] TOCLIENT_NEW_CONTENT packet format
- [ ] Client asset download on demand
- [ ] Client object type cache

### Phase 5: Social Features
- [ ] Version history per artifact
- [ ] Fork/attribution chain
- [ ] In-game inspector (view source of any object)
- [ ] Content browser / marketplace

---

## Namespace Rules

```
Built-in:     "base:stone", "base:pig"        -- reserved, immutable
Player:       "alice:marble", "bob:dragon"     -- author prefix enforced
Server admin: "admin:custom_ore"               -- admin privilege required
```

Players CANNOT register in the `base:` namespace.
Player artifacts MUST use their own username as prefix.

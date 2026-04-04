# Agentica - Python Client

The client handles rendering, input, the in-game code editor, and local preview/testing. Written in Python for consistency with the scripting model -- players use the same language to play and create.

---

## 1. Architecture Overview

```
+====================================================================+
|                        Agentica Python Client                        |
+====================================================================+
|                                                                      |
|  +------------------------+   +---------------------------+         |
|  |  Main Thread           |   |  Network Thread           |         |
|  |                        |   |                           |         |
|  |  Game loop:            |   |  TCP connection to server |         |
|  |  - Input polling       |   |  - Receive state deltas   |         |
|  |  - State interpolation |   |  - Send player actions    |         |
|  |  - Render frame        |   |  - Chunk data download    |         |
|  |  - UI / HUD / Editor   |   |  - Asset download         |         |
|  +------------------------+   +---------------------------+         |
|                                                                      |
|  +------------------------+   +---------------------------+         |
|  |  Mesh Worker Thread(s) |   |  Audio Thread             |         |
|  |                        |   |                           |         |
|  |  Chunk -> mesh         |   |  Positional audio         |         |
|  |  Greedy meshing algo   |   |  Ambient sounds           |         |
|  |  Upload to GPU         |   |  Music                    |         |
|  +------------------------+   +---------------------------+         |
|                                                                      |
|  +------------------------+                                         |
|  |  Preview Sandbox       |                                         |
|  |                        |                                         |
|  |  Local mini-server     |                                         |
|  |  for testing objects   |                                         |
|  |  and actions before    |                                         |
|  |  uploading             |                                         |
|  +------------------------+                                         |
+====================================================================+
```

---

## 2. Rendering Stack

```
Python client rendering options:

  Option A: moderngl + pyglet (lightweight, cross-platform)
  Option B: PyOpenGL + glfw   (more control, more boilerplate)
  Option C: wgpu-py           (modern GPU API, WebGPU-based)
  Option D: Ursina / Panda3D  (higher-level, faster to prototype)

  Recommended: moderngl + pyglet
  - moderngl wraps OpenGL with clean Python API
  - pyglet handles windowing, input, audio
  - Good enough performance for voxel rendering
  - Easy to extend with custom shaders
```

### Render Pipeline

```
Per frame:

  1. Input
     |-- Poll keyboard/mouse (pyglet events)
     |-- Compute camera direction from mouse delta
     |-- Map keys to player actions
     |
  2. State Update
     |-- Apply server state deltas (from network thread)
     |-- Interpolate entity positions (client-side prediction)
     |-- Update animations (time-based)
     |
  3. Visibility
     |-- Frustum culling (discard chunks outside camera cone)
     |-- Occlusion: skip chunks fully behind other chunks
     |-- LOD: far chunks use simplified meshes (future)
     |
  4. Render
     |-- Sky pass (sky color, sun/moon, clouds)
     |-- Terrain pass (chunk meshes, textured quads)
     |-- Entity pass (3D models for mobs/players/items)
     |-- Liquid pass (alpha-blended water surfaces)
     |-- Particle pass (instanced billboards)
     |-- UI pass (HUD, hotbar, inventory, chat)
     |-- Code editor pass (if open)
     |
  5. Swap buffers
```

### Chunk Meshing (Greedy Meshing)

```
Naive:  each visible block face = 2 triangles = 6 vertices
        16^3 block chunk, worst case: 4096 * 6 faces * 6 verts = 147,456 verts

Greedy: merge adjacent same-type faces into larger quads
        Typical chunk: ~2,000-5,000 verts (20-50x reduction)

Algorithm:
  For each axis (X, Y, Z):
    For each slice perpendicular to that axis:
      Build a 16x16 grid of "should this face be drawn?"
      Greedily merge adjacent same-type cells into rectangles
      Emit one quad per rectangle

  +--+--+--+--+       +--------+--+
  |  |  |  |  |       |        |  |
  +--+--+--+--+  -->  |  one   +--+     4 quads instead of 8
  |  |  |  |  |       |  quad  |  |
  +--+--+--+--+       +--------+--+
```

---

## 3. Client State Management

```python
class ClientState:
    """Local mirror of the server's world state."""

    # Chunks (received from server, meshed locally)
    chunks: Dict[ChunkPos, ClientChunk]  # block data + mesh

    # Entities (interpolated between server updates)
    entities: Dict[EntityId, ClientEntity]

    # Local player
    player: LocalPlayer
    camera: Camera

    # Object/Action definitions (received from server)
    object_registry: Dict[str, ObjectMeta]
    action_registry: Dict[str, ActionMeta]

    # UI state
    inventory_open: bool = False
    chat_open: bool = False
    editor_open: bool = False
    current_editor_file: Optional[str] = None
```

### Client-Side Prediction

```
Problem: 50ms+ round-trip to server means actions feel laggy.

Solution: predict locally, reconcile with server.

  t=0    Player presses "mine"
         Client: start dig animation + progress bar immediately
         Client: send TOSERVER_MINE to server

  t=25ms Server receives, validates, executes Mine action
         Server: sends TOCLIENT_BLOCK_CHANGE back

  t=50ms Client receives confirmation
         Client: block is already gone (predicted)
         If server says "no" (validation failed):
           Client: undo prediction, restore block

Predicted actions:
  - Block dig/place (position + type)
  - Player movement (client-authoritative with server validation)
  - Item use (animation only, effect waits for server)

Non-predicted (wait for server):
  - Damage dealt/received
  - Entity interactions
  - Inventory changes from crafting
  - Custom action effects
```

---

## 4. In-Game Code Editor

The centerpiece of Agentica -- players write Python inside the game.

```
+====================================================================+
|  Agentica Code Editor (in-game overlay)                              |
+====================================================================+
|                                                                      |
|  +----Tab Bar---------------------------------------------+         |
|  | pig.py  |  fireball.py* |  magic_ore.py |  + New      |         |
|  +---------------------------------------------------------+         |
|  |                                                         |         |
|  |  1  from agentworld.api import LivingObject, ObjectMeta    |         |
|  |  2                                                      |         |
|  |  3  class FlyingPig(LivingObject):                      |         |
|  |  4      """A pig that can fly!"""                        |         |
|  |  5                                                      |         |
|  |  6      meta = ObjectMeta(                               |         |
|  |  7          id="alice:flying_pig",                       |         |
|  |  8          display_name="Flying Pig",                   |  Preview|
|  |  9          model="pig.gltf",                            |  Window |
|  | 10          texture="pig_wings.png",                     |  +----+ |
|  | 11          max_hp=10,                                   |  |    | |
|  | 12          walk_speed=2.0,                              |  | 3D | |
|  | 13          gravity_scale=0.3,                           |  |view| |
|  | 14      )                                                |  |    | |
|  | 15                                                      |  +----+ |
|  | 16      wing_flap: float = 0.0                           |         |
|  | 17                                                      |         |
|  | 18      def step(self, dt, world):                       |         |
|  | 19  |       self.wing_flap += dt * 5.0                   |         |
|  |                                                         |         |
|  +---------------------------------------------------------+         |
|  | Output / Console                                        |         |
|  | > Syntax OK                                             |         |
|  | > Test instance created successfully                    |         |
|  | > step() ran 100 ticks in 8ms (avg 0.08ms/tick)         |         |
|  +---------------------------------------------------------+         |
|  | [Test Locally]  [Upload to Server]  [Save Draft]        |         |
|  +---------------------------------------------------------+         |
+====================================================================+
```

### Editor Features

```
Editing:
  - Syntax highlighting (Python)
  - Auto-indent
  - Bracket matching
  - Line numbers
  - Undo/redo (Ctrl+Z / Ctrl+Shift+Z)
  - Copy/paste (system clipboard)
  - Find/replace (Ctrl+F)
  - Tab completion for agentworld.api members

Assistance:
  - Inline error markers (red squiggly)
  - Type hints from agentworld.api stubs
  - Auto-complete for ObjectMeta/ActionMeta fields
  - Hover tooltips for API functions
  - Template gallery (start from examples)

Testing:
  - "Test Locally" button:
    Spawns a local mini-server with just your object
    Shows 3D preview in side panel
    Runs step() for N ticks, reports timing + errors
    Lets you interact (punch, mine, etc.) in preview
  - Console output (print statements, errors, timing)

Upload:
  - "Upload to Server" button:
    Sends source + assets to server
    Server validates (sandbox check)
    On success: object is live in the world
    On failure: error shown in console
```

### Templates

```python
# Template: Basic Block
from agentworld.api import PassiveObject, ObjectMeta

class MyBlock(PassiveObject):
    meta = ObjectMeta(
        id="{player}:my_block",
        display_name="My Block",
        category="terrain",
        texture="my_block.png",
        hardness=1.5,
        tool_group="pickaxe",
        groups={"cracky": 2},
    )

# Template: Basic Mob
from agentworld.api import LivingObject, ObjectMeta

class MyMob(LivingObject):
    meta = ObjectMeta(
        id="{player}:my_mob",
        display_name="My Mob",
        category="animal",
        model="my_mob.gltf",
        texture="my_mob.png",
        max_hp=10,
        walk_speed=3.0,
    )

    def step(self, dt: float, world: WorldView):
        pass  # Add behavior here

# Template: Basic Action
from agentworld.api import Action, ActionMeta

@Action
class MyAction:
    meta = ActionMeta(
        id="{player}:my_action",
        display_name="My Action",
        category="player",
        trigger="player_input",
    )

    actor: EntityId

    def validate(self, world: WorldView) -> bool:
        return True

    def execute(self, world: WorldView):
        pass  # Add effects here
```

---

## 5. Asset Pipeline

Players need textures, models, and sounds for their creations.

```
Asset creation options:

  1. In-game pixel art editor (built-in)
     - 16x16, 32x32, 64x64 canvas
     - Standard pixel art tools (pencil, fill, line, select)
     - Per-face preview on a cube
     - Export as PNG

  2. Import from file
     - Drag and drop PNG/OBJ/GLTF/OGG into the editor
     - File picker dialog
     - Auto-resize textures to power-of-2

  3. Placeholder textures
     - Solid color with text label
     - Procedural patterns (checkerboard, gradient, noise)
     - Good for prototyping before making real art

Asset upload with code:
  When uploading an object/action, associated assets
  are bundled and sent together.
  Server stores them in artifacts/assets/
  Other clients download on demand.
```

---

## 6. Client Directory Layout

```
agentica-client/
  src/
    main.py                      # Entry point
    client.py                    # Client class, main loop
    renderer/
      __init__.py
      engine.py                  # moderngl setup, frame loop
      chunk_mesh.py              # Greedy meshing algorithm
      entity_renderer.py         # 3D model rendering
      sky.py                     # Sky, sun, moon, clouds
      particles.py               # Particle system
      ui.py                      # HUD, menus, overlays
      shaders/
        terrain.vert / .frag     # Block rendering shaders
        entity.vert / .frag
        sky.vert / .frag
        liquid.vert / .frag
        ui.vert / .frag
    input/
      __init__.py
      keyboard.py                # Key bindings, action mapping
      mouse.py                   # Mouse look, click handling
    network/
      __init__.py
      connection.py              # TCP connection to server
      packet_handler.py          # Incoming packet dispatch
      delta_applicator.py        # Apply state deltas
    state/
      __init__.py
      client_state.py            # Local state mirror
      prediction.py              # Client-side prediction
      interpolation.py           # Entity position smoothing
    editor/
      __init__.py
      code_editor.py             # Text editor widget
      syntax_highlight.py        # Python syntax coloring
      autocomplete.py            # API completion
      preview_sandbox.py         # Local test server
      pixel_editor.py            # In-game texture painting
      template_gallery.py        # Starter templates
    audio/
      __init__.py
      sound_manager.py           # Positional audio, ambient
    camera/
      __init__.py
      camera.py                  # FPS camera, projection
      frustum.py                 # Frustum culling
  assets/
    textures/                    # Built-in textures
    models/                      # Built-in models
    sounds/                      # Built-in sounds
    shaders/                     # GLSL shaders
    fonts/                       # Editor font (monospace)
  pyproject.toml
  requirements.txt               # moderngl, pyglet, pydantic, etc.
```

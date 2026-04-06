# ModCraft - Architecture Diagram

Transcribed from the design diagram. Three cleanly separated layers.

---

## The Three Layers

```
+===========================+     +============================+
|    GAME MODEL LAYER       |     |    RENDERING LAYER         |
|    (Python code)          |     |    (C++ Engine)            |
|                           |     |                            |
|    World / Objects /      |     |    Game Client GUI         |
|    Actions / Behaviours   |     |    Animations, Textures,   |
|                           |     |    Sounds, Input, Views    |
+============+==============+     +============+===============+
             |                                 |
             v                                 v
+==============================================================+
|                    SERVER LAYER (C++ Engine)                   |
|                                                                |
|    Manages World. Hosts & loads Python Definitions             |
|    (Behaviour and Actions).                                    |
|    Single source of truth.                                     |
+==============================================================+
```

These layers are INDEPENDENT. You can swap the rendering engine (e.g. replace OpenGL with Vulkan, or use Unreal) without touching any game model code. You can add new Objects and Actions without recompiling the server or client.

---

## Full Diagram (text version of the whiteboard)

```
+-------------------------------------------------------------------------+
|                                                                          |
|   OBJECTS (everything in the world)                                     |
|                                                                          |
|   Player(s)                                                             |
|   (takes input)                                                         |
|       |                                                                  |
|       +-----> Passive                    Active                         |
|       |       e.g.                       e.g.                           |
|       |         Trees                      Player                       |
|       |         Blocks                     Bees                         |
|       |         Potion                     Lava                         |
|       |         Bucket                     Wheat (can grow)             |
|       |         TNT                                                     |
|       |                                                                  |
|   Items                                  NPC                            |
|     Potions                                Bees                         |
|     (Anything can                          Pigs                         |
|      be in inventory)                      Villager                     |
|                                                                          |
|   Properties (on ALL objects):                                          |
|     Inventory, Weight, HP, MP,                                          |
|     Affections towards other NPCs (or category of NPCs),               |
|     etc. -- Altered by World & Actions                                  |
|                                                                          |
+---+---------------------------------------------------------------------+
    |
    v
+-------------------------------------------------------------------------+
|                                                                          |
|   SERVER -- C++ Engine                                                  |
|   Manages World. Hosts & loads Python Definitions (Behaviour & Actions) |
|                                                                          |
|   +-------------------------------+                                     |
|   |          WORLD                |                                     |
|   |   Open, Endlessly generated   |                                     |
|   |                               |                                     |
|   |   +--------+   +---------+   |   +----------+                      |
|   |   | Objects |   | States  |   |   | Actions  |                      |
|   |   |         |   | Weather |   |   |          |                      |
|   |   |         |   | etc.    |   |   |          |                      |
|   |   +--------+   +---------+   |   +----------+                      |
|   +-------------------------------+                                     |
|                                                                          |
|   User Artifacts -> Hot loaded in the C++ Server                        |
|   (Code: Object Behaviour, Action Definition)                           |
|                                                                          |
|   +----------------------+      +------------------+                    |
|   | World Gen Engine     |      | Actions          |                    |
|   | Many Templates +     |      | (examples)       |                    |
|   | Randomizer           |      |   TNT Explode    |                    |
|   +----------------------+      |   Potion Heal    |                    |
|                                 |   Cast Fireball  |                    |
|                                 |   Chicken lay egg|                    |
|                                 +------------------+                    |
+---+---------------------------------------------------------------------+
    |
    |  <--- Python code, hot-loaded --->
    |
+---+---------------------------------------------------------------------+
|                                                                          |
|   BEHAVIOUR DEFINITION                  ACTION DEFINITION               |
|   (Python Code)                         (Python Code)                   |
|                                                                          |
|   Bees: go between flower               TNT: Blink 5s, Remove 5x5     |
|     and nest                               near blocks, play animation |
|   Pigs: wander around                   Fireball: travel direction,    |
|   Villager: trade, hangout                 remove 3x3 blocks if hit    |
|     together                                                            |
|                                                                          |
|   ** Can be generated by LLM            ** Can be generated by LLM     |
|      during play time **                   during play time **          |
|                                                                          |
+-------------------------------------------------------------------------+

+-------------------------------------------------------------------------+
|                                                                          |
|   GAME CLIENT -- C++ Engine (SEPARATE from game model)                  |
|                                                                          |
|   +----------------+    +---------------------+                         |
|   | Keyboard /     |    | Game Client GUI     |    +-- Animations       |
|   | Controller     |    |                     |    +-- Textures         |
|   +----------------+    | 1st & 3rd & RTS     |    +-- Sounds          |
|                         | view (Black & White) |                        |
|                         +---------------------+                         |
|                                                                          |
|                         +---------------------+                         |
|                         | LLM Editor          |                         |
|                         | (in-game code editor)|                        |
|                         +---------------------+                         |
|                                                                          |
+-------------------------------------------------------------------------+
```

---

## Product Roadmap (bottom of diagram)

```
Playable       -->   FUN   -->  Programmable  -->  Creative  -->  Agentic  -->  Meta realworld
Sandbox                                                                          assets
```

| Stage | What it means |
|-------|---------------|
| **Playable Sandbox** | Walk around, place/mine blocks, flat world (Milestone 1) |
| **FUN** | Combat, mobs, crafting, survival mechanics |
| **Programmable** | In-game Python editor, hot-load objects & actions |
| **Creative** | Players create and share content, artifact marketplace |
| **Agentic** | LLM generates objects/actions during gameplay, AI NPCs |
| **Meta realworld assets** | Bridge game creations to real-world value |

---

## Separation Rules

### Rule 1: Game Model knows NOTHING about rendering

```
Objects, Actions, World, Behaviours:
  - Pure Python classes
  - No OpenGL calls
  - No texture references (only texture NAME strings)
  - No animation code (only animation NAME strings)
  - No sound playback (only sound NAME strings)
  - No input handling (only receives structured Action requests)

The model says WHAT exists and WHAT happens.
The renderer decides HOW it looks and sounds.
```

### Rule 2: Renderer knows NOTHING about game logic

```
Renderer / Client:
  - Receives state snapshots from server
  - Maps object type names to visual assets
  - Does NOT know what a "pig" does
  - Does NOT validate actions
  - Does NOT run object behaviour code
  - Only knows: "entity X is at position Y with model Z"
```

### Rule 3: Server is the bridge

```
Server (C++):
  - Hosts the World data structure
  - Embeds Python runtime
  - Loads Object and Action Python code
  - Runs the 3-phase step loop
  - Sends state deltas to clients
  - Receives input from clients
  - Does NOT render
  - Does NOT handle keyboard/mouse
```

### Consequence: You can swap any layer

```
Want better graphics?
  -> Replace the C++ client renderer with Unreal/Godot/custom Vulkan
  -> Game model Python code unchanged
  -> Server C++ code unchanged

Want different game content?
  -> Write new Python objects and actions
  -> Client renderer unchanged (auto-discovers new assets)
  -> Server C++ code unchanged

Want a different server?
  -> Rewrite in Rust, Go, whatever
  -> As long as it embeds Python and runs the step loop
  -> Client and game model unchanged
```

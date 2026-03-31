# Reference Material -- NOT Our Design

These documents describe the existing Luanti engine and Minetest Game.

## Why These Exist

To learn from prior art. Luanti is a mature voxel engine with 14+ years of development. Understanding their solutions helps us make better decisions -- including knowing what to do DIFFERENTLY.

## What We Are NOT Copying

| Luanti/Minetest Approach | AiCraft Approach |
|--------------------------|------------------|
| Lua scripting (custom Lua VM embedded) | Python scripting (CPython embedded via pybind11) |
| Hard-coded C++ game functions (`register_node`, `register_abm`, etc.) | Thin C++ server with NO game logic -- all game logic in Python |
| Lua mods loaded at startup, require restart | Python objects/actions hot-loaded DURING gameplay |
| Engine defines node/tool/craft systems in C++ | Engine only provides World/Object/Action primitives |
| Sound, particle, HUD systems baked into C++ engine | C++ server has NO rendering -- Python client handles all visuals |
| Custom reliable UDP protocol (MTP) | Standard TCP (simpler, Python-native) |
| Lua API with 100+ `minetest.*` functions | Small Python API: WorldView + Object + Action base classes |
| Mod files on disk, loaded by server | Players write code IN-GAME, upload to server |
| Content defined by mod authors outside the game | Content defined by players INSIDE the game |

## What We CAN Learn From

- Chunk-based voxel storage (16x16x16 blocks) -- good design, we'll use similar
- Variable timestep with dtime -- proven approach
- Spatial indexing for entities -- needed for performance
- Greedy meshing for voxel rendering -- well-known algorithm
- Delta compression for network sync -- essential for bandwidth
- Separate threads for generation/meshing -- necessary for responsiveness

## Files

- `LUANTI_ENGINE.md` -- How the Luanti C++ engine works (loops, threading, networking)
- `MINETEST_GAME.md` -- What content the Minetest Game contains (nodes, recipes, tools)

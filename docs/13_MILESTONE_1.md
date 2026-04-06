# Milestone 1: Playable Flat World Sandbox

Walk around a smooth, beautiful voxel world. C++ server + C++ client, same process.

---

## Deliverables

- Open a window, render a flat voxel world
- WASD + mouse look to move around (FPS controls)
- Flat terrain: grass on top, dirt underneath, stone deeper
- A few block types with distinct colors/textures
- Smooth graphics: MSAA, per-vertex AO, smooth lighting, fog, sky

## Tech Stack

| Component | Choice | Reason |
|-----------|--------|--------|
| Windowing / Input | SDL2 (system installed) | Already available, battle-tested |
| GL Loading | GLAD (FetchContent) | Clean, generated for GL 4.1 core |
| Math | GLM (FetchContent) | Industry standard, header-only |
| Textures | Procedural (code-generated) | No external assets needed yet |
| Build | CMake 4.3 + GCC 13.3 | Available on system |
| Server | In-process thread | Networking deferred to milestone 2 |

## Visual Quality Targets

```
Must have (Milestone 1):
  [x] MSAA 4x anti-aliasing
  [x] Per-vertex ambient occlusion (smooth corner shadows)
  [x] Face-dependent shading (top bright, sides medium, bottom dark)
  [x] Distance fog (smooth fade to sky color)
  [x] Sky gradient (horizon to zenith)
  [x] Trilinear + anisotropic texture filtering
  [x] Greedy meshing (efficient, fewer triangles)

Future milestones:
  [ ] Shadow mapping (directional sun shadows)
  [ ] SSAO (screen-space ambient occlusion)
  [ ] Bloom + HDR tone mapping
  [ ] Water reflections / refractions
  [ ] Volumetric fog / god rays
  [ ] PBR materials
```

## Architecture

```
main()
  |
  |-- Create Server (flat world, runs in background thread)
  |     |-- Generate chunks (16x16x16 blocks)
  |     |-- Flat terrain: y<0 stone, y<4 dirt, y==4 grass, y>4 air
  |
  |-- Create Client
  |     |-- Init SDL2 window + OpenGL 4.1 context + MSAA
  |     |-- Load shaders
  |     |-- Generate procedural texture atlas
  |
  |-- Game Loop (client main thread)
        |
        |-- Poll SDL events (input)
        |-- Update camera (mouse look + WASD movement)
        |-- Request chunks from server near camera
        |-- Build meshes for new/dirty chunks (greedy mesh + AO)
        |-- Render:
        |     1. Clear + set sky gradient
        |     2. Draw terrain (chunk meshes)
        |     3. Draw crosshair
        |-- SDL_GL_SwapWindow
```

## File Structure

```
ModCraft/
  CMakeLists.txt
  src/
    main.cpp                 -- entry point, game loop
    common/
      types.h                -- Vec3i, AABB, constants
      block.h                -- BlockType enum, block properties
      chunk.h                -- Chunk data (16^3 blocks)
      world.h                -- World (holds chunks, generates terrain)
    client/
      window.h / .cpp        -- SDL2 window + GL context
      camera.h / .cpp        -- FPS camera
      shader.h / .cpp        -- GLSL shader loading
      chunk_mesher.h / .cpp  -- greedy meshing with AO
      renderer.h / .cpp      -- render pipeline
  shaders/
    terrain.vert / .frag     -- block rendering
    sky.vert / .frag         -- sky gradient
    crosshair.vert / .frag   -- center crosshair
```

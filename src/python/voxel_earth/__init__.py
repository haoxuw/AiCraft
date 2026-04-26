"""voxel_earth — port of ryanhlewis/VoxelEarth pipeline for Solarium.

Pipeline stages
───────────────
  1. geocode     text → (lat, lng)              [api.py]
  2. download    BFS Google 3D Tiles → GLBs     [download.py, phase 2]
  3. decode      Draco decode + rotate          [C++ phase 3]
  4. voxelize    2.5D scan converter            [C++ phase 4]
  5. place       voxel grid → server chunks     [C++ phase 5]

Cache layout under ~/.voxel/ — see cache.py.

Per Solarium rules:
  - Server owns world state (Rule 3): placement runs server-side only.
  - Block palette is an artifact (Rule 1): see artifacts/blocks/voxel_earth_palette.py.
  - No new ActionProposal type (Rule 0): /visit triggers a server RPC, not an action.
"""
from .cache import VoxelCache
from .api import GoogleApi, ApiKeyMissing

__all__ = ["VoxelCache", "GoogleApi", "ApiKeyMissing"]

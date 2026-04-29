"""Voxel Earth — dynamic location.

Same engine pipeline as toronto.py, but every location-specific knob
(region file path / tile dir, world-block offsets, display name) comes
from environment variables. This is what `make world LAT=… LNG=… RADIUS=…`
invokes — `python -m voxel_earth world` orchestrates the bake/landuse
pipeline, sets these env vars, then exec's the engine. Toronto and
Wonderland are Makefile aliases that just pre-fill the location.

Env vars (set at least one of REGION or TILE_DIR):
  SOLARIUM_VOXEL_REGION       — absolute path to legacy blocks.bin
  SOLARIUM_VOXEL_TILE_DIR     — root dir for shared .vtil shards
  SOLARIUM_VOXEL_REGION_LAT   — regional anchor latitude (floor of bake lat)
  SOLARIUM_VOXEL_REGION_LNG   — regional anchor longitude (floor of bake lng)
  SOLARIUM_VOXEL_OFFSET_X/Y/Z — world-block offsets (default 0/60/0)
  SOLARIUM_VOXEL_NAME         — display name shown in HUD/picker
"""

import os

_region   = os.environ.get("SOLARIUM_VOXEL_REGION", "")
_tile_dir = os.environ.get("SOLARIUM_VOXEL_TILE_DIR", "")
_name     = os.environ.get("SOLARIUM_VOXEL_NAME", "Voxel Earth (dynamic)")

world = {
    "id":          "voxel_earth",
    "name":        _name,
    "description": "Voxel Earth region selected at launch via SOLARIUM_VOXEL_*.",

    # Same as toronto: voxel-earth chunks have many block transitions, so
    # a smaller preload radius keeps the mesher caught up.
    "preload_radius_chunks": 5,
    "starting_day": 2,

    "terrain": {
        "type":        "voxel_earth",
        "region_file": _region,
        "tile_dir":    _tile_dir,
        "region_lat":  int(os.environ.get("SOLARIUM_VOXEL_REGION_LAT", "0")),
        "region_lng":  int(os.environ.get("SOLARIUM_VOXEL_REGION_LNG", "0")),
        "offset_x":    int(os.environ.get("SOLARIUM_VOXEL_OFFSET_X", "0")),
        "offset_y":    int(os.environ.get("SOLARIUM_VOXEL_OFFSET_Y", "60")),
        "offset_z":    int(os.environ.get("SOLARIUM_VOXEL_OFFSET_Z", "0")),
    },

    # No portal / village — the city or landscape is the world.
    "portal":   False,
    "village":  None,

    "spawn": {
        "search_x":   0.0,
        "search_z":   0.0,
        "min_height": 0.0,
        "max_height": 200.0,
    },

    "mobs": [],
}

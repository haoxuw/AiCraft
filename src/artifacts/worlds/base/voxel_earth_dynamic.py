"""Voxel Earth — dynamic location.

Same engine pipeline as toronto.py, but every location-specific knob
(region file path, world-block offsets, display name) comes from
environment variables. This is what `make world LAT=… LNG=… RADIUS=…`
invokes — `python -m voxel_earth world` orchestrates the bake/landuse
pipeline, sets these env vars, then exec's the engine. Toronto and
Wonderland are Makefile aliases that just pre-fill the location.

Env vars (all optional except SOLARIUM_VOXEL_REGION):
  SOLARIUM_VOXEL_REGION    — absolute path to blocks.bin (REQUIRED)
  SOLARIUM_VOXEL_OFFSET_X  — world-block offset X (default 0)
  SOLARIUM_VOXEL_OFFSET_Y  — world-block offset Y (default 60)
  SOLARIUM_VOXEL_OFFSET_Z  — world-block offset Z (default 0)
  SOLARIUM_VOXEL_NAME      — display name shown in HUD/picker
"""

import os

_region = os.environ.get("SOLARIUM_VOXEL_REGION", "")
_name   = os.environ.get("SOLARIUM_VOXEL_NAME", "Voxel Earth (dynamic)")

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

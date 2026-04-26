"""Toronto World — a voxelised slab of downtown Toronto.

Built from Google Photorealistic 3D Tiles via the voxel_earth pipeline:

  1. python -m voxel_earth set-key <YOUR_KEY>
  2. python -m voxel_earth download --location "Toronto" --radius 100
  3. ./build/civcraft-voxel-bake \\
        --glb-dir ~/.voxel/google/glb \\
        --out     ~/.voxel/regions/toronto/blocks.bin \\
        --voxel-size 1.0
  4. ./build/civcraft-server --template 6

The bake step writes a single VEAR-format file containing the full region
(228k voxels for the 100m default). The server reads that file at boot;
chunks come straight from voxel lookups, no Perlin and no villages.
"""

import os

world = {
    "id":          "toronto",
    "name":        "Toronto (Voxel Earth)",
    "description": "Downtown Toronto baked from Google Photorealistic 3D Tiles.",

    # Smaller than village (16) — voxel_earth chunks have far more block transitions
    # than rolling terrain, so the client mesher gets backed up at large radii.
    "preload_radius_chunks": 5,
    "starting_day": 2,         # summer skies over the city

    "terrain": {
        "type":        "voxel_earth",
        "region_file": os.path.expanduser("~/.voxel/regions/toronto/blocks.bin"),
        # World-block offsets — region (0,0,0) lands at (offset_x, offset_y, offset_z).
        # Region's own bbox is roughly (-65..194, -9..96, -80..203) in metres,
        # so offset_y = 60 puts ground around y=51 and tallest spire at ~y=156.
        "offset_x": 0,
        "offset_y": 60,
        "offset_z": 0,
    },

    # No portal, no village — the city is the world.
    "portal":   False,
    "village":  None,

    # Spawn near the region center; preferredSpawn() in C++ overrides this with
    # the actual roof-top Y once the region is loaded.
    "spawn": {
        "search_x":   0.0,
        "search_z":   0.0,
        "min_height": 0.0,
        "max_height": 200.0,
    },

    "mobs": [],   # quiet city for first cut
}

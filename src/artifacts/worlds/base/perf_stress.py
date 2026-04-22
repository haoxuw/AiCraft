"""Perf Stress — 100 villagers on a flat world for FPS/tick benchmarking.

Not a real gameplay world. All villagers run woodcutter behavior, so the
server sees 100 concurrent scan_blocks sweeps over tree radius, hammering
the LocalWorld shared_mutex and exercising the DecideWorker round-robin.
The client's AgentClient owns every mob on the solo-play side, so `agent`
and `chunks` probes stay lively too.

Launch:
    make perf_fps    PERF_TEMPLATE=6
    make perf_server PERF_TEMPLATE=6
"""

world = {
    "id":          "perf_stress",
    "name":        "Perf Stress (100 villagers)",
    "description": "Flat terrain, 100 villagers, many trees — benchmarking only.",

    "preload_radius_chunks": 12,

    # Gentle terrain so villagers don't wedge on cliffs; still natural enough
    # for trees to emit. Keep amplitude low (no hills) — we want physics cost
    # from mob density, not from pathfinding around terrain.
    "terrain": {
        "type":                "natural",
        "base_height":         8,
        "continent_scale":     200.0,
        "continent_amplitude": 2.0,
    },

    "spawn":  {"x": 0.0, "z": 0.0},
    "portal": False,

    # Dense trees so 100 woodcutters don't fight over the same 3 trunks.
    "trees": {
        "density":          0.03,
        "trunk_height_min": 5,
        "trunk_height_max": 7,
        "leaf_radius":      2,
    },

    # A single central monument ring. Villagers spawn on a radius-40 ring so
    # they're spread across the render radius — bunching them all together
    # would collapse 100 physics capsules into one overlapping cluster and
    # hide the real per-entity cost.
    "village": {
        "offset_x":         0,
        "offset_z":         0,
        "clearing_radius": 50,
        "houses": [
            {"cx": 0, "cz": 0, "w": 8, "d": 8, "stories": 1},
        ],
        "wall_block":  "cobblestone",
        "roof_block":  "wood",
        "floor_block": "cobblestone",
        "path_block":  "cobblestone",
        "story_height": 6,
        "door_height":  3,
        "window_row":   2,
    },

    "mobs": [
        {"type": "villager", "count": 100, "radius": 40, "spawn_at": "monument",
         "props": {
             "collect_goal": 3,
             "work_radius":  60.0,
         }},
    ],
}

"""Chicken — small round bird with thin legs.

Fast leg cycles, wings flap slightly, head bobs.
Edit parts to customize the chicken's appearance!

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

import math

model = {
    "id": "chicken",
    "height": 0.7,
    "scale": 1.25,
    "walk_speed": 9.0,
    "idle_bob": 0.005,
    "walk_bob": 0.015,
    "head_pivot": [0, 0.45, -0.10],
    "parts": [
        # Body (round-ish)
        {"name": "torso",
         "offset": [0, 0.32, 0], "size": [0.32, 0.28, 0.44], "color": [0.95, 0.95, 0.90, 1]},
        # Head
        {"name": "head", "head": True,
         "offset": [0, 0.55, -0.24], "size": [0.20, 0.20, 0.20], "color": [0.95, 0.95, 0.92, 1],
         "pivot": [0, 0.45, -0.10], "swing_axis": [1, 0, 0], "amplitude": 15, "phase": 0, "speed": 1.5},
        # Beak
        {"head": True,
         "offset": [0, 0.52, -0.35], "size": [0.08, 0.06, 0.10], "color": [0.95, 0.70, 0.20, 1],
         "pivot": [0, 0.45, -0.10], "swing_axis": [1, 0, 0], "amplitude": 15, "phase": 0, "speed": 1.5},
        # Comb
        {"head": True,
         "offset": [0, 0.66, -0.22], "size": [0.06, 0.10, 0.12], "color": [0.90, 0.15, 0.10, 1],
         "pivot": [0, 0.45, -0.10], "swing_axis": [1, 0, 0], "amplitude": 15, "phase": 0, "speed": 1.5},
        # Wattle
        {"head": True,
         "offset": [0, 0.44, -0.32], "size": [0.06, 0.08, 0.04], "color": [0.90, 0.20, 0.15, 1],
         "pivot": [0, 0.45, -0.10], "swing_axis": [1, 0, 0], "amplitude": 15, "phase": 0, "speed": 1.5},
        # Left wing
        {"offset": [-0.18, 0.33, 0.02], "size": [0.08, 0.20, 0.32], "color": [0.92, 0.92, 0.87, 1],
         "pivot": [-0.14, 0.42, 0], "swing_axis": [0, 0, 1], "amplitude": 12, "phase": 0, "speed": 1},
        # Right wing
        {"offset": [0.18, 0.33, 0.02], "size": [0.08, 0.20, 0.32], "color": [0.92, 0.92, 0.87, 1],
         "pivot": [0.14, 0.42, 0], "swing_axis": [0, 0, 1], "amplitude": 12, "phase": math.pi, "speed": 1},
        # Left leg
        {"offset": [-0.07, 0.08, 0], "size": [0.06, 0.20, 0.06], "color": [0.90, 0.70, 0.20, 1],
         "pivot": [-0.07, 0.18, 0], "swing_axis": [1, 0, 0], "amplitude": 35, "phase": 0, "speed": 1},
        # Right leg
        {"offset": [0.07, 0.08, 0], "size": [0.06, 0.20, 0.06], "color": [0.90, 0.70, 0.20, 1],
         "pivot": [0.07, 0.18, 0], "swing_axis": [1, 0, 0], "amplitude": 35, "phase": math.pi, "speed": 1},
        # Tail feathers
        {"offset": [0, 0.42, 0.26], "size": [0.16, 0.24, 0.08], "color": [0.88, 0.88, 0.82, 1]},
    ]
}

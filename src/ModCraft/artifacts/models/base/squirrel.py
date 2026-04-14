"""Squirrel — small quick rodent with a huge bushy tail.

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

import math

model = {
    "id": "squirrel",
    "height": 0.3,
    "scale": 1.0,
    "walk_speed": 10.0,
    "idle_bob": 0.006,
    "walk_bob": 0.020,
    "head_pivot": [0, 0.26, -0.10],
    "parts": [
        # Body — small and compact
        {"name": "torso",
         "offset": [0, 0.18, 0], "size": [0.16, 0.16, 0.28], "color": [0.55, 0.32, 0.15, 1]},
        # Head — perched forward
        {"name": "head", "head": True,
         "offset": [0, 0.28, -0.20], "size": [0.16, 0.14, 0.14], "color": [0.58, 0.34, 0.17, 1],
         "pivot": [0, 0.26, -0.10], "swing_axis": [1, 0, 0], "amplitude": 10, "phase": 0, "speed": 1.0},
        # Cream muzzle / belly
        {"head": True,
         "offset": [0, 0.24, -0.275], "size": [0.08, 0.05, 0.03], "color": [0.92, 0.88, 0.78, 1],
         "pivot": [0, 0.26, -0.10], "swing_axis": [1, 0, 0], "amplitude": 10, "phase": 0, "speed": 1.0},
        # Nose tip — small, dark
        {"head": True,
         "offset": [0, 0.26, -0.291], "size": [0.02, 0.02, 0.01], "color": [0.12, 0.08, 0.06, 1],
         "pivot": [0, 0.26, -0.10], "swing_axis": [1, 0, 0], "amplitude": 10, "phase": 0, "speed": 1.0},
        # Beady black eyes
        {"head": True,
         "offset": [-0.06, 0.30, -0.271], "size": [0.02, 0.025, 0.01], "color": [0.04, 0.03, 0.03, 1],
         "pivot": [0, 0.26, -0.10], "swing_axis": [1, 0, 0], "amplitude": 10, "phase": 0, "speed": 1.0},
        {"head": True,
         "offset": [ 0.06, 0.30, -0.271], "size": [0.02, 0.025, 0.01], "color": [0.04, 0.03, 0.03, 1],
         "pivot": [0, 0.26, -0.10], "swing_axis": [1, 0, 0], "amplitude": 10, "phase": 0, "speed": 1.0},
        # Ears — tufted, tall
        {"head": True,
         "offset": [-0.055, 0.38, -0.18], "size": [0.04, 0.07, 0.04], "color": [0.50, 0.28, 0.12, 1],
         "pivot": [0, 0.26, -0.10], "swing_axis": [1, 0, 0], "amplitude": 10, "phase": 0, "speed": 1.0},
        {"head": True,
         "offset": [ 0.055, 0.38, -0.18], "size": [0.04, 0.07, 0.04], "color": [0.50, 0.28, 0.12, 1],
         "pivot": [0, 0.26, -0.10], "swing_axis": [1, 0, 0], "amplitude": 10, "phase": 0, "speed": 1.0},
        # White belly patch
        {"offset": [0, 0.115, -0.04], "size": [0.10, 0.04, 0.16], "color": [0.92, 0.88, 0.78, 1]},
        # Front legs (shorter)
        {"offset": [-0.06, 0.06, -0.09], "size": [0.05, 0.12, 0.05], "color": [0.52, 0.30, 0.13, 1],
         "pivot": [-0.06, 0.12, -0.09], "swing_axis": [1, 0, 0], "amplitude": 38, "phase": 0, "speed": 1},
        {"offset": [ 0.06, 0.06, -0.09], "size": [0.05, 0.12, 0.05], "color": [0.52, 0.30, 0.13, 1],
         "pivot": [ 0.06, 0.12, -0.09], "swing_axis": [1, 0, 0], "amplitude": 38, "phase": math.pi, "speed": 1},
        # Back legs (larger for jumping)
        {"offset": [-0.07, 0.06, 0.08], "size": [0.06, 0.12, 0.08], "color": [0.52, 0.30, 0.13, 1],
         "pivot": [-0.07, 0.12, 0.08], "swing_axis": [1, 0, 0], "amplitude": 38, "phase": math.pi, "speed": 1},
        {"offset": [ 0.07, 0.06, 0.08], "size": [0.06, 0.12, 0.08], "color": [0.52, 0.30, 0.13, 1],
         "pivot": [ 0.07, 0.12, 0.08], "swing_axis": [1, 0, 0], "amplitude": 38, "phase": 0, "speed": 1},
        # ── Bushy tail — the signature ──
        # Lower tail base
        {"offset": [0, 0.24, 0.18], "size": [0.10, 0.12, 0.08], "color": [0.62, 0.38, 0.18, 1],
         "pivot": [0, 0.22, 0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 2},
        # Tail middle (arching up)
        {"offset": [0, 0.36, 0.20], "size": [0.12, 0.14, 0.08], "color": [0.65, 0.40, 0.20, 1],
         "pivot": [0, 0.22, 0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 2},
        # Tail tip (fluffy top, lighter)
        {"offset": [0, 0.48, 0.16], "size": [0.14, 0.12, 0.10], "color": [0.75, 0.55, 0.28, 1],
         "pivot": [0, 0.22, 0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 2},
    ]
}

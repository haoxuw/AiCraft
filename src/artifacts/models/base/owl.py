"""Owl — round brown-and-cream bird with huge forward-facing yellow eyes.

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

import math

model = {
    "id": "owl",
    "head_pivot": [0.0, 0.96, 0.0],
    "walk_bob": 0.04,
    "idle_bob": 0.02,
    "walk_speed": 3.0,
    "parts": [
        {"name": "torso", "offset": [0.0, 0.6, 0.0], "size": [0.68, 0.72, 0.6], "color": [0.45, 0.3, 0.15, 1.0]},
        {"offset": [0.0, 0.56, -0.28], "size": [0.48, 0.56, 0.08], "color": [0.85, 0.75, 0.55, 1.0]},
        {"offset": [0.0, 0.44, -0.322], "size": [0.4, 0.02, 0.02], "color": [0.35, 0.22, 0.1, 1.0]},
        {"offset": [0.0, 0.6, -0.322], "size": [0.4, 0.02, 0.02], "color": [0.35, 0.22, 0.1, 1.0]},
        {"offset": [0.0, 0.76, -0.322], "size": [0.4, 0.02, 0.02], "color": [0.35, 0.22, 0.1, 1.0]},
        {"name": "head", "offset": [0.0, 1.16, 0.0], "size": [0.68, 0.56, 0.56], "color": [0.48, 0.32, 0.16, 1.0], "pivot": [0.0, 0.96, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 4, "phase": 0, "speed": 1.0, "head": True},
        {"offset": [0.0, 1.12, -0.28], "size": [0.52, 0.48, 0.04], "color": [0.88, 0.8, 0.62, 1.0], "pivot": [0.0, 0.96, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 4, "phase": 0, "speed": 1.0, "head": True},
        {"offset": [-0.16, 1.16, -0.31], "size": [0.18, 0.2, 0.02], "color": [0.95, 0.78, 0.15, 1.0], "pivot": [0.0, 0.96, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 4, "phase": 0, "speed": 1.0, "head": True},
        {"offset": [0.16, 1.16, -0.31], "size": [0.18, 0.2, 0.02], "color": [0.95, 0.78, 0.15, 1.0], "pivot": [0.0, 0.96, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 4, "phase": 0, "speed": 1.0, "head": True},
        {"offset": [-0.16, 1.16, -0.324], "size": [0.08, 0.1, 0.01], "color": [0.06, 0.04, 0.03, 1.0], "pivot": [0.0, 0.96, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 4, "phase": 0, "speed": 1.0, "head": True},
        {"offset": [0.16, 1.16, -0.324], "size": [0.08, 0.1, 0.01], "color": [0.06, 0.04, 0.03, 1.0], "pivot": [0.0, 0.96, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 4, "phase": 0, "speed": 1.0, "head": True},
        {"offset": [0.0, 1.04, -0.31], "size": [0.08, 0.12, 0.06], "color": [0.72, 0.55, 0.2, 1.0], "pivot": [0.0, 0.96, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 4, "phase": 0, "speed": 1.0, "head": True},
        {"offset": [-0.24, 1.52, 0.04], "size": [0.12, 0.24, 0.12], "color": [0.35, 0.22, 0.1, 1.0], "pivot": [0.0, 0.96, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 4, "phase": 0, "speed": 1.0, "head": True},
        {"offset": [0.24, 1.52, 0.04], "size": [0.12, 0.24, 0.12], "color": [0.35, 0.22, 0.1, 1.0], "pivot": [0.0, 0.96, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 4, "phase": 0, "speed": 1.0, "head": True},
        {"name": "wing_l", "offset": [-0.36, 0.6, 0.0], "size": [0.12, 0.52, 0.44], "color": [0.38, 0.24, 0.12, 1.0], "pivot": [-0.3, 0.84, 0.0], "swing_axis": [0.0, 0.0, 1.0], "amplitude": 6, "phase": 0, "speed": 1.5},
        {"name": "wing_r", "offset": [0.36, 0.6, 0.0], "size": [0.12, 0.52, 0.44], "color": [0.38, 0.24, 0.12, 1.0], "pivot": [0.3, 0.84, 0.0], "swing_axis": [0.0, 0.0, 1.0], "amplitude": 6, "phase": 3.1416, "speed": 1.5},
        {"offset": [0.0, 0.4, 0.36], "size": [0.44, 0.16, 0.12], "color": [0.4, 0.26, 0.13, 1.0]},
        {"name": "leg_l", "offset": [-0.14, 0.14, 0.04], "size": [0.1, 0.2, 0.1], "color": [0.7, 0.55, 0.25, 1.0], "pivot": [-0.14, 0.24, 0.04], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 20, "phase": 0, "speed": 1},
        {"name": "leg_r", "offset": [0.14, 0.14, 0.04], "size": [0.1, 0.2, 0.1], "color": [0.7, 0.55, 0.25, 1.0], "pivot": [0.14, 0.24, 0.04], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 20, "phase": 3.1416, "speed": 1},
    ],
    "clips": {
        "fly": {
            "wing_l": {"axis": [0.0, 0.0, 1.0], "amplitude": 18, "phase": 0, "bias": -80, "speed": 6},
            "wing_r": {"axis": [0.0, 0.0, 1.0], "amplitude": -18, "phase": 0, "bias": 80, "speed": 6},
            "leg_l": {"axis": [1.0, 0.0, 0.0], "amplitude": 0, "bias": -70, "speed": 1},
            "leg_r": {"axis": [1.0, 0.0, 0.0], "amplitude": 0, "bias": -70, "speed": 1},
        },
        "land": {
            "wing_l": {"axis": [0.0, 0.0, 1.0], "amplitude": 20, "phase": 0, "bias": -35, "speed": 3},
            "wing_r": {"axis": [0.0, 0.0, 1.0], "amplitude": -20, "phase": 0, "bias": 35, "speed": 3},
            "leg_l": {"axis": [1.0, 0.0, 0.0], "amplitude": 5, "bias": 30, "speed": 1.5},
            "leg_r": {"axis": [1.0, 0.0, 0.0], "amplitude": 5, "bias": 30, "speed": 1.5},
        },
    },
}

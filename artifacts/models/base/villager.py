"""Villager — humanoid NPC with hat, robe, nose.

Edit parts to customize the villager's appearance!

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

import math

model = {
    "id": "villager",
    "height": 1.8,
    "scale": 1.0,
    "hand_r":  [ 0.40,  0.73, -0.12],
    "hand_l":  [-0.40,  0.73, -0.12],
    "pivot_r": [ 0.32,  1.35,  0.00],
    "pivot_l": [-0.32,  1.35,  0.00],
    "walk_speed": 3.0,
    "idle_bob": 0.010,
    "walk_bob": 0.04,
    "parts": [
        # Head
        {"offset": [0, 1.55, 0], "size": [0.40, 0.40, 0.40], "color": [0.85, 0.72, 0.58, 1],
         "pivot": [0, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 4, "phase": 0, "speed": 2},
        # Hat brim
        {"offset": [0, 1.76, 0], "size": [0.44, 0.08, 0.44], "color": [0.45, 0.30, 0.15, 1],
         "pivot": [0, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 4, "phase": 0, "speed": 2},
        # Hat crown
        {"offset": [0, 1.83, 0], "size": [0.28, 0.16, 0.28], "color": [0.45, 0.30, 0.15, 1],
         "pivot": [0, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 4, "phase": 0, "speed": 2},
        # Nose
        {"offset": [0, 1.50, -0.20], "size": [0.08, 0.12, 0.08], "color": [0.78, 0.62, 0.48, 1],
         "pivot": [0, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 4, "phase": 0, "speed": 2},
        # Torso (brown robe)
        {"offset": [0, 1.05, 0], "size": [0.44, 0.60, 0.28], "color": [0.55, 0.38, 0.20, 1]},
        # Robe lower
        {"offset": [0, 0.72, 0], "size": [0.48, 0.24, 0.32], "color": [0.50, 0.35, 0.18, 1]},
        # Belt
        {"offset": [0, 0.85, -0.13], "size": [0.40, 0.06, 0.04], "color": [0.25, 0.15, 0.08, 1]},
        # Left arm
        {"offset": [-0.32, 1.05, 0], "size": [0.16, 0.60, 0.16], "color": [0.55, 0.38, 0.20, 1],
         "pivot": [-0.32, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
        # Right arm
        {"offset": [0.32, 1.05, 0], "size": [0.16, 0.60, 0.16], "color": [0.55, 0.38, 0.20, 1],
         "pivot": [0.32, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        # Left hand
        {"offset": [-0.32, 0.73, 0], "size": [0.12, 0.08, 0.12], "color": [0.85, 0.72, 0.58, 1],
         "pivot": [-0.32, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
        # Right hand
        {"offset": [0.32, 0.73, 0], "size": [0.12, 0.08, 0.12], "color": [0.85, 0.72, 0.58, 1],
         "pivot": [0.32, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        # Left leg
        {"offset": [-0.10, 0.30, 0], "size": [0.20, 0.60, 0.20], "color": [0.50, 0.35, 0.18, 1],
         "pivot": [-0.10, 0.60, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        # Right leg
        {"offset": [0.10, 0.30, 0], "size": [0.20, 0.60, 0.20], "color": [0.50, 0.35, 0.18, 1],
         "pivot": [0.10, 0.60, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
    ]
}

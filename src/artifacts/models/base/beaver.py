"""Beaver — stocky brown rodent with orange teeth and a flat paddle tail.

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

import math

model = {
    "id": "beaver",
    "head_pivot": [0.0, 0.68, -0.32],
    "walk_bob": 0.04,
    "idle_bob": 0.01,
    "walk_speed": 3.5,
    "parts": [
        {"name": "torso", "offset": [0.0, 0.56, 0.0], "size": [0.64, 0.56, 1.0], "color": [0.42, 0.26, 0.14, 1.0]},
        {"offset": [0.0, 0.3, 0.0], "size": [0.56, 0.06, 0.88], "color": [0.55, 0.38, 0.22, 1.0]},
        {"name": "head", "offset": [0.0, 0.72, -0.6], "size": [0.52, 0.44, 0.48], "color": [0.44, 0.28, 0.15, 1.0], "pivot": [0.0, 0.68, -0.32], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 6, "phase": 0, "speed": 0.8, "head": True},
        {"offset": [0.0, 0.58, -0.88], "size": [0.28, 0.16, 0.12], "color": [0.52, 0.35, 0.2, 1.0], "pivot": [0.0, 0.68, -0.32], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 6, "phase": 0, "speed": 0.8, "head": True},
        {"offset": [0.0, 0.64, -0.952], "size": [0.1, 0.08, 0.02], "color": [0.08, 0.05, 0.04, 1.0], "pivot": [0.0, 0.68, -0.32], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 6, "phase": 0, "speed": 0.8, "head": True},
        {"offset": [0.0, 0.51, -0.952], "size": [0.12, 0.08, 0.02], "color": [0.92, 0.62, 0.15, 1.0], "pivot": [0.0, 0.68, -0.32], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 6, "phase": 0, "speed": 0.8, "head": True},
        {"offset": [-0.15, 0.8, -0.852], "size": [0.05, 0.06, 0.01], "color": [0.04, 0.03, 0.03, 1.0], "pivot": [0.0, 0.68, -0.32], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 6, "phase": 0, "speed": 0.8, "head": True},
        {"offset": [0.15, 0.8, -0.852], "size": [0.05, 0.06, 0.01], "color": [0.04, 0.03, 0.03, 1.0], "pivot": [0.0, 0.68, -0.32], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 6, "phase": 0, "speed": 0.8, "head": True},
        {"offset": [-0.2, 0.98, -0.56], "size": [0.1, 0.1, 0.08], "color": [0.32, 0.2, 0.1, 1.0], "pivot": [0.0, 0.68, -0.32], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 6, "phase": 0, "speed": 0.8, "head": True},
        {"offset": [0.2, 0.98, -0.56], "size": [0.1, 0.1, 0.08], "color": [0.32, 0.2, 0.1, 1.0], "pivot": [0.0, 0.68, -0.32], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 6, "phase": 0, "speed": 0.8, "head": True},
        {"offset": [-0.24, 0.14, -0.24], "size": [0.16, 0.28, 0.2], "color": [0.32, 0.2, 0.1, 1.0], "pivot": [-0.24, 0.32, -0.24], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 30, "phase": 0, "speed": 1},
        {"offset": [0.24, 0.14, -0.24], "size": [0.16, 0.28, 0.2], "color": [0.32, 0.2, 0.1, 1.0], "pivot": [0.24, 0.32, -0.24], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 30, "phase": 3.1416, "speed": 1},
        {"offset": [-0.24, 0.14, 0.28], "size": [0.16, 0.28, 0.2], "color": [0.32, 0.2, 0.1, 1.0], "pivot": [-0.24, 0.32, 0.28], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 30, "phase": 3.1416, "speed": 1},
        {"offset": [0.24, 0.14, 0.28], "size": [0.16, 0.28, 0.2], "color": [0.32, 0.2, 0.1, 1.0], "pivot": [0.24, 0.32, 0.28], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 30, "phase": 0, "speed": 1},
        {"offset": [0.0, 0.44, 0.6], "size": [0.16, 0.12, 0.12], "color": [0.3, 0.2, 0.1, 1.0], "pivot": [0.0, 0.44, 0.56], "swing_axis": [0.0, 1.0, 0.0], "amplitude": 6, "phase": 0, "speed": 1.5},
        {"offset": [0.0, 0.38, 0.88], "size": [0.44, 0.08, 0.48], "color": [0.22, 0.14, 0.08, 1.0], "pivot": [0.0, 0.44, 0.56], "swing_axis": [0.0, 1.0, 0.0], "amplitude": 8, "phase": 0, "speed": 1.5},
        {"offset": [0.0, 0.422, 0.72], "size": [0.4, 0.02, 0.04], "color": [0.12, 0.08, 0.05, 1.0], "pivot": [0.0, 0.44, 0.56], "swing_axis": [0.0, 1.0, 0.0], "amplitude": 8, "phase": 0, "speed": 1.5},
        {"offset": [0.0, 0.422, 0.88], "size": [0.4, 0.02, 0.04], "color": [0.12, 0.08, 0.05, 1.0], "pivot": [0.0, 0.44, 0.56], "swing_axis": [0.0, 1.0, 0.0], "amplitude": 8, "phase": 0, "speed": 1.5},
        {"offset": [0.0, 0.422, 1.04], "size": [0.4, 0.02, 0.04], "color": [0.12, 0.08, 0.05, 1.0], "pivot": [0.0, 0.44, 0.56], "swing_axis": [0.0, 1.0, 0.0], "amplitude": 8, "phase": 0, "speed": 1.5},
    ],
}

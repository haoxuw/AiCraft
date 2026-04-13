"""Cat — sleek, pointy ears, long tail with separate tip.

Edit parts to customize the cat's appearance!

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

import math

model = {
    "id": "cat",
    "height": 0.5,
    "scale": 1.0,
    "walk_speed": 6.0,
    "idle_bob": 0.004,
    "walk_bob": 0.018,
    "head_pivot": [0, 0.34, -0.18],
    "parts": [
        # Body (sleek, elongated)
        {"name": "torso",
         "offset": [0, 0.25, 0], "size": [0.24, 0.20, 0.56], "color": [0.90, 0.55, 0.20, 1]},
        # Head
        {"name": "head", "head": True,
         "offset": [0, 0.36, -0.30], "size": [0.24, 0.22, 0.22], "color": [0.92, 0.58, 0.22, 1],
         "pivot": [0, 0.34, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.5},
        # Left ear
        {"head": True,
         "offset": [-0.08, 0.48, -0.28], "size": [0.06, 0.12, 0.06], "color": [0.85, 0.50, 0.18, 1]},
        # Right ear
        {"head": True,
         "offset": [0.08, 0.48, -0.28], "size": [0.06, 0.12, 0.06], "color": [0.85, 0.50, 0.18, 1]},
        # Front-left leg
        {"offset": [-0.06, 0.08, -0.16], "size": [0.06, 0.20, 0.06], "color": [0.88, 0.52, 0.18, 1],
         "pivot": [-0.06, 0.18, -0.16], "swing_axis": [1, 0, 0], "amplitude": 35, "phase": 0, "speed": 1},
        # Front-right leg
        {"offset": [0.06, 0.08, -0.16], "size": [0.06, 0.20, 0.06], "color": [0.88, 0.52, 0.18, 1],
         "pivot": [0.06, 0.18, -0.16], "swing_axis": [1, 0, 0], "amplitude": 35, "phase": math.pi, "speed": 1},
        # Back-left leg
        {"offset": [-0.06, 0.08, 0.16], "size": [0.06, 0.20, 0.06], "color": [0.88, 0.52, 0.18, 1],
         "pivot": [-0.06, 0.18, 0.16], "swing_axis": [1, 0, 0], "amplitude": 35, "phase": math.pi, "speed": 1},
        # Back-right leg
        {"offset": [0.06, 0.08, 0.16], "size": [0.06, 0.20, 0.06], "color": [0.88, 0.52, 0.18, 1],
         "pivot": [0.06, 0.18, 0.16], "swing_axis": [1, 0, 0], "amplitude": 35, "phase": 0, "speed": 1},
        # Tail
        {"offset": [0, 0.32, 0.32], "size": [0.04, 0.04, 0.24], "color": [0.85, 0.50, 0.18, 1],
         "pivot": [0, 0.28, 0.28], "swing_axis": [1, 0, 0], "amplitude": 15, "phase": 0, "speed": 1.5},
        # Tail tip
        {"offset": [0, 0.38, 0.44], "size": [0.04, 0.04, 0.12], "color": [0.80, 0.45, 0.15, 1],
         "pivot": [0, 0.28, 0.28], "swing_axis": [1, 0, 0], "amplitude": 20, "phase": 0.5, "speed": 1.5},
    ]
}

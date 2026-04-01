"""Pig — cute farm animal model.

Pink body with 4 stubby legs, round head, floppy ears, and a curly tail.
Edit parts to customize the pig's appearance!

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase, speed
"""

import math

model = {
    "id": "pig",
    "height": 0.9,
    "scale": 1.25,
    "walk_speed": 7.0,
    "parts": [
        # Body (fat)
        {"offset": [0, 0.45, 0], "size": [0.60, 0.50, 0.80], "color": [0.90, 0.72, 0.68, 1]},
        # Head
        {"offset": [0, 0.55, -0.45], "size": [0.44, 0.40, 0.40], "color": [0.92, 0.75, 0.70, 1],
         "pivot": [0, 0.55, -0.25], "swing_axis": [1,0,0], "amplitude": 8, "speed": 0.5},
        # Snout
        {"offset": [0, 0.48, -0.64], "size": [0.24, 0.16, 0.10], "color": [0.95, 0.65, 0.60, 1],
         "pivot": [0, 0.55, -0.25], "swing_axis": [1,0,0], "amplitude": 8, "speed": 0.5},
        # Ears
        {"offset": [-0.18, 0.72, -0.42], "size": [0.12, 0.08, 0.16], "color": [0.88, 0.65, 0.60, 1]},
        {"offset": [ 0.18, 0.72, -0.42], "size": [0.12, 0.08, 0.16], "color": [0.88, 0.65, 0.60, 1]},
        # Front legs
        {"offset": [-0.18, 0.15, -0.25], "size": [0.14, 0.30, 0.14], "color": [0.88, 0.68, 0.63, 1],
         "pivot": [-0.18, 0.30, -0.25], "swing_axis": [1,0,0], "amplitude": 30, "phase": 0},
        {"offset": [ 0.18, 0.15, -0.25], "size": [0.14, 0.30, 0.14], "color": [0.88, 0.68, 0.63, 1],
         "pivot": [ 0.18, 0.30, -0.25], "swing_axis": [1,0,0], "amplitude": 30, "phase": math.pi},
        # Back legs
        {"offset": [-0.18, 0.15, 0.25], "size": [0.14, 0.30, 0.14], "color": [0.88, 0.68, 0.63, 1],
         "pivot": [-0.18, 0.30, 0.25], "swing_axis": [1,0,0], "amplitude": 30, "phase": math.pi},
        {"offset": [ 0.18, 0.15, 0.25], "size": [0.14, 0.30, 0.14], "color": [0.88, 0.68, 0.63, 1],
         "pivot": [ 0.18, 0.30, 0.25], "swing_axis": [1,0,0], "amplitude": 30, "phase": 0},
        # Curly tail
        {"offset": [0, 0.55, 0.42], "size": [0.08, 0.08, 0.12], "color": [0.92, 0.70, 0.65, 1],
         "pivot": [0, 0.50, 0.40], "swing_axis": [0,1,0], "amplitude": 15, "speed": 2},
    ]
}

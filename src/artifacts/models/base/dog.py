"""Dog — loyal companion, 4 legs, pointy ears, wagging tail.

Edit parts to customize the dog's appearance!

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

import math

model = {
    "id": "dog",
    "height": 0.7,
    "scale": 1.43,
    "walk_speed": 5.0,
    "idle_bob": 0.006,
    "walk_bob": 0.025,
    "head_pivot": [0, 0.50, -0.22],
    "parts": [
        # Body
        {"name": "torso",
         "offset": [0, 0.38, 0], "size": [0.36, 0.32, 0.70], "color": [0.75, 0.55, 0.35, 1]},
        # Head
        {"name": "head", "head": True,
         "offset": [0, 0.52, -0.38], "size": [0.32, 0.28, 0.32], "color": [0.78, 0.58, 0.38, 1],
         "pivot": [0, 0.50, -0.22], "swing_axis": [1, 0, 0], "amplitude": 10, "phase": 0, "speed": 0.5},
        # Snout
        {"head": True,
         "offset": [0, 0.46, -0.54], "size": [0.16, 0.12, 0.12], "color": [0.70, 0.48, 0.30, 1],
         "pivot": [0, 0.50, -0.22], "swing_axis": [1, 0, 0], "amplitude": 10, "phase": 0, "speed": 0.5},
        # Black nose tip
        {"head": True,
         "offset": [0, 0.50, -0.61], "size": [0.06, 0.04, 0.02], "color": [0.08, 0.06, 0.05, 1],
         "pivot": [0, 0.50, -0.22], "swing_axis": [1, 0, 0], "amplitude": 10, "phase": 0, "speed": 0.5},
        # Eyes (black dots on head front)
        {"head": True,
         "offset": [-0.08, 0.56, -0.545], "size": [0.03, 0.04, 0.01], "color": [0.04, 0.03, 0.03, 1],
         "pivot": [0, 0.50, -0.22], "swing_axis": [1, 0, 0], "amplitude": 10, "phase": 0, "speed": 0.5},
        {"head": True,
         "offset": [ 0.08, 0.56, -0.545], "size": [0.03, 0.04, 0.01], "color": [0.04, 0.03, 0.03, 1],
         "pivot": [0, 0.50, -0.22], "swing_axis": [1, 0, 0], "amplitude": 10, "phase": 0, "speed": 0.5},
        # Left ear (pushed out so outer face doesn't z-fight with head x-face)
        {"head": True,
         "offset": [-0.125, 0.66, -0.34], "size": [0.08, 0.16, 0.08], "color": [0.65, 0.42, 0.25, 1]},
        # Right ear
        {"head": True,
         "offset": [0.125, 0.66, -0.34], "size": [0.08, 0.16, 0.08], "color": [0.65, 0.42, 0.25, 1]},
        # Front-left leg
        {"offset": [-0.10, 0.12, -0.20], "size": [0.10, 0.28, 0.10], "color": [0.72, 0.52, 0.32, 1],
         "pivot": [-0.10, 0.26, -0.20], "swing_axis": [1, 0, 0], "amplitude": 40, "phase": 0, "speed": 1},
        # Front-right leg
        {"offset": [0.10, 0.12, -0.20], "size": [0.10, 0.28, 0.10], "color": [0.72, 0.52, 0.32, 1],
         "pivot": [0.10, 0.26, -0.20], "swing_axis": [1, 0, 0], "amplitude": 40, "phase": math.pi, "speed": 1},
        # Back-left leg
        {"offset": [-0.10, 0.12, 0.20], "size": [0.10, 0.28, 0.10], "color": [0.72, 0.52, 0.32, 1],
         "pivot": [-0.10, 0.26, 0.20], "swing_axis": [1, 0, 0], "amplitude": 40, "phase": math.pi, "speed": 1},
        # Back-right leg
        {"offset": [0.10, 0.12, 0.20], "size": [0.10, 0.28, 0.10], "color": [0.72, 0.52, 0.32, 1],
         "pivot": [0.10, 0.26, 0.20], "swing_axis": [1, 0, 0], "amplitude": 40, "phase": 0, "speed": 1},
        # Tail (wags on Y axis)
        {"offset": [0, 0.48, 0.38], "size": [0.06, 0.06, 0.20], "color": [0.72, 0.52, 0.32, 1],
         "pivot": [0, 0.45, 0.35], "swing_axis": [0, 1, 0], "amplitude": 25, "phase": 0, "speed": 3},
        # Red collar
        {"offset": [0, 0.46, -0.32], "size": [0.38, 0.08, 0.10], "color": [0.75, 0.15, 0.15, 1]},
        # Gold collar tag
        {"offset": [0, 0.40, -0.39], "size": [0.04, 0.05, 0.02], "color": [0.85, 0.70, 0.20, 1]},
        # White chest patch
        {"offset": [0, 0.28, -0.36], "size": [0.14, 0.10, 0.04], "color": [0.92, 0.90, 0.85, 1]},
    ]
}

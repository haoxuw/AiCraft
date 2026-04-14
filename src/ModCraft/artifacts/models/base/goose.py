"""Goose — long-necked white waterfowl with a bright orange beak.

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

import math

model = {
    "id": "goose",
    "height": 0.95,
    "scale": 1.0,
    "walk_speed": 3.5,
    "idle_bob": 0.008,
    "walk_bob": 0.030,
    "head_pivot": [0, 0.78, -0.18],
    "parts": [
        # Body — plump oval, sits up from legs
        {"name": "torso",
         "offset": [0, 0.50, 0.04], "size": [0.32, 0.34, 0.48], "color": [0.94, 0.93, 0.90, 1]},
        # Upper back bump
        {"offset": [0, 0.68, 0.08], "size": [0.26, 0.08, 0.36], "color": [0.90, 0.89, 0.86, 1]},
        # ── Long neck — three segments rising forward ──
        {"offset": [0, 0.62, -0.15], "size": [0.12, 0.14, 0.12], "color": [0.93, 0.92, 0.88, 1],
         "pivot": [0, 0.56, -0.18], "swing_axis": [1, 0, 0], "amplitude": 5, "phase": 0, "speed": 1.0},
        {"offset": [0, 0.72, -0.19], "size": [0.11, 0.14, 0.11], "color": [0.93, 0.92, 0.88, 1],
         "pivot": [0, 0.56, -0.18], "swing_axis": [1, 0, 0], "amplitude": 5, "phase": 0, "speed": 1.0},
        # Head — small relative to body, forward of neck
        {"name": "head", "head": True,
         "offset": [0, 0.86, -0.24], "size": [0.16, 0.14, 0.18], "color": [0.94, 0.93, 0.90, 1],
         "pivot": [0, 0.78, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 1.2},
        # ── Bright orange beak (signature) ──
        {"head": True,
         "offset": [0, 0.83, -0.37], "size": [0.08, 0.07, 0.10], "color": [0.95, 0.55, 0.12, 1],
         "pivot": [0, 0.78, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 1.2},
        # Black nostrils (on beak top)
        {"head": True,
         "offset": [0, 0.862, -0.38], "size": [0.04, 0.01, 0.02], "color": [0.10, 0.08, 0.06, 1],
         "pivot": [0, 0.78, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 1.2},
        # Eyes — small round dark dots
        {"head": True,
         "offset": [-0.081, 0.88, -0.26], "size": [0.01, 0.03, 0.03], "color": [0.04, 0.03, 0.03, 1],
         "pivot": [0, 0.78, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 1.2},
        {"head": True,
         "offset": [ 0.081, 0.88, -0.26], "size": [0.01, 0.03, 0.03], "color": [0.04, 0.03, 0.03, 1],
         "pivot": [0, 0.78, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 1.2},
        # Wings — folded flat to body, faint bob
        {"offset": [-0.17, 0.52, 0.06], "size": [0.04, 0.24, 0.32], "color": [0.88, 0.87, 0.84, 1],
         "pivot": [-0.14, 0.64, 0], "swing_axis": [0, 0, 1], "amplitude": 5, "phase": 0, "speed": 1.5},
        {"offset": [ 0.17, 0.52, 0.06], "size": [0.04, 0.24, 0.32], "color": [0.88, 0.87, 0.84, 1],
         "pivot": [ 0.14, 0.64, 0], "swing_axis": [0, 0, 1], "amplitude": 5, "phase": math.pi, "speed": 1.5},
        # Tail feathers — short raised fan
        {"offset": [0, 0.56, 0.28], "size": [0.22, 0.12, 0.06], "color": [0.90, 0.89, 0.86, 1]},
        # ── Orange webbed feet / legs ──
        {"offset": [-0.08, 0.14, 0.00], "size": [0.05, 0.28, 0.05], "color": [0.92, 0.52, 0.12, 1],
         "pivot": [-0.08, 0.30, 0], "swing_axis": [1, 0, 0], "amplitude": 40, "phase": 0, "speed": 1},
        {"offset": [ 0.08, 0.14, 0.00], "size": [0.05, 0.28, 0.05], "color": [0.92, 0.52, 0.12, 1],
         "pivot": [ 0.08, 0.30, 0], "swing_axis": [1, 0, 0], "amplitude": 40, "phase": math.pi, "speed": 1},
        # Webbed feet — flat pads
        {"offset": [-0.08, 0.015, -0.04], "size": [0.10, 0.03, 0.14], "color": [0.95, 0.58, 0.15, 1],
         "pivot": [-0.08, 0.30, 0], "swing_axis": [1, 0, 0], "amplitude": 40, "phase": 0, "speed": 1},
        {"offset": [ 0.08, 0.015, -0.04], "size": [0.10, 0.03, 0.14], "color": [0.95, 0.58, 0.15, 1],
         "pivot": [ 0.08, 0.30, 0], "swing_axis": [1, 0, 0], "amplitude": 40, "phase": math.pi, "speed": 1},
    ]
}

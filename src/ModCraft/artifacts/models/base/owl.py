"""Owl — round brown-and-cream bird with huge forward-facing yellow eyes.

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

import math

model = {
    "id": "owl",
    "height": 0.5,
    "scale": 1.0,
    "walk_speed": 3.0,
    "idle_bob": 0.010,
    "walk_bob": 0.020,
    "head_pivot": [0, 0.48, 0],
    "parts": [
        # Body — round brown barrel
        {"name": "torso",
         "offset": [0, 0.30, 0], "size": [0.34, 0.36, 0.30], "color": [0.45, 0.30, 0.15, 1]},
        # Cream chest panel
        {"offset": [0, 0.28, -0.14], "size": [0.24, 0.28, 0.04], "color": [0.85, 0.75, 0.55, 1]},
        # Chest speckle stripes (darker)
        {"offset": [0, 0.22, -0.161], "size": [0.20, 0.01, 0.01], "color": [0.35, 0.22, 0.10, 1]},
        {"offset": [0, 0.30, -0.161], "size": [0.20, 0.01, 0.01], "color": [0.35, 0.22, 0.10, 1]},
        {"offset": [0, 0.38, -0.161], "size": [0.20, 0.01, 0.01], "color": [0.35, 0.22, 0.10, 1]},
        # Head — large round
        {"name": "head", "head": True,
         "offset": [0, 0.58, 0], "size": [0.34, 0.28, 0.28], "color": [0.48, 0.32, 0.16, 1],
         "pivot": [0, 0.48, 0], "swing_axis": [1, 0, 0], "amplitude": 4, "phase": 0, "speed": 1.0},
        # Facial disc — pale heart-shape panel
        {"head": True,
         "offset": [0, 0.56, -0.14], "size": [0.26, 0.24, 0.02], "color": [0.88, 0.80, 0.62, 1],
         "pivot": [0, 0.48, 0], "swing_axis": [1, 0, 0], "amplitude": 4, "phase": 0, "speed": 1.0},
        # ── Huge yellow eyes ──
        {"head": True,
         "offset": [-0.08, 0.58, -0.155], "size": [0.09, 0.10, 0.01], "color": [0.95, 0.78, 0.15, 1],
         "pivot": [0, 0.48, 0], "swing_axis": [1, 0, 0], "amplitude": 4, "phase": 0, "speed": 1.0},
        {"head": True,
         "offset": [ 0.08, 0.58, -0.155], "size": [0.09, 0.10, 0.01], "color": [0.95, 0.78, 0.15, 1],
         "pivot": [0, 0.48, 0], "swing_axis": [1, 0, 0], "amplitude": 4, "phase": 0, "speed": 1.0},
        # Eye pupils
        {"head": True,
         "offset": [-0.08, 0.58, -0.162], "size": [0.04, 0.05, 0.005], "color": [0.06, 0.04, 0.03, 1],
         "pivot": [0, 0.48, 0], "swing_axis": [1, 0, 0], "amplitude": 4, "phase": 0, "speed": 1.0},
        {"head": True,
         "offset": [ 0.08, 0.58, -0.162], "size": [0.04, 0.05, 0.005], "color": [0.06, 0.04, 0.03, 1],
         "pivot": [0, 0.48, 0], "swing_axis": [1, 0, 0], "amplitude": 4, "phase": 0, "speed": 1.0},
        # Hooked beak (small, orange-gray, between eyes)
        {"head": True,
         "offset": [0, 0.52, -0.155], "size": [0.04, 0.06, 0.03], "color": [0.72, 0.55, 0.20, 1],
         "pivot": [0, 0.48, 0], "swing_axis": [1, 0, 0], "amplitude": 4, "phase": 0, "speed": 1.0},
        # ── Signature ear tufts ──
        {"head": True,
         "offset": [-0.12, 0.76, 0.02], "size": [0.06, 0.12, 0.06], "color": [0.35, 0.22, 0.10, 1],
         "pivot": [0, 0.48, 0], "swing_axis": [1, 0, 0], "amplitude": 4, "phase": 0, "speed": 1.0},
        {"head": True,
         "offset": [ 0.12, 0.76, 0.02], "size": [0.06, 0.12, 0.06], "color": [0.35, 0.22, 0.10, 1],
         "pivot": [0, 0.48, 0], "swing_axis": [1, 0, 0], "amplitude": 4, "phase": 0, "speed": 1.0},
        # Wings — folded at sides, slight bob (not in flight at this height)
        {"offset": [-0.18, 0.30, 0.00], "size": [0.06, 0.26, 0.22], "color": [0.38, 0.24, 0.12, 1],
         "pivot": [-0.15, 0.42, 0], "swing_axis": [0, 0, 1], "amplitude": 6, "phase": 0, "speed": 1.5},
        {"offset": [ 0.18, 0.30, 0.00], "size": [0.06, 0.26, 0.22], "color": [0.38, 0.24, 0.12, 1],
         "pivot": [ 0.15, 0.42, 0], "swing_axis": [0, 0, 1], "amplitude": 6, "phase": math.pi, "speed": 1.5},
        # Tail feathers — short stubby fan
        {"offset": [0, 0.20, 0.18], "size": [0.22, 0.08, 0.06], "color": [0.40, 0.26, 0.13, 1]},
        # Legs — short feathered stubs
        {"offset": [-0.07, 0.07, 0.02], "size": [0.05, 0.10, 0.05], "color": [0.70, 0.55, 0.25, 1],
         "pivot": [-0.07, 0.12, 0.02], "swing_axis": [1, 0, 0], "amplitude": 20, "phase": 0, "speed": 1},
        {"offset": [ 0.07, 0.07, 0.02], "size": [0.05, 0.10, 0.05], "color": [0.70, 0.55, 0.25, 1],
         "pivot": [ 0.07, 0.12, 0.02], "swing_axis": [1, 0, 0], "amplitude": 20, "phase": math.pi, "speed": 1},
    ]
}

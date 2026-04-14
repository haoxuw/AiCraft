"""Bee — tiny yellow-and-black striped flyer with translucent wings.

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

import math

model = {
    "id": "bee",
    "height": 0.25,
    "scale": 1.0,
    "walk_speed": 4.0,
    "idle_bob": 0.025,    # strong hover wobble
    "walk_bob": 0.015,
    "head_pivot": [0, 0.12, -0.08],
    "parts": [
        # Body — yellow abdomen
        {"name": "torso",
         "offset": [0, 0.12, 0.02], "size": [0.18, 0.16, 0.22], "color": [0.95, 0.78, 0.15, 1]},
        # ── Signature black stripes — thin rings on top/sides only ──
        # Shrunk width and height so yellow is the dominant color.
        {"offset": [0, 0.20, -0.04], "size": [0.19, 0.015, 0.04], "color": [0.12, 0.10, 0.08, 1]},
        {"offset": [0, 0.20,  0.06], "size": [0.19, 0.015, 0.04], "color": [0.12, 0.10, 0.08, 1]},
        # Side stripe bands (thin vertical, left/right)
        {"offset": [-0.091, 0.12, -0.04], "size": [0.005, 0.17, 0.04], "color": [0.12, 0.10, 0.08, 1]},
        {"offset": [ 0.091, 0.12, -0.04], "size": [0.005, 0.17, 0.04], "color": [0.12, 0.10, 0.08, 1]},
        {"offset": [-0.091, 0.12,  0.06], "size": [0.005, 0.17, 0.04], "color": [0.12, 0.10, 0.08, 1]},
        {"offset": [ 0.091, 0.12,  0.06], "size": [0.005, 0.17, 0.04], "color": [0.12, 0.10, 0.08, 1]},
        # Stinger (rear, small black point)
        {"offset": [0, 0.12, 0.16], "size": [0.03, 0.03, 0.04], "color": [0.08, 0.06, 0.05, 1]},
        # Head — dark, smaller
        {"name": "head", "head": True,
         "offset": [0, 0.12, -0.14], "size": [0.14, 0.14, 0.12], "color": [0.15, 0.12, 0.08, 1],
         "pivot": [0, 0.12, -0.08], "swing_axis": [1, 0, 0], "amplitude": 6, "phase": 0, "speed": 1.5},
        # Big compound eyes
        {"head": True,
         "offset": [-0.066, 0.13, -0.17], "size": [0.015, 0.08, 0.07], "color": [0.10, 0.08, 0.08, 1],
         "pivot": [0, 0.12, -0.08], "swing_axis": [1, 0, 0], "amplitude": 6, "phase": 0, "speed": 1.5},
        {"head": True,
         "offset": [ 0.066, 0.13, -0.17], "size": [0.015, 0.08, 0.07], "color": [0.10, 0.08, 0.08, 1],
         "pivot": [0, 0.12, -0.08], "swing_axis": [1, 0, 0], "amplitude": 6, "phase": 0, "speed": 1.5},
        # Tiny antennae
        {"head": True,
         "offset": [-0.04, 0.22, -0.16], "size": [0.012, 0.08, 0.012], "color": [0.10, 0.08, 0.06, 1],
         "pivot": [0, 0.12, -0.08], "swing_axis": [1, 0, 0], "amplitude": 6, "phase": 0, "speed": 1.5},
        {"head": True,
         "offset": [ 0.04, 0.22, -0.16], "size": [0.012, 0.08, 0.012], "color": [0.10, 0.08, 0.06, 1],
         "pivot": [0, 0.12, -0.08], "swing_axis": [1, 0, 0], "amplitude": 6, "phase": 0, "speed": 1.5},
        # ── Wings — buzz fast ──
        # Left wing (outward+up hinge)
        {"offset": [-0.13, 0.22, 0.00], "size": [0.12, 0.02, 0.18], "color": [0.90, 0.92, 0.95, 1],
         "pivot": [-0.05, 0.22, 0.00], "swing_axis": [0, 0, 1], "amplitude": 22, "phase": 0, "speed": 12},
        # Right wing
        {"offset": [ 0.13, 0.22, 0.00], "size": [0.12, 0.02, 0.18], "color": [0.90, 0.92, 0.95, 1],
         "pivot": [ 0.05, 0.22, 0.00], "swing_axis": [0, 0, 1], "amplitude": 22, "phase": math.pi, "speed": 12},
        # Tiny legs (mostly decorative dangles)
        {"offset": [-0.07, 0.02, -0.04], "size": [0.02, 0.06, 0.02], "color": [0.10, 0.08, 0.06, 1]},
        {"offset": [ 0.07, 0.02, -0.04], "size": [0.02, 0.06, 0.02], "color": [0.10, 0.08, 0.06, 1]},
        {"offset": [-0.07, 0.02,  0.06], "size": [0.02, 0.06, 0.02], "color": [0.10, 0.08, 0.06, 1]},
        {"offset": [ 0.07, 0.02,  0.06], "size": [0.02, 0.06, 0.02], "color": [0.10, 0.08, 0.06, 1]},
    ]
}

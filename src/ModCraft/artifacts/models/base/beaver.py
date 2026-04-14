"""Beaver — stocky brown rodent with orange teeth and a flat paddle tail.

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

import math

model = {
    "id": "beaver",
    "height": 0.5,
    "scale": 1.0,
    "walk_speed": 3.5,
    "idle_bob": 0.005,
    "walk_bob": 0.020,
    "head_pivot": [0, 0.34, -0.16],
    "parts": [
        # Body — rounded brown
        {"name": "torso",
         "offset": [0, 0.28, 0], "size": [0.32, 0.28, 0.50], "color": [0.42, 0.26, 0.14, 1]},
        # Paler belly
        {"offset": [0, 0.15, 0], "size": [0.28, 0.03, 0.44], "color": [0.55, 0.38, 0.22, 1]},
        # Head — large blocky
        {"name": "head", "head": True,
         "offset": [0, 0.36, -0.30], "size": [0.26, 0.22, 0.24], "color": [0.44, 0.28, 0.15, 1],
         "pivot": [0, 0.34, -0.16], "swing_axis": [1, 0, 0], "amplitude": 6, "phase": 0, "speed": 0.8},
        # Muzzle — lighter
        {"head": True,
         "offset": [0, 0.29, -0.44], "size": [0.14, 0.08, 0.06], "color": [0.52, 0.35, 0.20, 1],
         "pivot": [0, 0.34, -0.16], "swing_axis": [1, 0, 0], "amplitude": 6, "phase": 0, "speed": 0.8},
        # Black nose tip
        {"head": True,
         "offset": [0, 0.32, -0.476], "size": [0.05, 0.04, 0.01], "color": [0.08, 0.05, 0.04, 1],
         "pivot": [0, 0.34, -0.16], "swing_axis": [1, 0, 0], "amplitude": 6, "phase": 0, "speed": 0.8},
        # ── Signature orange buck teeth ──
        {"head": True,
         "offset": [0, 0.255, -0.476], "size": [0.06, 0.04, 0.01], "color": [0.92, 0.62, 0.15, 1],
         "pivot": [0, 0.34, -0.16], "swing_axis": [1, 0, 0], "amplitude": 6, "phase": 0, "speed": 0.8},
        # Eyes — small beady dots
        {"head": True,
         "offset": [-0.075, 0.40, -0.426], "size": [0.025, 0.03, 0.005], "color": [0.04, 0.03, 0.03, 1],
         "pivot": [0, 0.34, -0.16], "swing_axis": [1, 0, 0], "amplitude": 6, "phase": 0, "speed": 0.8},
        {"head": True,
         "offset": [ 0.075, 0.40, -0.426], "size": [0.025, 0.03, 0.005], "color": [0.04, 0.03, 0.03, 1],
         "pivot": [0, 0.34, -0.16], "swing_axis": [1, 0, 0], "amplitude": 6, "phase": 0, "speed": 0.8},
        # Round ears — small, on top
        {"head": True,
         "offset": [-0.10, 0.49, -0.28], "size": [0.05, 0.05, 0.04], "color": [0.32, 0.20, 0.10, 1],
         "pivot": [0, 0.34, -0.16], "swing_axis": [1, 0, 0], "amplitude": 6, "phase": 0, "speed": 0.8},
        {"head": True,
         "offset": [ 0.10, 0.49, -0.28], "size": [0.05, 0.05, 0.04], "color": [0.32, 0.20, 0.10, 1],
         "pivot": [0, 0.34, -0.16], "swing_axis": [1, 0, 0], "amplitude": 6, "phase": 0, "speed": 0.8},
        # Stubby legs
        {"offset": [-0.12, 0.07, -0.12], "size": [0.08, 0.14, 0.10], "color": [0.32, 0.20, 0.10, 1],
         "pivot": [-0.12, 0.16, -0.12], "swing_axis": [1, 0, 0], "amplitude": 30, "phase": 0, "speed": 1},
        {"offset": [ 0.12, 0.07, -0.12], "size": [0.08, 0.14, 0.10], "color": [0.32, 0.20, 0.10, 1],
         "pivot": [ 0.12, 0.16, -0.12], "swing_axis": [1, 0, 0], "amplitude": 30, "phase": math.pi, "speed": 1},
        {"offset": [-0.12, 0.07, 0.14], "size": [0.08, 0.14, 0.10], "color": [0.32, 0.20, 0.10, 1],
         "pivot": [-0.12, 0.16, 0.14], "swing_axis": [1, 0, 0], "amplitude": 30, "phase": math.pi, "speed": 1},
        {"offset": [ 0.12, 0.07, 0.14], "size": [0.08, 0.14, 0.10], "color": [0.32, 0.20, 0.10, 1],
         "pivot": [ 0.12, 0.16, 0.14], "swing_axis": [1, 0, 0], "amplitude": 30, "phase": 0, "speed": 1},
        # ── Signature flat paddle tail ──
        # Thin stub connecting body → paddle
        {"offset": [0, 0.22, 0.30], "size": [0.08, 0.06, 0.06], "color": [0.30, 0.20, 0.10, 1],
         "pivot": [0, 0.22, 0.28], "swing_axis": [0, 1, 0], "amplitude": 6, "phase": 0, "speed": 1.5},
        # The wide flat paddle
        {"offset": [0, 0.19, 0.44], "size": [0.22, 0.04, 0.24], "color": [0.22, 0.14, 0.08, 1],
         "pivot": [0, 0.22, 0.28], "swing_axis": [0, 1, 0], "amplitude": 8, "phase": 0, "speed": 1.5},
        # Cross-hatch texture strips (darker) for the scaled-paddle look
        {"offset": [0, 0.211, 0.36], "size": [0.20, 0.01, 0.02], "color": [0.12, 0.08, 0.05, 1],
         "pivot": [0, 0.22, 0.28], "swing_axis": [0, 1, 0], "amplitude": 8, "phase": 0, "speed": 1.5},
        {"offset": [0, 0.211, 0.44], "size": [0.20, 0.01, 0.02], "color": [0.12, 0.08, 0.05, 1],
         "pivot": [0, 0.22, 0.28], "swing_axis": [0, 1, 0], "amplitude": 8, "phase": 0, "speed": 1.5},
        {"offset": [0, 0.211, 0.52], "size": [0.20, 0.01, 0.02], "color": [0.12, 0.08, 0.05, 1],
         "pivot": [0, 0.22, 0.28], "swing_axis": [0, 1, 0], "amplitude": 8, "phase": 0, "speed": 1.5},
    ]
}

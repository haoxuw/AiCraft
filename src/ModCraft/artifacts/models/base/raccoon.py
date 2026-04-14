"""Raccoon — chunky trash-bandit with a ringed tail and black eye mask.

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

import math

model = {
    "id": "raccoon",
    "height": 0.55,
    "scale": 1.0,
    "walk_speed": 5.5,
    "idle_bob": 0.007,
    "walk_bob": 0.025,
    "head_pivot": [0, 0.42, -0.18],
    "parts": [
        # Body — chunky gray
        {"name": "torso",
         "offset": [0, 0.32, 0], "size": [0.32, 0.28, 0.56], "color": [0.45, 0.45, 0.48, 1]},
        # Darker dorsal stripe
        {"offset": [0, 0.461, 0], "size": [0.18, 0.01, 0.50], "color": [0.22, 0.22, 0.24, 1]},
        # Paler belly
        {"offset": [0, 0.188, 0], "size": [0.28, 0.03, 0.50], "color": [0.60, 0.58, 0.55, 1]},
        # Head
        {"name": "head", "head": True,
         "offset": [0, 0.44, -0.32], "size": [0.28, 0.24, 0.24], "color": [0.55, 0.55, 0.58, 1],
         "pivot": [0, 0.42, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.8},
        # Pale muzzle — cream/white
        {"head": True,
         "offset": [0, 0.38, -0.455], "size": [0.14, 0.08, 0.05], "color": [0.90, 0.88, 0.82, 1],
         "pivot": [0, 0.42, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.8},
        # Black nose tip
        {"head": True,
         "offset": [0, 0.40, -0.486], "size": [0.04, 0.04, 0.02], "color": [0.08, 0.06, 0.05, 1],
         "pivot": [0, 0.42, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.8},
        # ── Signature bandit mask — black band across eyes ──
        {"head": True,
         "offset": [-0.08, 0.47, -0.441], "size": [0.12, 0.06, 0.01], "color": [0.10, 0.08, 0.08, 1],
         "pivot": [0, 0.42, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.8},
        {"head": True,
         "offset": [ 0.08, 0.47, -0.441], "size": [0.12, 0.06, 0.01], "color": [0.10, 0.08, 0.08, 1],
         "pivot": [0, 0.42, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.8},
        # Eyes — small bright dots inside the mask
        {"head": True,
         "offset": [-0.08, 0.47, -0.448], "size": [0.03, 0.03, 0.005], "color": [0.92, 0.85, 0.30, 1],
         "pivot": [0, 0.42, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.8},
        {"head": True,
         "offset": [ 0.08, 0.47, -0.448], "size": [0.03, 0.03, 0.005], "color": [0.92, 0.85, 0.30, 1],
         "pivot": [0, 0.42, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.8},
        # Ears — rounded, on top
        {"head": True,
         "offset": [-0.10, 0.59, -0.28], "size": [0.06, 0.08, 0.06], "color": [0.38, 0.38, 0.42, 1],
         "pivot": [0, 0.42, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.8},
        {"head": True,
         "offset": [ 0.10, 0.59, -0.28], "size": [0.06, 0.08, 0.06], "color": [0.38, 0.38, 0.42, 1],
         "pivot": [0, 0.42, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.8},
        # Legs — stubby dark
        {"offset": [-0.10, 0.10, -0.18], "size": [0.08, 0.20, 0.08], "color": [0.28, 0.26, 0.26, 1],
         "pivot": [-0.10, 0.22, -0.18], "swing_axis": [1, 0, 0], "amplitude": 35, "phase": 0, "speed": 1},
        {"offset": [ 0.10, 0.10, -0.18], "size": [0.08, 0.20, 0.08], "color": [0.28, 0.26, 0.26, 1],
         "pivot": [ 0.10, 0.22, -0.18], "swing_axis": [1, 0, 0], "amplitude": 35, "phase": math.pi, "speed": 1},
        {"offset": [-0.10, 0.10, 0.18], "size": [0.08, 0.20, 0.08], "color": [0.28, 0.26, 0.26, 1],
         "pivot": [-0.10, 0.22, 0.18], "swing_axis": [1, 0, 0], "amplitude": 35, "phase": math.pi, "speed": 1},
        {"offset": [ 0.10, 0.10, 0.18], "size": [0.08, 0.20, 0.08], "color": [0.28, 0.26, 0.26, 1],
         "pivot": [ 0.10, 0.22, 0.18], "swing_axis": [1, 0, 0], "amplitude": 35, "phase": 0, "speed": 1},
        # ── Ringed tail — alternating light/dark bands ──
        {"offset": [0, 0.40, 0.35], "size": [0.10, 0.10, 0.10], "color": [0.55, 0.55, 0.58, 1],
         "pivot": [0, 0.40, 0.30], "swing_axis": [0, 1, 0], "amplitude": 10, "phase": 0, "speed": 2},
        {"offset": [0, 0.40, 0.44], "size": [0.09, 0.09, 0.08], "color": [0.18, 0.18, 0.20, 1],
         "pivot": [0, 0.40, 0.30], "swing_axis": [0, 1, 0], "amplitude": 12, "phase": 0, "speed": 2},
        {"offset": [0, 0.40, 0.52], "size": [0.085, 0.085, 0.08], "color": [0.55, 0.55, 0.58, 1],
         "pivot": [0, 0.40, 0.30], "swing_axis": [0, 1, 0], "amplitude": 14, "phase": 0, "speed": 2},
        {"offset": [0, 0.40, 0.60], "size": [0.08, 0.08, 0.08], "color": [0.18, 0.18, 0.20, 1],
         "pivot": [0, 0.40, 0.30], "swing_axis": [0, 1, 0], "amplitude": 16, "phase": 0, "speed": 2},
        # Tail tip — dark
        {"offset": [0, 0.40, 0.68], "size": [0.07, 0.07, 0.06], "color": [0.08, 0.08, 0.10, 1],
         "pivot": [0, 0.40, 0.30], "swing_axis": [0, 1, 0], "amplitude": 18, "phase": 0, "speed": 2},
    ]
}

"""Crewmate -- Among Us astronaut (helmet IS the head).

The iconic Among Us astronaut. Sus.
STR 2  STA 3  AGI 3  INT 4

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

import math

model = {
    "id": "crewmate",
    "height": 1.65,
    "scale": 1.0,
    "hand_r":  [ 0.54,  0.82, -0.12],
    "hand_l":  [-0.54,  0.82, -0.12],
    "pivot_r": [ 0.36,  1.02,  0.00],
    "pivot_l": [-0.36,  1.02,  0.00],
    "walk_speed": 1.5,
    "idle_bob": 0.015,
    "walk_bob": 0.055,
    "parts": [
        # Visor = head for face texture (must be parts[0])
        {"offset": [0, 1.08, -0.21], "size": [0.44, 0.42, 0.04], "color": [0.38, 0.92, 0.85, 1]},
        # Body egg -- lower
        {"offset": [0, 0.64, 0], "size": [0.68, 0.68, 0.52], "color": [0.85, 0.18, 0.18, 1]},
        # Body egg -- upper
        {"offset": [0, 1.17, 0], "size": [0.54, 0.44, 0.44], "color": [0.85, 0.18, 0.18, 1]},
        # Body egg -- dome
        {"offset": [0, 1.50, 0], "size": [0.36, 0.24, 0.32], "color": [0.85, 0.18, 0.18, 1]},
        # Body base trim
        {"offset": [0, 0.34, 0], "size": [0.60, 0.10, 0.44], "color": [0.52, 0.10, 0.10, 1]},
        # Backpack
        {"offset": [0, 0.82, 0.32], "size": [0.34, 0.52, 0.24], "color": [0.52, 0.10, 0.10, 1]},
        # O2 port
        {"offset": [0, 0.92, 0.43], "size": [0.10, 0.10, 0.04], "color": [0.34, 0.06, 0.06, 1]},
        # Strap
        {"offset": [0, 1.08, 0.26], "size": [0.24, 0.06, 0.12], "color": [0.42, 0.08, 0.08, 1]},
        # Left arm
        {"name": "left_hand",
         "offset": [-0.46, 0.82, 0], "size": [0.20, 0.30, 0.20], "color": [0.68, 0.14, 0.14, 1],
         "pivot": [-0.36, 1.02, 0], "swing_axis": [1, 0, 0], "amplitude": 45, "phase": math.pi, "speed": 1},
        # Right arm
        {"name": "right_hand",
         "offset": [0.46, 0.82, 0], "size": [0.20, 0.30, 0.20], "color": [0.68, 0.14, 0.14, 1],
         "pivot": [0.36, 1.02, 0], "swing_axis": [1, 0, 0], "amplitude": 45, "phase": 0, "speed": 1},
        # Left leg
        {"offset": [-0.15, 0.15, 0], "size": [0.28, 0.30, 0.36], "color": [0.68, 0.14, 0.14, 1],
         "pivot": [-0.15, 0.32, 0], "swing_axis": [1, 0, 0], "amplitude": 40, "phase": 0, "speed": 1},
        # Right leg
        {"offset": [0.15, 0.15, 0], "size": [0.28, 0.30, 0.36], "color": [0.68, 0.14, 0.14, 1],
         "pivot": [0.15, 0.32, 0], "swing_axis": [1, 0, 0], "amplitude": 40, "phase": math.pi, "speed": 1},
        # Left sole
        {"offset": [-0.15, 0.02, 0.01], "size": [0.30, 0.06, 0.36], "color": [0.42, 0.08, 0.08, 1],
         "pivot": [-0.15, 0.32, 0], "swing_axis": [1, 0, 0], "amplitude": 40, "phase": 0, "speed": 1},
        # Right sole
        {"offset": [0.15, 0.02, 0.01], "size": [0.30, 0.06, 0.36], "color": [0.42, 0.08, 0.08, 1],
         "pivot": [0.15, 0.32, 0], "swing_axis": [1, 0, 0], "amplitude": 40, "phase": math.pi, "speed": 1},
    ],

    # Clips target the merged arm-hand parts (crewmate has no separate forearm).
    "clips": {
        "attack": {
            "right_hand": {"axis": [1, 0, 0], "amp": 60, "bias": -30, "speed": 3.0, "phase": 0},
        },
        "chop": {
            "right_hand": {"axis": [1, 0, 0], "amp": 35, "bias": -70, "speed": 1.2, "phase": 0},
        },
        "mine": {
            "right_hand": {"axis": [1, 0, 0], "amp": 40, "bias": -60, "speed": 1.4, "phase": 0},
        },
        "wave": {
            "right_hand": {"axis": [0, 0, 1], "amp": 25, "bias": -150, "speed": 2.0, "phase": 0},
        },
        "dance": {
            "right_hand": {"axis": [0, 0, 1], "amp": 40, "bias": -100, "speed": 1.5, "phase": 0},
            "left_hand":  {"axis": [0, 0, 1], "amp": 40, "bias":  100, "speed": 1.5, "phase": 0},
        },
        "sleep": {
            "right_hand": {"axis": [1, 0, 0], "amp": 0, "bias": 0, "speed": 0.5, "phase": 0},
            "left_hand":  {"axis": [1, 0, 0], "amp": 0, "bias": 0, "speed": 0.5, "phase": 0},
        },
    },
}

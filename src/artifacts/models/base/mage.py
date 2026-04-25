"""Purple Mage -- arcane robes, tall hat, staff with gem.

Wielder of arcane arts, draped in star-dusted robes.
STR 1  STA 2  AGI 3  INT 5

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

import math

model = {
    "id": "mage",
    "hand_r": [0.52, 0.82, -0.12],
    "hand_l": [-0.52, 0.82, -0.12],
    "pivot_r": [0.32, 1.4, 0.0],
    "pivot_l": [-0.32, 1.4, 0.0],
    "walk_bob": 0.04,
    "idle_bob": 0.01,
    "walk_speed": 1.7,
    "parts": [
        {"offset": [0.0, 1.65, 0.0], "size": [0.44, 0.44, 0.44], "color": [0.92, 0.82, 0.7, 1.0], "pivot": [0.0, 1.44, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 5, "phase": 0, "speed": 2},
        {"offset": [0.0, 1.48, -0.2], "size": [0.28, 0.14, 0.08], "color": [0.92, 0.9, 0.86, 1.0], "pivot": [0.0, 1.44, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 5, "phase": 0, "speed": 2, "head": True},
        {"offset": [0.0, 1.64, -0.24], "size": [0.04, 0.06, 0.04], "color": [0.78, 0.62, 0.5, 1.0], "pivot": [0.0, 1.44, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 5, "phase": 0, "speed": 2, "head": True},
        {"offset": [-0.08, 1.72, -0.231], "size": [0.04, 0.03, 0.01], "color": [0.35, 0.55, 0.9, 1.0], "pivot": [0.0, 1.44, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 5, "phase": 0, "speed": 2, "head": True},
        {"offset": [0.08, 1.72, -0.231], "size": [0.04, 0.03, 0.01], "color": [0.35, 0.55, 0.9, 1.0], "pivot": [0.0, 1.44, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 5, "phase": 0, "speed": 2, "head": True},
        {"offset": [0.0, 0.4, 0.0], "size": [0.68, 0.2, 0.48], "color": [0.36, 0.06, 0.54, 1.0]},
        {"offset": [0.0, 0.64, 0.0], "size": [0.6, 0.48, 0.44], "color": [0.45, 0.1, 0.65, 1.0]},
        {"offset": [0.0, 1.08, 0.0], "size": [0.44, 0.44, 0.32], "color": [0.45, 0.1, 0.65, 1.0]},
        {"offset": [0.0, 1.08, -0.15], "size": [0.28, 0.32, 0.04], "color": [0.6, 0.2, 0.84, 1.0]},
        {"offset": [0.0, 0.87, -0.21], "size": [0.36, 0.06, 0.04], "color": [0.8, 0.68, 0.12, 1.0]},
        {"offset": [0.0, 0.87, -0.23], "size": [0.08, 0.08, 0.02], "color": [0.95, 0.88, 0.2, 1.0]},
        {"offset": [0.0, 1.89, 0.0], "size": [0.56, 0.08, 0.56], "color": [0.22, 0.05, 0.34, 1.0], "pivot": [0.0, 1.44, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 5, "phase": 0, "speed": 2},
        {"offset": [0.0, 1.94, 0.0], "size": [0.42, 0.05, 0.42], "color": [0.8, 0.68, 0.12, 1.0], "pivot": [0.0, 1.44, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 5, "phase": 0, "speed": 2},
        {"offset": [0.0, 2.02, 0.0], "size": [0.36, 0.16, 0.36], "color": [0.22, 0.05, 0.34, 1.0], "pivot": [0.0, 1.44, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 5, "phase": 0, "speed": 2},
        {"offset": [0.0, 2.16, 0.0], "size": [0.24, 0.2, 0.24], "color": [0.22, 0.05, 0.34, 1.0], "pivot": [0.0, 1.44, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 5, "phase": 0, "speed": 2},
        {"offset": [0.0, 2.32, 0.0], "size": [0.14, 0.28, 0.14], "color": [0.22, 0.05, 0.34, 1.0], "pivot": [0.0, 1.44, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 5, "phase": 0, "speed": 2},
        {"offset": [0.07, 2.1, -0.12], "size": [0.06, 0.06, 0.02], "color": [0.95, 0.88, 0.2, 1.0], "pivot": [0.0, 1.44, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 5, "phase": 0, "speed": 2},
        {"name": "left_upper_arm", "offset": [-0.32, 1.08, 0.0], "size": [0.2, 0.6, 0.2], "color": [0.45, 0.1, 0.65, 1.0], "pivot": [-0.32, 1.4, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 3.1416, "speed": 1},
        {"name": "left_forearm", "offset": [-0.32, 0.815, 0.0], "size": [0.28, 0.1, 0.24], "color": [0.36, 0.06, 0.54, 1.0], "pivot": [-0.32, 1.4, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 3.1416, "speed": 1},
        {"name": "left_hand", "offset": [-0.32, 0.74, 0.0], "size": [0.16, 0.1, 0.14], "color": [0.92, 0.82, 0.7, 1.0], "pivot": [-0.32, 1.4, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 3.1416, "speed": 1},
        {"name": "right_upper_arm", "offset": [0.32, 1.08, 0.0], "size": [0.2, 0.6, 0.2], "color": [0.45, 0.1, 0.65, 1.0], "pivot": [0.32, 1.4, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 0, "speed": 1},
        {"name": "right_forearm", "offset": [0.32, 0.815, 0.0], "size": [0.28, 0.1, 0.24], "color": [0.36, 0.06, 0.54, 1.0], "pivot": [0.32, 1.4, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 0, "speed": 1},
        {"name": "right_hand", "offset": [0.32, 0.74, 0.0], "size": [0.16, 0.1, 0.14], "color": [0.92, 0.82, 0.7, 1.0], "pivot": [0.32, 1.4, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 0, "speed": 1},
        {"offset": [0.44, 0.82, -0.08], "size": [0.06, 0.96, 0.06], "color": [0.4, 0.28, 0.12, 1.0], "pivot": [0.32, 1.4, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 0, "speed": 1},
        {"offset": [0.44, 1.34, -0.08], "size": [0.18, 0.18, 0.18], "color": [0.38, 0.7, 1.0, 1.0], "pivot": [0.32, 1.4, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 0, "speed": 1},
        {"offset": [0.44, 1.34, -0.08], "size": [0.1, 0.1, 0.1], "color": [0.76, 0.92, 1.0, 1.0], "pivot": [0.32, 1.4, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 0, "speed": 1},
        {"offset": [-0.1, 0.22, 0.0], "size": [0.18, 0.44, 0.2], "color": [0.32, 0.06, 0.48, 1.0], "pivot": [-0.1, 0.48, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 45, "phase": 0, "speed": 1},
        {"offset": [0.1, 0.22, 0.0], "size": [0.18, 0.44, 0.2], "color": [0.32, 0.06, 0.48, 1.0], "pivot": [0.1, 0.48, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 45, "phase": 3.1416, "speed": 1},
    ],
    "clips": {
        "attack": {
            "right_upper_arm": {"axis": [1.0, 0.0, 0.0], "amp": 60, "phase": 0, "bias": 30, "speed": 3.0},
            "right_forearm": {"axis": [1.0, 0.0, 0.0], "amp": 60, "phase": 0, "bias": 30, "speed": 3.0},
            "right_hand": {"axis": [1.0, 0.0, 0.0], "amp": 60, "phase": 0, "bias": 30, "speed": 3.0},
        },
        "chop": {
            "right_upper_arm": {"axis": [1.0, 0.0, 0.0], "amp": 35, "phase": 0, "bias": 70, "speed": 1.2},
            "right_forearm": {"axis": [1.0, 0.0, 0.0], "amp": 35, "phase": 0, "bias": 70, "speed": 1.2},
            "right_hand": {"axis": [1.0, 0.0, 0.0], "amp": 35, "phase": 0, "bias": 70, "speed": 1.2},
        },
        "mine": {
            "right_upper_arm": {"axis": [1.0, 0.0, 0.0], "amp": 40, "phase": 0, "bias": 60, "speed": 1.4},
            "right_forearm": {"axis": [1.0, 0.0, 0.0], "amp": 40, "phase": 0, "bias": 60, "speed": 1.4},
            "right_hand": {"axis": [1.0, 0.0, 0.0], "amp": 40, "phase": 0, "bias": 60, "speed": 1.4},
        },
        "wave": {
            "right_upper_arm": {"axis": [0.0, 0.0, 1.0], "amp": 20, "phase": 0, "bias": 130, "speed": 2.0},
            "right_forearm": {"axis": [0.0, 0.0, 1.0], "amp": 20, "phase": 0, "bias": 130, "speed": 2.0},
            "right_hand": {"axis": [0.0, 0.0, 1.0], "amp": 20, "phase": 0, "bias": 130, "speed": 2.0},
        },
        "dance": {
            "right_upper_arm": {"axis": [0.0, 0.0, 1.0], "amp": 35, "phase": 0, "bias": 80, "speed": 1.5},
            "right_forearm": {"axis": [0.0, 0.0, 1.0], "amp": 35, "phase": 0, "bias": 80, "speed": 1.5},
            "right_hand": {"axis": [0.0, 0.0, 1.0], "amp": 35, "phase": 0, "bias": 80, "speed": 1.5},
            "left_upper_arm": {"axis": [0.0, 0.0, 1.0], "amp": 35, "phase": 3.1416, "bias": -80, "speed": 1.5},
            "left_forearm": {"axis": [0.0, 0.0, 1.0], "amp": 35, "phase": 3.1416, "bias": -80, "speed": 1.5},
            "left_hand": {"axis": [0.0, 0.0, 1.0], "amp": 35, "phase": 3.1416, "bias": -80, "speed": 1.5},
        },
        "sleep": {
            "right_upper_arm": {"axis": [1.0, 0.0, 0.0], "amp": 0, "phase": 0, "bias": 0, "speed": 0.5},
            "left_upper_arm": {"axis": [1.0, 0.0, 0.0], "amp": 0, "phase": 0, "bias": 0, "speed": 0.5},
        },
    },
}

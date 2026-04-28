"""Skeleton -- undead warrior in rusted iron.

An undead warrior draped in rusted iron.
STR 3  STA 2  AGI 4  INT 3

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

model = {
    "id": "skeleton",
    "hand_r": [0.2, 0.38, -0.12],
    "hand_l": [-0.2, 0.38, -0.12],
    "pivot_r": [0.12, 0.65, 0.0],
    "pivot_l": [-0.12, 0.65, 0.0],
    "walk_bob": 0.03,
    "idle_bob": 0.008,
    "walk_speed": 1.8,
    "parts": [
        {"offset": [0.0, 1.78, 0.0], "size": [0.46, 0.46, 0.46], "color": [0.88, 0.85, 0.78, 1.0], "pivot": [0.0, 1.55, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 6, "phase": 0, "speed": 2},
        {"offset": [0.0, 1.62, -0.02], "size": [0.36, 0.1, 0.34], "color": [0.82, 0.78, 0.7, 1.0], "pivot": [0.0, 1.55, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 6, "phase": 0, "speed": 2},
        {"offset": [0.0, 1.1, 0.0], "size": [0.12, 0.6, 0.12], "color": [0.7, 0.65, 0.58, 1.0], "pivot": [0.0, 1.1, 0.0], "swing_axis": [0.0, 1.0, 0.0], "amplitude": 4, "phase": 3.1416, "speed": 1},
        {"offset": [0.0, 1.18, 0.0], "size": [0.4, 0.36, 0.22], "color": [0.88, 0.85, 0.78, 1.0]},
        {"offset": [-0.14, 1.08, -0.06], "size": [0.16, 0.04, 0.12], "color": [0.8, 0.76, 0.68, 1.0]},
        {"offset": [0.14, 1.08, -0.06], "size": [0.16, 0.04, 0.12], "color": [0.8, 0.76, 0.68, 1.0]},
        {"offset": [0.0, 0.72, 0.0], "size": [0.36, 0.1, 0.22], "color": [0.7, 0.65, 0.58, 1.0]},
        {"offset": [0.0, 0.88, -0.08], "size": [0.32, 0.28, 0.04], "color": [0.3, 0.28, 0.25, 0.85]},
        {"offset": [0.0, 0.72, -0.1], "size": [0.4, 0.06, 0.04], "color": [0.52, 0.35, 0.22, 1.0]},
        {"name": "left_upper_arm", "offset": [-0.32, 1.4, 0.0], "size": [0.24, 0.12, 0.2], "color": [0.52, 0.35, 0.22, 1.0], "pivot": [-0.32, 1.38, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 3.1416, "speed": 1},
        {"name": "left_forearm", "offset": [-0.32, 1.1, 0.0], "size": [0.1, 0.52, 0.1], "color": [0.88, 0.85, 0.78, 1.0], "pivot": [-0.32, 1.38, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 3.1416, "speed": 1},
        {"name": "left_hand", "offset": [-0.32, 0.8, 0.0], "size": [0.12, 0.08, 0.08], "color": [0.7, 0.65, 0.58, 1.0], "pivot": [-0.32, 1.38, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 3.1416, "speed": 1},
        {"name": "right_forearm", "offset": [0.32, 1.1, 0.0], "size": [0.1, 0.52, 0.1], "color": [0.88, 0.85, 0.78, 1.0], "pivot": [0.32, 1.38, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 0, "speed": 1},
        {"name": "right_hand", "offset": [0.32, 0.8, 0.0], "size": [0.12, 0.08, 0.08], "color": [0.7, 0.65, 0.58, 1.0], "pivot": [0.32, 1.38, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 0, "speed": 1},
        {"offset": [0.08, 1.05, 0.16], "size": [0.28, 0.32, 0.04], "color": [0.42, 0.3, 0.18, 1.0]},
        {"offset": [0.08, 1.05, 0.13], "size": [0.08, 0.08, 0.04], "color": [0.52, 0.35, 0.22, 1.0]},
        {"offset": [0.0, 1.02, 0.22], "size": [0.5, 0.76, 0.03], "color": [0.22, 0.18, 0.16, 1.0], "pivot": [0.0, 1.38, 0.2], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 5, "phase": 0, "speed": 1.0},
        {"offset": [-0.16, 0.48, 0.245], "size": [0.14, 0.36, 0.02], "color": [0.14, 0.1, 0.08, 1.0], "pivot": [0.0, 1.38, 0.2], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 7, "phase": 0, "speed": 1.0},
        {"offset": [0.02, 0.52, 0.245], "size": [0.1, 0.26, 0.02], "color": [0.18, 0.14, 0.12, 1.0], "pivot": [0.0, 1.38, 0.2], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 6, "phase": 0, "speed": 1.0},
        {"offset": [0.18, 0.58, 0.245], "size": [0.12, 0.18, 0.02], "color": [0.16, 0.12, 0.1, 1.0], "pivot": [0.0, 1.38, 0.2], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 6, "phase": 0, "speed": 1.0},
        {"offset": [-0.06, 0.26, 0.255], "size": [0.06, 0.1, 0.02], "color": [0.1, 0.08, 0.06, 1.0], "pivot": [0.0, 1.38, 0.2], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 9, "phase": 0, "speed": 1.0},
        {"offset": [0.14, 0.3, 0.255], "size": [0.05, 0.12, 0.02], "color": [0.1, 0.08, 0.06, 1.0], "pivot": [0.0, 1.38, 0.2], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 9, "phase": 0, "speed": 1.0},
        {"offset": [-0.12, 0.38, 0.0], "size": [0.12, 0.44, 0.12], "color": [0.88, 0.85, 0.78, 1.0], "pivot": [-0.12, 0.66, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 0, "speed": 1},
        {"offset": [-0.12, 0.08, -0.04], "size": [0.14, 0.1, 0.22], "color": [0.7, 0.65, 0.58, 1.0], "pivot": [-0.12, 0.66, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 0, "speed": 1},
        {"offset": [0.12, 0.38, 0.0], "size": [0.12, 0.44, 0.12], "color": [0.88, 0.85, 0.78, 1.0], "pivot": [0.12, 0.66, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 3.1416, "speed": 1},
        {"offset": [0.12, 0.08, -0.04], "size": [0.14, 0.1, 0.22], "color": [0.7, 0.65, 0.58, 1.0], "pivot": [0.12, 0.66, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 3.1416, "speed": 1},
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

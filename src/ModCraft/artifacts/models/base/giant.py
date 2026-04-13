"""Giant -- massive iron guardian.

Massive iron guardian with lava-cracked chest.
STR 5  STA 5  AGI 1  INT 1

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

import math

model = {
    "id": "giant",
    "height": 2.2,
    "scale": 1.0,
    "hand_r":  [ 0.72,  0.52, -0.20],
    "hand_l":  [-0.72,  0.52, -0.20],
    "pivot_r": [ 0.48,  1.36,  0.00],
    "pivot_l": [-0.48,  1.36,  0.00],
    "walk_speed": 1.2,
    "idle_bob": 0.005,
    "walk_bob": 0.09,
    "parts": [
        # Head (texture provides face -- must be parts[0])
        {"offset": [0, 1.62, 0], "size": [0.60, 0.52, 0.56], "color": [0.42, 0.40, 0.38, 1],
         "pivot": [0, 1.46, 0], "swing_axis": [1, 0, 0], "amplitude": 3, "phase": 0, "speed": 1.5},

        # Left leg
        {"offset": [-0.22, 0.28, 0], "size": [0.36, 0.56, 0.40], "color": [0.45, 0.42, 0.40, 1],
         "pivot": [-0.22, 0.58, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        # Right leg
        {"offset": [0.22, 0.28, 0], "size": [0.36, 0.56, 0.40], "color": [0.45, 0.42, 0.40, 1],
         "pivot": [0.22, 0.58, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
        # Left foot
        {"offset": [-0.22, 0.05, 0.04], "size": [0.40, 0.16, 0.48], "color": [0.32, 0.30, 0.28, 1],
         "pivot": [-0.22, 0.58, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        # Right foot
        {"offset": [0.22, 0.05, 0.04], "size": [0.40, 0.16, 0.48], "color": [0.32, 0.30, 0.28, 1],
         "pivot": [0.22, 0.58, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
        # Lower torso
        {"offset": [0, 0.76, 0], "size": [0.80, 0.52, 0.60], "color": [0.45, 0.42, 0.40, 1]},
        # Upper torso
        {"offset": [0, 1.18, 0], "size": [0.84, 0.52, 0.56], "color": [0.48, 0.45, 0.43, 1]},
        # Chest crack (lava glow outer)
        {"offset": [0, 1.10, -0.27], "size": [0.24, 0.36, 0.02], "color": [1.00, 0.58, 0.08, 1]},
        # Chest crack (lava glow inner)
        {"offset": [0, 1.10, -0.28], "size": [0.12, 0.20, 0.02], "color": [1.00, 0.85, 0.30, 1]},
        # Left rivet
        {"offset": [-0.24, 0.92, -0.29], "size": [0.08, 0.08, 0.02], "color": [0.22, 0.20, 0.18, 1]},
        # Right rivet
        {"offset": [0.24, 0.92, -0.29], "size": [0.08, 0.08, 0.02], "color": [0.22, 0.20, 0.18, 1]},
        # Center rivet
        {"offset": [0, 0.92, -0.29], "size": [0.06, 0.06, 0.02], "color": [0.22, 0.20, 0.18, 1]},
        # Left shoulder bolt
        {"offset": [-0.48, 1.34, 0], "size": [0.10, 0.10, 0.10], "color": [0.22, 0.20, 0.18, 1]},
        # Right shoulder bolt
        {"offset": [0.48, 1.34, 0], "size": [0.10, 0.10, 0.10], "color": [0.22, 0.20, 0.18, 1]},
        # Left arm (massive)
        {"name": "left_upper_arm",
         "offset": [-0.64, 1.00, 0], "size": [0.40, 0.84, 0.40], "color": [0.45, 0.42, 0.40, 1],
         "pivot": [-0.48, 1.36, 0], "swing_axis": [1, 0, 0], "amplitude": 48, "phase": math.pi, "speed": 1},
        # Left fist
        {"name": "left_hand",
         "offset": [-0.64, 0.52, 0], "size": [0.48, 0.40, 0.48], "color": [0.32, 0.30, 0.28, 1],
         "pivot": [-0.48, 1.36, 0], "swing_axis": [1, 0, 0], "amplitude": 48, "phase": math.pi, "speed": 1},
        # Right arm (massive)
        {"name": "right_upper_arm",
         "offset": [0.64, 1.00, 0], "size": [0.40, 0.84, 0.40], "color": [0.45, 0.42, 0.40, 1],
         "pivot": [0.48, 1.36, 0], "swing_axis": [1, 0, 0], "amplitude": 48, "phase": 0, "speed": 1},
        # Right fist
        {"name": "right_hand",
         "offset": [0.64, 0.52, 0], "size": [0.48, 0.40, 0.48], "color": [0.32, 0.30, 0.28, 1],
         "pivot": [0.48, 1.36, 0], "swing_axis": [1, 0, 0], "amplitude": 48, "phase": 0, "speed": 1},
    ],

    # Shared humanoid clip vocabulary (giant has no separate forearm — fist is the hand).
    "clips": {
        "attack": {
            "right_upper_arm": {"axis": [1, 0, 0], "amp": 60, "bias": -30, "speed": 2.5, "phase": 0},
            "right_hand":      {"axis": [1, 0, 0], "amp": 60, "bias": -30, "speed": 2.5, "phase": 0},
        },
        "chop": {
            "right_upper_arm": {"axis": [1, 0, 0], "amp": 35, "bias": -70, "speed": 1.0, "phase": 0},
            "right_hand":      {"axis": [1, 0, 0], "amp": 35, "bias": -70, "speed": 1.0, "phase": 0},
        },
        "mine": {
            "right_upper_arm": {"axis": [1, 0, 0], "amp": 40, "bias": -60, "speed": 1.2, "phase": 0},
            "right_hand":      {"axis": [1, 0, 0], "amp": 40, "bias": -60, "speed": 1.2, "phase": 0},
        },
        "wave": {
            "right_upper_arm": {"axis": [0, 0, 1], "amp": 20, "bias": 130, "speed": 1.8, "phase": 0},
            "right_hand":      {"axis": [0, 0, 1], "amp": 20, "bias": 130, "speed": 1.8, "phase": 0},
        },
        "dance": {
            "right_upper_arm": {"axis": [0, 0, 1], "amp": 35, "bias":  80, "speed": 1.2, "phase": 0},
            "right_hand":      {"axis": [0, 0, 1], "amp": 35, "bias":  80, "speed": 1.2, "phase": 0},
            "left_upper_arm":  {"axis": [0, 0, 1], "amp": 35, "bias": -80, "speed": 1.2, "phase": math.pi},
            "left_hand":       {"axis": [0, 0, 1], "amp": 35, "bias": -80, "speed": 1.2, "phase": math.pi},
        },
        "sleep": {
            "right_upper_arm": {"axis": [1, 0, 0], "amp": 0, "bias": 0, "speed": 0.5, "phase": 0},
            "left_upper_arm":  {"axis": [1, 0, 0], "amp": 0, "bias": 0, "speed": 0.5, "phase": 0},
        },
    },
}

"""Skeleton -- undead warrior in rusted iron.

An undead warrior draped in rusted iron.
STR 3  STA 2  AGI 4  INT 3

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

import math

model = {
    "id": "skeleton",
    "height": 2.0,
    "scale": 1.0,
    "hand_r":  [ 0.20,  0.38, -0.12],
    "hand_l":  [-0.20,  0.38, -0.12],
    "pivot_r": [ 0.12,  0.65,  0.00],
    "pivot_l": [-0.12,  0.65,  0.00],
    "walk_speed": 1.8,
    "idle_bob": 0.008,
    "walk_bob": 0.03,
    "parts": [
        # Skull (texture provides face -- must be parts[0])
        {"offset": [0, 1.78, 0], "size": [0.46, 0.46, 0.46], "color": [0.88, 0.85, 0.78, 1],
         "pivot": [0, 1.55, 0], "swing_axis": [1, 0, 0], "amplitude": 6, "phase": 0, "speed": 2},
        # Jaw
        {"offset": [0, 1.62, -0.02], "size": [0.36, 0.10, 0.34], "color": [0.82, 0.78, 0.70, 1],
         "pivot": [0, 1.55, 0], "swing_axis": [1, 0, 0], "amplitude": 6, "phase": 0, "speed": 2},

        # Spine (Y-axis counter-twist)
        {"offset": [0, 1.10, 0], "size": [0.12, 0.60, 0.12], "color": [0.70, 0.65, 0.58, 1],
         "pivot": [0, 1.10, 0], "swing_axis": [0, 1, 0], "amplitude": 4, "phase": math.pi, "speed": 1},
        # Ribcage
        {"offset": [0, 1.18, 0], "size": [0.40, 0.36, 0.22], "color": [0.88, 0.85, 0.78, 1]},
        # Left rib detail
        {"offset": [-0.14, 1.08, -0.06], "size": [0.16, 0.04, 0.12], "color": [0.80, 0.76, 0.68, 1]},
        # Right rib detail
        {"offset": [0.14, 1.08, -0.06], "size": [0.16, 0.04, 0.12], "color": [0.80, 0.76, 0.68, 1]},
        # Pelvis
        {"offset": [0, 0.72, 0], "size": [0.36, 0.10, 0.22], "color": [0.70, 0.65, 0.58, 1]},
        # Rusted chestplate fragment
        {"offset": [0, 0.88, -0.08], "size": [0.32, 0.28, 0.04], "color": [0.30, 0.28, 0.25, 0.85]},
        # Belt
        {"offset": [0, 0.72, -0.10], "size": [0.40, 0.06, 0.04], "color": [0.52, 0.35, 0.22, 1]},

        # Left rusted pauldron
        {"name": "left_upper_arm",
         "offset": [-0.32, 1.40, 0], "size": [0.24, 0.12, 0.20], "color": [0.52, 0.35, 0.22, 1],
         "pivot": [-0.32, 1.38, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
        # Left bone arm
        {"name": "left_forearm",
         "offset": [-0.32, 1.10, 0], "size": [0.10, 0.52, 0.10], "color": [0.88, 0.85, 0.78, 1],
         "pivot": [-0.32, 1.38, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
        # Left hand
        {"name": "left_hand",
         "offset": [-0.32, 0.80, 0], "size": [0.12, 0.08, 0.08], "color": [0.70, 0.65, 0.58, 1],
         "pivot": [-0.32, 1.38, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
        # Right bare bone arm
        {"name": "right_forearm",
         "offset": [0.32, 1.10, 0], "size": [0.10, 0.52, 0.10], "color": [0.88, 0.85, 0.78, 1],
         "pivot": [0.32, 1.38, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        # Right hand
        {"name": "right_hand",
         "offset": [0.32, 0.80, 0], "size": [0.12, 0.08, 0.08], "color": [0.70, 0.65, 0.58, 1],
         "pivot": [0.32, 1.38, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},

        # Shield fragment
        {"offset": [0.08, 1.05, 0.16], "size": [0.28, 0.32, 0.04], "color": [0.42, 0.30, 0.18, 1]},
        # Shield boss
        {"offset": [0.08, 1.05, 0.13], "size": [0.08, 0.08, 0.04], "color": [0.52, 0.35, 0.22, 1]},

        # ═══ TATTERED CLOAK (hangs from shoulders, frayed hem) ═══
        # Main slab — dark rusted gray, mild sway
        {"offset": [0, 1.02, 0.22], "size": [0.50, 0.76, 0.03], "color": [0.22, 0.18, 0.16, 1],
         "pivot": [0, 1.38, 0.20], "swing_axis": [1, 0, 0], "amplitude": 5, "phase": 0, "speed": 1.0},
        # Tatter strip — left (long, darkest)
        {"offset": [-0.16, 0.48, 0.245], "size": [0.14, 0.36, 0.02], "color": [0.14, 0.10, 0.08, 1],
         "pivot": [0, 1.38, 0.20], "swing_axis": [1, 0, 0], "amplitude": 7, "phase": 0, "speed": 1.0},
        # Tatter strip — center (medium, torn mid-length)
        {"offset": [0.02, 0.52, 0.245], "size": [0.10, 0.26, 0.02], "color": [0.18, 0.14, 0.12, 1],
         "pivot": [0, 1.38, 0.20], "swing_axis": [1, 0, 0], "amplitude": 6, "phase": 0, "speed": 1.0},
        # Tatter strip — right (short, jagged)
        {"offset": [0.18, 0.58, 0.245], "size": [0.12, 0.18, 0.02], "color": [0.16, 0.12, 0.10, 1],
         "pivot": [0, 1.38, 0.20], "swing_axis": [1, 0, 0], "amplitude": 6, "phase": 0, "speed": 1.0},
        # Frayed fragment — drifting below
        {"offset": [-0.06, 0.26, 0.255], "size": [0.06, 0.10, 0.02], "color": [0.10, 0.08, 0.06, 1],
         "pivot": [0, 1.38, 0.20], "swing_axis": [1, 0, 0], "amplitude": 9, "phase": 0, "speed": 1.0},
        {"offset": [ 0.14, 0.30, 0.255], "size": [0.05, 0.12, 0.02], "color": [0.10, 0.08, 0.06, 1],
         "pivot": [0, 1.38, 0.20], "swing_axis": [1, 0, 0], "amplitude": 9, "phase": 0, "speed": 1.0},

        # Left leg
        {"offset": [-0.12, 0.38, 0], "size": [0.12, 0.44, 0.12], "color": [0.88, 0.85, 0.78, 1],
         "pivot": [-0.12, 0.66, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        # Left foot
        {"offset": [-0.12, 0.08, -0.04], "size": [0.14, 0.10, 0.22], "color": [0.70, 0.65, 0.58, 1],
         "pivot": [-0.12, 0.66, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        # Right leg
        {"offset": [0.12, 0.38, 0], "size": [0.12, 0.44, 0.12], "color": [0.88, 0.85, 0.78, 1],
         "pivot": [0.12, 0.66, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
        # Right foot
        {"offset": [0.12, 0.08, -0.04], "size": [0.14, 0.10, 0.22], "color": [0.70, 0.65, 0.58, 1],
         "pivot": [0.12, 0.66, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
    ],

    # Shared humanoid clip vocabulary.
    "clips": {
        "attack": {
            "right_upper_arm": {"axis": [1, 0, 0], "amp": 60, "bias":  30, "speed": 3.0, "phase": 0},
            "right_forearm":   {"axis": [1, 0, 0], "amp": 60, "bias":  30, "speed": 3.0, "phase": 0},
            "right_hand":      {"axis": [1, 0, 0], "amp": 60, "bias":  30, "speed": 3.0, "phase": 0},
        },
        "chop": {
            "right_upper_arm": {"axis": [1, 0, 0], "amp": 35, "bias":  70, "speed": 1.2, "phase": 0},
            "right_forearm":   {"axis": [1, 0, 0], "amp": 35, "bias":  70, "speed": 1.2, "phase": 0},
            "right_hand":      {"axis": [1, 0, 0], "amp": 35, "bias":  70, "speed": 1.2, "phase": 0},
        },
        "mine": {
            "right_upper_arm": {"axis": [1, 0, 0], "amp": 40, "bias":  60, "speed": 1.4, "phase": 0},
            "right_forearm":   {"axis": [1, 0, 0], "amp": 40, "bias":  60, "speed": 1.4, "phase": 0},
            "right_hand":      {"axis": [1, 0, 0], "amp": 40, "bias":  60, "speed": 1.4, "phase": 0},
        },
        "wave": {
            "right_upper_arm": {"axis": [0, 0, 1], "amp": 20, "bias": 130, "speed": 2.0, "phase": 0},
            "right_forearm":   {"axis": [0, 0, 1], "amp": 20, "bias": 130, "speed": 2.0, "phase": 0},
            "right_hand":      {"axis": [0, 0, 1], "amp": 20, "bias": 130, "speed": 2.0, "phase": 0},
        },
        "dance": {
            "right_upper_arm": {"axis": [0, 0, 1], "amp": 35, "bias":  80, "speed": 1.5, "phase": 0},
            "right_forearm":   {"axis": [0, 0, 1], "amp": 35, "bias":  80, "speed": 1.5, "phase": 0},
            "right_hand":      {"axis": [0, 0, 1], "amp": 35, "bias":  80, "speed": 1.5, "phase": 0},
            "left_upper_arm":  {"axis": [0, 0, 1], "amp": 35, "bias": -80, "speed": 1.5, "phase": math.pi},
            "left_forearm":    {"axis": [0, 0, 1], "amp": 35, "bias": -80, "speed": 1.5, "phase": math.pi},
            "left_hand":       {"axis": [0, 0, 1], "amp": 35, "bias": -80, "speed": 1.5, "phase": math.pi},
        },
        "sleep": {
            "right_upper_arm": {"axis": [1, 0, 0], "amp": 0, "bias": 0, "speed": 0.5, "phase": 0},
            "left_upper_arm":  {"axis": [1, 0, 0], "amp": 0, "bias": 0, "speed": 0.5, "phase": 0},
        },
    },
}

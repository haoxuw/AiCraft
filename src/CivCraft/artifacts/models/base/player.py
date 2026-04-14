"""Player — 2 blocks tall, Minecraft-style proportions.

Arms and legs swing opposite when walking. Right arm and left arm are
``name``-tagged so held items attach at the hand grip and named clips
(chop/mine/wave/dance/attack) can drive them.

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

import math

model = {
    "id": "player",
    "height": 2.0,
    "scale": 1.25,
    "walk_speed": 2.0,
    "idle_bob": 0.012,
    "walk_bob": 0.06,
    # Hand attachment: grip goes here; pivot = shoulder joint
    # Derived from arm part: offset [0.37, 1.05, 0], size [0.20, 0.70, 0.20]
    # Bottom of arm = 1.05 - 0.35 = 0.70 (hand position)
    "hand_r":  [ 0.47,  0.70, -0.12],
    "hand_l":  [-0.47,  0.70, -0.12],
    "pivot_r": [ 0.37,  1.40,  0.00],
    "pivot_l": [-0.37,  1.40,  0.00],
    "parts": [
        # Head -- nods once per step (2x walk freq)
        {"name": "head", "head": True,
         "offset": [0, 1.75, 0], "size": [0.50, 0.50, 0.50], "color": [0.85, 0.70, 0.55, 1],
         "pivot": [0, 1.5, 0], "swing_axis": [1, 0, 0], "amplitude": 5, "phase": 0, "speed": 2},
        # Torso -- Y-axis counter-twist
        {"name": "torso",
         "offset": [0, 1.05, 0], "size": [0.50, 0.70, 0.30], "color": [0.20, 0.45, 0.75, 1],
         "pivot": [0, 1.05, 0], "swing_axis": [0, 1, 0], "amplitude": 5, "phase": math.pi, "speed": 1},
        # Left arm (named so clips and held items can target it)
        # amp is negative so arms swing opposite to same-side leg (natural walk)
        {"name": "left_hand",
         "offset": [-0.37, 1.05, 0], "size": [0.20, 0.70, 0.20], "color": [0.80, 0.65, 0.50, 1],
         "pivot": [-0.37, 1.40, 0], "swing_axis": [1, 0, 0], "amplitude": -50, "phase": math.pi, "speed": 1},
        # Right arm
        {"name": "right_hand",
         "offset": [0.37, 1.05, 0], "size": [0.20, 0.70, 0.20], "color": [0.80, 0.65, 0.50, 1],
         "pivot": [0.37, 1.40, 0], "swing_axis": [1, 0, 0], "amplitude": -50, "phase": 0, "speed": 1},
        # Left leg -- big stride
        {"name": "left_leg",
         "offset": [-0.12, 0.35, 0], "size": [0.24, 0.70, 0.24], "color": [0.22, 0.22, 0.32, 1],
         "pivot": [-0.12, 0.70, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        # Right leg
        {"name": "right_leg",
         "offset": [0.12, 0.35, 0], "size": [0.24, 0.70, 0.24], "color": [0.22, 0.22, 0.32, 1],
         "pivot": [0.12, 0.70, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},

        # ═══ LEATHER BELT + GOLD BUCKLE ═══
        {"offset": [0, 0.75, 0], "size": [0.52, 0.08, 0.32], "color": [0.32, 0.20, 0.12, 1]},
        {"offset": [0, 0.75, -0.18], "size": [0.10, 0.06, 0.02], "color": [0.85, 0.68, 0.20, 1]},

        # ═══ RED CAPE (hero accent, gentle sway) ═══
        # Gold collar trim
        {"offset": [0, 1.34, 0.20], "size": [0.42, 0.04, 0.03], "color": [0.78, 0.62, 0.18, 1],
         "pivot": [0, 1.38, 0.17], "swing_axis": [1, 0, 0], "amplitude": 6, "phase": 0, "speed": 1.2},
        # Main cape slab
        {"offset": [0, 0.94, 0.19], "size": [0.44, 0.88, 0.04], "color": [0.55, 0.12, 0.14, 1],
         "pivot": [0, 1.38, 0.17], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 1.2},
        # Darker inner lining peek (tapered)
        {"offset": [0, 0.80, 0.21], "size": [0.36, 0.52, 0.02], "color": [0.38, 0.08, 0.10, 1],
         "pivot": [0, 1.38, 0.17], "swing_axis": [1, 0, 0], "amplitude": 9, "phase": 0, "speed": 1.2},
    ],

    # Standard humanoid clip vocabulary. Same shape as villager.py / knight.py.
    "clips": {
        "attack": {
            # Overhead chop: arm swings through a wide arc from raised-back
            # (-30°) down past horizontal to down-forward (+150°), so the
            # sword tip actually arcs DOWN toward the target on the downswing
            # rather than sticking out sideways as a thrust.
            "right_hand": {"axis": [1, 0, 0], "amp": 90, "bias":  60, "speed": 3.0, "phase": 0},
        },
        "chop": {
            "right_hand": {"axis": [1, 0, 0], "amp": 35, "bias":  70, "speed": 1.2, "phase": 0},
            "torso":      {"axis": [0, 1, 0], "amp": 8,  "speed": 1.2, "phase": 0},
        },
        "mine": {
            "right_hand": {"axis": [1, 0, 0], "amp": 40, "bias":  60, "speed": 1.4, "phase": 0},
            "torso":      {"axis": [0, 1, 0], "amp": 6,  "speed": 1.4, "phase": 0},
        },
        "wave": {
            "right_hand": {"axis": [0, 0, 1], "amp": 20, "bias": 130, "speed": 2.0, "phase": 0},
        },
        "dance": {
            "right_hand": {"axis": [0, 0, 1], "amp": 35, "bias":  80, "speed": 1.5, "phase": 0},
            "left_hand":  {"axis": [0, 0, 1], "amp": 35, "bias": -80, "speed": 1.5, "phase": math.pi},
            "torso":      {"axis": [0, 1, 0], "amp": 15, "speed": 1.5, "phase": 0},
            "head":       {"axis": [0, 1, 0], "amp": 12, "speed": 1.5, "phase": 1.5708},
        },
        "sleep": {
            "right_hand": {"axis": [1, 0, 0], "amp": 0, "bias": 0, "speed": 0.5, "phase": 0},
            "left_hand":  {"axis": [1, 0, 0], "amp": 0, "bias": 0, "speed": 0.5, "phase": 0},
            "torso":      {"axis": [1, 0, 0], "amp": 2, "bias": 0, "speed": 0.5, "phase": 0},
        },
    },
}

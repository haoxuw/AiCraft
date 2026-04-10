"""Blue Knight — steel-trimmed blue plate armor, cape, pauldrons.

Stalwart defender in steel-trimmed blue plate.
STR 4  STA 4  AGI 2  INT 2

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (deg), phase (radians), speed

Rigging model (see docs/22_BEHAVIORS.md and client/box_model.h):
  - ``"head": True`` flags the head so it rotates around ``head_pivot``
    for Minecraft-style head tracking. Non-head parts are unaffected.
  - ``"name"`` fields make parts targetable by named animation clips.
  - Torso has a tiny Y-axis counter-twist during walk (secondary motion).
"""

import math

model = {
    "id": "knight",
    "height": 2.0,
    "scale": 1.0,
    "walk_speed": 2.0,
    "idle_bob": 0.012,
    "walk_bob": 0.05,
    "hand_r":  [ 0.50,  0.72, -0.14],
    "hand_l":  [-0.50,  0.72, -0.14],
    "pivot_r": [ 0.37,  1.40,  0.00],
    "pivot_l": [-0.37,  1.40,  0.00],

    # Head tracking pivot (base of helm / neck joint)
    "head_pivot": [0, 1.50, 0],

    "parts": [
        # Head (texture provides face + hair -- must be parts[0])
        {"name": "head", "head": True,
         "offset": [0, 1.75, 0], "size": [0.50, 0.50, 0.50], "color": [0.85, 0.70, 0.55, 1],
         "pivot": [0, 1.5, 0], "swing_axis": [1, 0, 0], "amplitude": 4, "phase": 0, "speed": 2},
        # Torso (Y-axis counter-twist)
        {"name": "torso",
         "offset": [0, 1.08, 0], "size": [0.50, 0.64, 0.30], "color": [0.18, 0.32, 0.72, 1],
         "pivot": [0, 1.08, 0], "swing_axis": [0, 1, 0], "amplitude": 4, "phase": math.pi, "speed": 1},
        # Chestplate
        {"offset": [0, 1.12, -0.14], "size": [0.44, 0.52, 0.04], "color": [0.55, 0.58, 0.62, 1]},
        # Chestplate emblem
        {"offset": [0, 1.18, -0.17], "size": [0.10, 0.10, 0.02], "color": [0.80, 0.70, 0.20, 1]},
        # Gorget
        {"offset": [0, 1.39, 0], "size": [0.46, 0.08, 0.30], "color": [0.55, 0.58, 0.62, 1]},
        # Belt
        {"offset": [0, 0.76, 0], "size": [0.52, 0.08, 0.32], "color": [0.35, 0.25, 0.15, 1]},
        # Belt buckle
        {"offset": [0, 0.76, -0.15], "size": [0.08, 0.06, 0.04], "color": [0.80, 0.70, 0.20, 1]},

        # Left pauldron
        {"name": "left_upper_arm",
         "offset": [-0.37, 1.38, 0], "size": [0.28, 0.12, 0.28], "color": [0.55, 0.58, 0.62, 1],
         "pivot": [-0.37, 1.40, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
        # Left arm
        {"name": "left_upper_arm",
         "offset": [-0.37, 1.08, 0], "size": [0.20, 0.56, 0.20], "color": [0.12, 0.22, 0.55, 1],
         "pivot": [-0.37, 1.40, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
        # Left gauntlet
        {"name": "left_forearm",
         "offset": [-0.37, 0.86, 0], "size": [0.24, 0.18, 0.24], "color": [0.38, 0.40, 0.45, 1],
         "pivot": [-0.37, 1.40, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
        # Left hand
        {"name": "left_hand",
         "offset": [-0.37, 0.77, 0], "size": [0.16, 0.10, 0.16], "color": [0.85, 0.70, 0.55, 1],
         "pivot": [-0.37, 1.40, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
        # Right pauldron
        {"name": "right_upper_arm",
         "offset": [0.37, 1.38, 0], "size": [0.28, 0.12, 0.28], "color": [0.55, 0.58, 0.62, 1],
         "pivot": [0.37, 1.40, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        # Right arm
        {"name": "right_upper_arm",
         "offset": [0.37, 1.08, 0], "size": [0.20, 0.56, 0.20], "color": [0.12, 0.22, 0.55, 1],
         "pivot": [0.37, 1.40, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        # Right gauntlet
        {"name": "right_forearm",
         "offset": [0.37, 0.86, 0], "size": [0.24, 0.18, 0.24], "color": [0.38, 0.40, 0.45, 1],
         "pivot": [0.37, 1.40, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        # Right hand
        {"name": "right_hand",
         "offset": [0.37, 0.77, 0], "size": [0.16, 0.10, 0.16], "color": [0.85, 0.70, 0.55, 1],
         "pivot": [0.37, 1.40, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},

        # Cape (gentle sway)
        {"offset": [0, 0.95, 0.20], "size": [0.44, 0.76, 0.06], "color": [0.14, 0.25, 0.60, 1],
         "pivot": [0, 1.35, 0.18], "swing_axis": [1, 0, 0], "amplitude": 10, "phase": 0, "speed": 1.2},

        # Left leg
        {"name": "left_leg",
         "offset": [-0.13, 0.40, 0], "size": [0.22, 0.48, 0.22], "color": [0.18, 0.18, 0.28, 1],
         "pivot": [-0.13, 0.70, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        # Left shin guard
        {"name": "left_leg",
         "offset": [-0.13, 0.38, -0.12], "size": [0.16, 0.28, 0.04], "color": [0.38, 0.40, 0.45, 1],
         "pivot": [-0.13, 0.70, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        # Left boot
        {"name": "left_leg",
         "offset": [-0.13, 0.10, 0], "size": [0.24, 0.20, 0.26], "color": [0.30, 0.22, 0.14, 1],
         "pivot": [-0.13, 0.70, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        # Right leg
        {"name": "right_leg",
         "offset": [0.13, 0.40, 0], "size": [0.22, 0.48, 0.22], "color": [0.18, 0.18, 0.28, 1],
         "pivot": [0.13, 0.70, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
        # Right shin guard
        {"name": "right_leg",
         "offset": [0.13, 0.38, -0.12], "size": [0.16, 0.28, 0.04], "color": [0.38, 0.40, 0.45, 1],
         "pivot": [0.13, 0.70, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
        # Right boot
        {"name": "right_leg",
         "offset": [0.13, 0.10, 0], "size": [0.24, 0.20, 0.26], "color": [0.30, 0.22, 0.14, 1],
         "pivot": [0.13, 0.70, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
    ],

    # ═══════════════ NAMED ANIMATION CLIPS ═══════════════
    # Shared humanoid clip vocabulary. See villager.py for the rationale.
    "clips": {
        "attack": {
            "right_upper_arm": {"axis": [1, 0, 0], "amp": 60, "bias": -30, "speed": 3.0, "phase": 0},
            "right_forearm":   {"axis": [1, 0, 0], "amp": 60, "bias": -30, "speed": 3.0, "phase": 0},
            "right_hand":      {"axis": [1, 0, 0], "amp": 60, "bias": -30, "speed": 3.0, "phase": 0},
            "torso":           {"axis": [0, 1, 0], "amp": 10, "speed": 3.0, "phase": 0},
        },
        "chop": {
            "right_upper_arm": {"axis": [1, 0, 0], "amp": 35, "bias": -70, "speed": 1.2, "phase": 0},
            "right_forearm":   {"axis": [1, 0, 0], "amp": 35, "bias": -70, "speed": 1.2, "phase": 0},
            "right_hand":      {"axis": [1, 0, 0], "amp": 35, "bias": -70, "speed": 1.2, "phase": 0},
            "torso":           {"axis": [0, 1, 0], "amp": 8,  "speed": 1.2, "phase": 0},
        },
        "mine": {
            "right_upper_arm": {"axis": [1, 0, 0], "amp": 40, "bias": -60, "speed": 1.4, "phase": 0},
            "right_forearm":   {"axis": [1, 0, 0], "amp": 40, "bias": -60, "speed": 1.4, "phase": 0},
            "right_hand":      {"axis": [1, 0, 0], "amp": 40, "bias": -60, "speed": 1.4, "phase": 0},
            "torso":           {"axis": [0, 1, 0], "amp": 6,  "speed": 1.4, "phase": 0},
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
            "torso":           {"axis": [0, 1, 0], "amp": 15, "speed": 1.5, "phase": 0},
            "head":            {"axis": [0, 1, 0], "amp": 12, "speed": 1.5, "phase": 1.5708},
        },
        "sleep": {
            "right_upper_arm": {"axis": [1, 0, 0], "amp": 0, "bias": 0, "speed": 0.5, "phase": 0},
            "left_upper_arm":  {"axis": [1, 0, 0], "amp": 0, "bias": 0, "speed": 0.5, "phase": 0},
            "torso":           {"axis": [1, 0, 0], "amp": 2, "bias": 0, "speed": 0.5, "phase": 0},
        },
    },
}

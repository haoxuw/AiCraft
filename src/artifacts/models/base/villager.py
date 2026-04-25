"""Villager — steel-trimmed plate armor, cape, pauldrons.

Stout settler in steel-trimmed plate. Formerly the knight look; villagers
now inherit the armored silhouette because knight is a playable creature.
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
    "id": "villager",
    "head_pivot": [0.0, 1.5, 0.0],
    "hand_r": [0.5, 0.72, -0.14],
    "hand_l": [-0.5, 0.72, -0.14],
    "pivot_r": [0.37, 1.4, 0.0],
    "pivot_l": [-0.37, 1.4, 0.0],
    "walk_bob": 0.05,
    "idle_bob": 0.012,
    "walk_speed": 2.0,
    "parts": [
        {"name": "head", "offset": [0.0, 1.75, 0.0], "size": [0.5, 0.5, 0.5], "color": [0.85, 0.7, 0.55, 1.0], "pivot": [0.0, 1.5, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 4, "phase": 0, "speed": 2, "head": True},
        {"offset": [0.0, 1.76, -0.255], "size": [0.32, 0.06, 0.01], "color": [0.1, 0.1, 0.12, 1.0], "pivot": [0.0, 1.5, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 4, "phase": 0, "speed": 2, "head": True},
        {"offset": [-0.08, 1.76, -0.262], "size": [0.04, 0.03, 0.01], "color": [0.55, 0.78, 0.95, 1.0], "pivot": [0.0, 1.5, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 4, "phase": 0, "speed": 2, "head": True},
        {"offset": [0.08, 1.76, -0.262], "size": [0.04, 0.03, 0.01], "color": [0.55, 0.78, 0.95, 1.0], "pivot": [0.0, 1.5, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 4, "phase": 0, "speed": 2, "head": True},
        {"name": "torso", "offset": [0.0, 1.08, 0.0], "size": [0.5, 0.64, 0.3], "color": [0.18, 0.32, 0.72, 1.0], "pivot": [0.0, 1.08, 0.0], "swing_axis": [0.0, 1.0, 0.0], "amplitude": 4, "phase": 3.1416, "speed": 1},
        {"offset": [0.0, 1.12, -0.14], "size": [0.44, 0.52, 0.04], "color": [0.55, 0.58, 0.62, 1.0]},
        {"offset": [0.0, 1.18, -0.17], "size": [0.1, 0.1, 0.02], "color": [0.8, 0.7, 0.2, 1.0]},
        {"offset": [0.0, 1.39, 0.0], "size": [0.46, 0.08, 0.28], "color": [0.55, 0.58, 0.62, 1.0]},
        {"offset": [0.0, 0.76, 0.0], "size": [0.52, 0.08, 0.32], "color": [0.35, 0.25, 0.15, 1.0]},
        {"offset": [0.0, 0.76, -0.15], "size": [0.08, 0.06, 0.04], "color": [0.8, 0.7, 0.2, 1.0]},
        {"name": "left_upper_arm", "offset": [-0.37, 1.38, 0.0], "size": [0.28, 0.12, 0.28], "color": [0.55, 0.58, 0.62, 1.0], "pivot": [-0.37, 1.4, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 3.1416, "speed": 1},
        {"name": "left_upper_arm", "offset": [-0.37, 1.08, 0.0], "size": [0.2, 0.56, 0.2], "color": [0.12, 0.22, 0.55, 1.0], "pivot": [-0.37, 1.4, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 3.1416, "speed": 1},
        {"name": "left_forearm", "offset": [-0.37, 0.86, 0.0], "size": [0.24, 0.18, 0.24], "color": [0.38, 0.4, 0.45, 1.0], "pivot": [-0.37, 1.4, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 3.1416, "speed": 1},
        {"name": "left_hand", "offset": [-0.37, 0.77, 0.0], "size": [0.16, 0.1, 0.16], "color": [0.85, 0.7, 0.55, 1.0], "pivot": [-0.37, 1.4, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 3.1416, "speed": 1},
        {"name": "right_upper_arm", "offset": [0.37, 1.38, 0.0], "size": [0.28, 0.12, 0.28], "color": [0.55, 0.58, 0.62, 1.0], "pivot": [0.37, 1.4, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 0, "speed": 1},
        {"name": "right_upper_arm", "offset": [0.37, 1.08, 0.0], "size": [0.2, 0.56, 0.2], "color": [0.12, 0.22, 0.55, 1.0], "pivot": [0.37, 1.4, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 0, "speed": 1},
        {"name": "right_forearm", "offset": [0.37, 0.86, 0.0], "size": [0.24, 0.18, 0.24], "color": [0.38, 0.4, 0.45, 1.0], "pivot": [0.37, 1.4, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 0, "speed": 1},
        {"name": "right_hand", "offset": [0.37, 0.77, 0.0], "size": [0.16, 0.1, 0.16], "color": [0.85, 0.7, 0.55, 1.0], "pivot": [0.37, 1.4, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 0, "speed": 1},
        {"offset": [0.0, 0.95, 0.2], "size": [0.44, 0.76, 0.06], "color": [0.14, 0.25, 0.6, 1.0], "pivot": [0.0, 1.35, 0.18], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 10, "phase": 0, "speed": 1.2},
        {"name": "left_leg", "offset": [-0.13, 0.4, 0.0], "size": [0.22, 0.48, 0.22], "color": [0.18, 0.18, 0.28, 1.0], "pivot": [-0.13, 0.7, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 0, "speed": 1},
        {"name": "left_leg", "offset": [-0.13, 0.38, -0.12], "size": [0.16, 0.28, 0.04], "color": [0.38, 0.4, 0.45, 1.0], "pivot": [-0.13, 0.7, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 0, "speed": 1},
        {"name": "left_leg", "offset": [-0.13, 0.1, 0.0], "size": [0.24, 0.2, 0.26], "color": [0.3, 0.22, 0.14, 1.0], "pivot": [-0.13, 0.7, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 0, "speed": 1},
        {"name": "right_leg", "offset": [0.13, 0.4, 0.0], "size": [0.22, 0.48, 0.22], "color": [0.18, 0.18, 0.28, 1.0], "pivot": [0.13, 0.7, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 3.1416, "speed": 1},
        {"name": "right_leg", "offset": [0.13, 0.38, -0.12], "size": [0.16, 0.28, 0.04], "color": [0.38, 0.4, 0.45, 1.0], "pivot": [0.13, 0.7, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 3.1416, "speed": 1},
        {"name": "right_leg", "offset": [0.13, 0.1, 0.0], "size": [0.24, 0.2, 0.26], "color": [0.3, 0.22, 0.14, 1.0], "pivot": [0.13, 0.7, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 3.1416, "speed": 1},
    ],
    "clips": {
        "attack": {
            "right_upper_arm": {"axis": [1.0, 0.0, 0.0], "amp": 60, "phase": 0, "bias": 30, "speed": 3.0},
            "right_forearm": {"axis": [1.0, 0.0, 0.0], "amp": 60, "phase": 0, "bias": 30, "speed": 3.0},
            "right_hand": {"axis": [1.0, 0.0, 0.0], "amp": 60, "phase": 0, "bias": 30, "speed": 3.0},
            "torso": {"axis": [0.0, 1.0, 0.0], "amp": 10, "phase": 0, "speed": 3.0},
        },
        "chop": {
            "right_upper_arm": {"axis": [1.0, 0.0, 0.0], "amp": 35, "phase": 0, "bias": 70, "speed": 1.2},
            "right_forearm": {"axis": [1.0, 0.0, 0.0], "amp": 35, "phase": 0, "bias": 70, "speed": 1.2},
            "right_hand": {"axis": [1.0, 0.0, 0.0], "amp": 35, "phase": 0, "bias": 70, "speed": 1.2},
            "torso": {"axis": [0.0, 1.0, 0.0], "amp": 8, "phase": 0, "speed": 1.2},
        },
        "mine": {
            "right_upper_arm": {"axis": [1.0, 0.0, 0.0], "amp": 40, "phase": 0, "bias": 60, "speed": 1.4},
            "right_forearm": {"axis": [1.0, 0.0, 0.0], "amp": 40, "phase": 0, "bias": 60, "speed": 1.4},
            "right_hand": {"axis": [1.0, 0.0, 0.0], "amp": 40, "phase": 0, "bias": 60, "speed": 1.4},
            "torso": {"axis": [0.0, 1.0, 0.0], "amp": 6, "phase": 0, "speed": 1.4},
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
            "torso": {"axis": [0.0, 1.0, 0.0], "amp": 15, "phase": 0, "speed": 1.5},
            "head": {"axis": [0.0, 1.0, 0.0], "amp": 12, "phase": 1.5708, "speed": 1.5},
        },
        "sleep": {
            "right_upper_arm": {"axis": [1.0, 0.0, 0.0], "amp": 0, "phase": 0, "bias": 0, "speed": 0.5},
            "left_upper_arm": {"axis": [1.0, 0.0, 0.0], "amp": 0, "phase": 0, "bias": 0, "speed": 0.5},
            "torso": {"axis": [1.0, 0.0, 0.0], "amp": 2, "phase": 0, "bias": 0, "speed": 0.5},
        },
    },
}

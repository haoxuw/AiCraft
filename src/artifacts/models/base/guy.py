"""Guy — 2 blocks tall, hero silhouette with red cape and leather belt.

A playable humanoid. Arms and legs swing opposite when walking. Right and
left arm are ``name``-tagged so held items attach at the hand grip and
named clips (chop/mine/wave/dance/attack) can drive them.

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

import math

model = {
    "id": "guy",
    "hand_r": [0.5875, 0.875, -0.15],
    "hand_l": [-0.5875, 0.875, -0.15],
    "pivot_r": [0.4625, 1.75, 0.0],
    "pivot_l": [-0.4625, 1.75, 0.0],
    "walk_bob": 0.075,
    "idle_bob": 0.015,
    "walk_speed": 2.0,
    "parts": [
        {"name": "head", "offset": [0.0, 2.1875, 0.0], "size": [0.625, 0.625, 0.625], "color": [0.85, 0.7, 0.55, 1.0], "pivot": [0.0, 1.875, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 5, "phase": 0, "speed": 2, "head": True},
        {"name": "torso", "offset": [0.0, 1.3125, 0.0], "size": [0.625, 0.875, 0.375], "color": [0.2, 0.45, 0.75, 1.0], "pivot": [0.0, 1.3125, 0.0], "swing_axis": [0.0, 1.0, 0.0], "amplitude": 5, "phase": 3.1416, "speed": 1},
        {"name": "left_hand", "offset": [-0.4625, 1.3125, 0.0], "size": [0.25, 0.875, 0.25], "color": [0.8, 0.65, 0.5, 1.0], "pivot": [-0.4625, 1.75, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 3.1416, "speed": 1},
        {"name": "right_hand", "offset": [0.4625, 1.3125, 0.0], "size": [0.25, 0.875, 0.25], "color": [0.8, 0.65, 0.5, 1.0], "pivot": [0.4625, 1.75, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 0, "speed": 1},
        {"name": "left_leg", "offset": [-0.15, 0.4375, 0.0], "size": [0.3, 0.875, 0.3], "color": [0.22, 0.22, 0.32, 1.0], "pivot": [-0.15, 0.875, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 0, "speed": 1},
        {"name": "right_leg", "offset": [0.15, 0.4375, 0.0], "size": [0.3, 0.875, 0.3], "color": [0.22, 0.22, 0.32, 1.0], "pivot": [0.15, 0.875, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 3.1416, "speed": 1},
        {"offset": [0.0, 0.9375, 0.0], "size": [0.65, 0.1, 0.4], "color": [0.32, 0.2, 0.12, 1.0]},
        {"offset": [0.0, 0.9375, -0.225], "size": [0.125, 0.075, 0.025], "color": [0.85, 0.68, 0.2, 1.0]},
        {"offset": [0.0, 1.0, 0.2125], "size": [0.45, 0.65, 0.0187], "color": [0.38, 0.08, 0.1, 1.0], "pivot": [0.0, 1.725, 0.2125], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 1.2},
        {"offset": [0.0, 1.175, 0.25], "size": [0.55, 1.1, 0.025], "color": [0.55, 0.12, 0.14, 1.0], "pivot": [0.0, 1.725, 0.2125], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 1.2},
        {"offset": [0.0, 1.675, 0.2875], "size": [0.525, 0.05, 0.025], "color": [0.78, 0.62, 0.18, 1.0], "pivot": [0.0, 1.725, 0.2125], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 1.2},
    ],
    "clips": {
        "attack": {
            "right_hand": {"axis": [1.0, 0.0, 0.0], "amp": 90, "phase": 0, "bias": 60, "speed": 3.0},
        },
        "chop": {
            "right_hand": {"axis": [1.0, 0.0, 0.0], "amp": 35, "phase": 0, "bias": 70, "speed": 1.2},
            "torso": {"axis": [0.0, 1.0, 0.0], "amp": 8, "phase": 0, "speed": 1.2},
        },
        "mine": {
            "right_hand": {"axis": [1.0, 0.0, 0.0], "amp": 40, "phase": 0, "bias": 60, "speed": 1.4},
            "torso": {"axis": [0.0, 1.0, 0.0], "amp": 6, "phase": 0, "speed": 1.4},
        },
        "wave": {
            "right_hand": {"axis": [0.0, 0.0, 1.0], "amp": 20, "phase": 0, "bias": 130, "speed": 2.0},
        },
        "dance": {
            "right_hand": {"axis": [0.0, 0.0, 1.0], "amp": 35, "phase": 0, "bias": 80, "speed": 1.5},
            "left_hand": {"axis": [0.0, 0.0, 1.0], "amp": 35, "phase": 3.1416, "bias": -80, "speed": 1.5},
            "torso": {"axis": [0.0, 1.0, 0.0], "amp": 15, "phase": 0, "speed": 1.5},
            "head": {"axis": [0.0, 1.0, 0.0], "amp": 12, "phase": 1.5708, "speed": 1.5},
        },
        "sleep": {
            "right_hand": {"axis": [1.0, 0.0, 0.0], "amp": 0, "phase": 0, "bias": 0, "speed": 0.5},
            "left_hand": {"axis": [1.0, 0.0, 0.0], "amp": 0, "phase": 0, "bias": 0, "speed": 0.5},
            "torso": {"axis": [1.0, 0.0, 0.0], "amp": 2, "phase": 0, "bias": 0, "speed": 0.5},
        },
        "sit": {
            "left_leg": {"axis": [1.0, 0.0, 0.0], "amp": 0, "phase": 0, "bias": 55, "speed": 0.6},
            "right_leg": {"axis": [1.0, 0.0, 0.0], "amp": 0, "phase": 0, "bias": 55, "speed": 0.6},
            "left_hand": {"axis": [1.0, 0.0, 0.0], "amp": 1, "phase": 0, "bias": 35, "speed": 0.6},
            "right_hand": {"axis": [1.0, 0.0, 0.0], "amp": 1, "phase": 0, "bias": 35, "speed": 0.6},
            "torso": {"axis": [1.0, 0.0, 0.0], "amp": 1, "phase": 0, "bias": -25, "speed": 0.6},
            "head": {"axis": [1.0, 0.0, 0.0], "amp": 1, "phase": 0, "bias": -15, "speed": 0.6},
        },
        "hurt": {
            "torso": {"axis": [1.0, 0.0, 0.0], "amp": 15, "phase": -1.5708, "bias": 0, "speed": 8.0},
            "head": {"axis": [1.0, 0.0, 0.0], "amp": 18, "phase": -1.5708, "bias": 0, "speed": 8.0},
            "right_hand": {"axis": [1.0, 0.0, 0.0], "amp": 10, "phase": -1.5708, "bias": 15, "speed": 8.0},
            "left_hand": {"axis": [1.0, 0.0, 0.0], "amp": 10, "phase": -1.5708, "bias": 15, "speed": 8.0},
        },
    },
}

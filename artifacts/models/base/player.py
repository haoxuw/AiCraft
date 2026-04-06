"""Player — 2 blocks tall, Minecraft-style proportions.

Arms and legs swing opposite when walking.
Edit parts to customize the player's appearance!

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
        {"offset": [0, 1.75, 0], "size": [0.50, 0.50, 0.50], "color": [0.85, 0.70, 0.55, 1],
         "pivot": [0, 1.5, 0], "swing_axis": [1, 0, 0], "amplitude": 5, "phase": 0, "speed": 2},
        # Torso -- Y-axis counter-twist
        {"offset": [0, 1.05, 0], "size": [0.50, 0.70, 0.30], "color": [0.20, 0.45, 0.75, 1],
         "pivot": [0, 1.05, 0], "swing_axis": [0, 1, 0], "amplitude": 5, "phase": math.pi, "speed": 1},
        # Left arm -- large confident swing
        {"offset": [-0.37, 1.05, 0], "size": [0.20, 0.70, 0.20], "color": [0.80, 0.65, 0.50, 1],
         "pivot": [-0.37, 1.40, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
        # Right arm
        {"offset": [0.37, 1.05, 0], "size": [0.20, 0.70, 0.20], "color": [0.80, 0.65, 0.50, 1],
         "pivot": [0.37, 1.40, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        # Left leg -- big stride
        {"offset": [-0.12, 0.35, 0], "size": [0.24, 0.70, 0.24], "color": [0.22, 0.22, 0.32, 1],
         "pivot": [-0.12, 0.70, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        # Right leg
        {"offset": [0.12, 0.35, 0], "size": [0.24, 0.70, 0.24], "color": [0.22, 0.22, 0.32, 1],
         "pivot": [0.12, 0.70, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
    ]
}

"""Shield — wooden shield with iron boss and rim.

Held on the arm facing outward, blocking incoming attacks.
"""

model = {
    "id": "shield",
    "height": 0.8,
    # Shield faces outward from the arm, slightly angled
    "equip": {
        "rotation": [0, 90, 0],    # face outward (Y rotation)
        "offset": [0.08, 0, -0.05],  # slightly outward from arm
        "scale": 0.7,
    },
    "parts": [
        # Face (wooden planks)
        {"offset": [0, 0.32, 0], "size": [0.06, 0.52, 0.40], "color": [0.52, 0.35, 0.18, 1]},
        # Plank detail
        {"offset": [0, 0.32, -0.08], "size": [0.07, 0.48, 0.08], "color": [0.58, 0.42, 0.22, 1]},
        {"offset": [0, 0.32, 0.08], "size": [0.07, 0.48, 0.08], "color": [0.58, 0.42, 0.22, 1]},
        # Iron boss
        {"offset": [0.04, 0.32, 0], "size": [0.06, 0.14, 0.14], "color": [0.58, 0.56, 0.52, 1]},
        # Iron rim
        {"offset": [0, 0.56, 0], "size": [0.07, 0.04, 0.42], "color": [0.50, 0.48, 0.44, 1]},
        {"offset": [0, 0.08, 0], "size": [0.07, 0.04, 0.42], "color": [0.50, 0.48, 0.44, 1]},
        # Rivets
        {"offset": [0.04, 0.50, -0.14], "size": [0.03, 0.03, 0.03], "color": [0.42, 0.40, 0.38, 1]},
        {"offset": [0.04, 0.50, 0.14], "size": [0.03, 0.03, 0.03], "color": [0.42, 0.40, 0.38, 1]},
        {"offset": [0.04, 0.14, -0.14], "size": [0.03, 0.03, 0.03], "color": [0.42, 0.40, 0.38, 1]},
        {"offset": [0.04, 0.14, 0.14], "size": [0.03, 0.03, 0.03], "color": [0.42, 0.40, 0.38, 1]},
    ]
}

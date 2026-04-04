"""Egg — smooth oval egg laid by chickens.

Approximated as stacked layers getting narrower toward the top,
creating a convincing oval/egg shape from box primitives.
"""

model = {
    "id": "egg",
    "height": 0.35,
    "equip": {
        "rotation": [0, 0, 0],
        "offset": [0, -0.08, -0.02],
        "scale": 0.6,
    },
    "parts": [
        # Bottom (narrow base, like a real egg sits)
        {"offset": [0, 0.02, 0], "size": [0.08, 0.04, 0.08], "color": [0.93, 0.90, 0.84, 1]},
        # Lower body (widening)
        {"offset": [0, 0.06, 0], "size": [0.12, 0.06, 0.12], "color": [0.95, 0.92, 0.86, 1]},
        # Mid-lower (widest part of egg — the belly)
        {"offset": [0, 0.11, 0], "size": [0.14, 0.06, 0.14], "color": [0.96, 0.94, 0.88, 1]},
        # Mid body
        {"offset": [0, 0.16, 0], "size": [0.13, 0.06, 0.13], "color": [0.96, 0.93, 0.87, 1]},
        # Upper body (narrowing)
        {"offset": [0, 0.21, 0], "size": [0.11, 0.05, 0.11], "color": [0.95, 0.92, 0.86, 1]},
        # Near top (getting pointy)
        {"offset": [0, 0.25, 0], "size": [0.08, 0.04, 0.08], "color": [0.94, 0.91, 0.85, 1]},
        # Tip (small rounded top)
        {"offset": [0, 0.28, 0], "size": [0.05, 0.03, 0.05], "color": [0.93, 0.90, 0.84, 1]},
        # Subtle highlight (light spot on the side)
        {"offset": [-0.03, 0.16, -0.04], "size": [0.03, 0.06, 0.02], "color": [0.98, 0.97, 0.94, 0.7]},
    ]
}

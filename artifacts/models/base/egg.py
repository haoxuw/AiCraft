"""Egg — smooth oval egg laid by chickens.

Simple 4-part construction: wide belly, narrower top/bottom.
Fewer parts = cleaner silhouette without visible ridges.
"""

model = {
    "id": "egg",
    "height": 0.30,
    "equip": {
        "rotation": [0, 0, 0],
        "offset": [0, -0.06, -0.02],
        "scale": 0.6,
    },
    "parts": [
        # Main body (widest part — the belly of the egg)
        {"offset": [0, 0.12, 0], "size": [0.14, 0.16, 0.14], "color": [0.96, 0.94, 0.88, 1]},
        # Top half (slightly narrower)
        {"offset": [0, 0.22, 0], "size": [0.10, 0.10, 0.10], "color": [0.95, 0.93, 0.87, 1]},
        # Bottom (narrow base)
        {"offset": [0, 0.04, 0], "size": [0.10, 0.08, 0.10], "color": [0.94, 0.91, 0.85, 1]},
        # Highlight (subtle sheen)
        {"offset": [-0.03, 0.15, -0.05], "size": [0.03, 0.06, 0.02], "color": [0.98, 0.97, 0.94, 0.6]},
    ]
}

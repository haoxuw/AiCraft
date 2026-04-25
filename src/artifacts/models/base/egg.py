"""Egg — smooth oval egg laid by chickens.

Simple 4-part construction: wide belly, narrower top/bottom.
Fewer parts = cleaner silhouette without visible ridges.
"""

model = {
    "id": "egg",
    "equip": {"rotation": [0.0, 0.0, 0.0], "offset": [0.0, -0.06, -0.02]},
    "parts": [
        {"offset": [0.0, 0.072, 0.0], "size": [0.084, 0.096, 0.084], "color": [0.96, 0.94, 0.88, 1.0]},
        {"offset": [0.0, 0.132, 0.0], "size": [0.06, 0.06, 0.06], "color": [0.95, 0.93, 0.87, 1.0]},
        {"offset": [0.0, 0.024, 0.0], "size": [0.06, 0.048, 0.06], "color": [0.94, 0.91, 0.85, 1.0]},
        {"offset": [-0.018, 0.09, -0.03], "size": [0.018, 0.036, 0.012], "color": [0.98, 0.97, 0.94, 0.6]},
    ],
}

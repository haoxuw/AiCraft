"""Wheat Crop — growing wheat at mid-stage.

Partially grown wheat. Needs more time to mature.
"""

model = {
    "id": "wheat_crop",
    "height": 0.4,
    "parts": [
        # Green-yellow stalks — mid-growth
        {"offset": [0, 0.10, 0], "size": [0.12, 0.20, 0.12], "color": [0.55, 0.62, 0.22, 1]},
        # Small forming heads
        {"offset": [0, 0.22, 0], "size": [0.14, 0.06, 0.14], "color": [0.65, 0.60, 0.25, 1]},
    ]
}

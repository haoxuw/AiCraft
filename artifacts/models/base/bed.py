"""Bed — red mattress on a wooden frame.

Sleep through the night. Sets your spawn point.
"""

model = {
    "id": "bed",
    "height": 0.5,
    "parts": [
        # Wood frame — flat base
        {"offset": [0, 0.06, 0], "size": [0.70, 0.08, 0.50], "color": [0.50, 0.35, 0.15, 1]},
        # Red mattress — soft top
        {"offset": [0, 0.14, 0], "size": [0.66, 0.10, 0.46], "color": [0.75, 0.18, 0.15, 1]},
        # Pillow — white head
        {"offset": [-0.24, 0.18, 0], "size": [0.14, 0.06, 0.30], "color": [0.90, 0.88, 0.85, 1]},
    ]
}

"""Leaves — dark green semi-transparent foliage block.

Found on treetops. Decays when not attached to wood.
"""

model = {
    "id": "leaves",
    "height": 1.0,
    "parts": [
        # Leaf mass — slightly transparent
        {"offset": [0, 0.5, 0], "size": [0.80, 0.80, 0.80], "color": [0.25, 0.55, 0.18, 0.9]},
    ]
}

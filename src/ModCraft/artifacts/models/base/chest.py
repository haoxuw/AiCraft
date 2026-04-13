"""Chest — brown storage box with darker lid and gold clasp.

Stores items. Right-click to open.
"""

model = {
    "id": "chest",
    "height": 0.8,
    "parts": [
        # Body — main box
        {"offset": [0, 0.20, 0], "size": [0.60, 0.36, 0.50], "color": [0.52, 0.38, 0.18, 1]},
        # Lid — darker top
        {"offset": [0, 0.42, 0], "size": [0.62, 0.10, 0.52], "color": [0.45, 0.32, 0.14, 1]},
        # Clasp — gold latch on front
        {"offset": [0, 0.34, 0.26], "size": [0.06, 0.08, 0.02], "color": [0.80, 0.70, 0.25, 1]},
        # Base rim — bottom edge
        {"offset": [0, 0.03, 0], "size": [0.64, 0.04, 0.54], "color": [0.42, 0.30, 0.12, 1]},
    ]
}

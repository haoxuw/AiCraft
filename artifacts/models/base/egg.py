"""Egg — white oval egg, slightly narrower at the top.

A basic food item dropped by chickens.
"""

model = {
    "id": "egg",
    "height": 0.3,
    "parts": [
        # Main body — wide lower half
        {"offset": [0, 0.06, 0], "size": [0.12, 0.10, 0.12], "color": [0.95, 0.93, 0.88, 1]},
        # Upper half — narrower top
        {"offset": [0, 0.13, 0], "size": [0.10, 0.08, 0.10], "color": [0.94, 0.92, 0.86, 1]},
        # Tip — narrow crown
        {"offset": [0, 0.18, 0], "size": [0.06, 0.04, 0.06], "color": [0.93, 0.90, 0.85, 1]},
    ]
}

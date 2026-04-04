"""Fence — brown wooden post with horizontal rails.

Keeps mobs in (or out). 1.5 blocks tall for collision.
"""

model = {
    "id": "fence",
    "height": 1.0,
    "parts": [
        # Vertical post — center
        {"offset": [0, 0.36, 0], "size": [0.12, 0.72, 0.12], "color": [0.50, 0.35, 0.15, 1]},
        # Top rail — horizontal bar
        {"offset": [0, 0.58, 0], "size": [0.80, 0.06, 0.06], "color": [0.52, 0.38, 0.18, 1]},
        # Bottom rail — horizontal bar
        {"offset": [0, 0.28, 0], "size": [0.80, 0.06, 0.06], "color": [0.52, 0.38, 0.18, 1]},
    ]
}

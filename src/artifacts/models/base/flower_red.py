"""Flower (red) — small stem with a red bloom on top.

Coordinates are centered at the annotation's base (sits on top of the host
block). +Y is up. Whole model fits within a 1x1 block footprint and under
0.5 blocks tall.
"""

model = {
    "id": "flower_red",
    "height": 0.5,
    "parts": [
        # Green stem
        {"offset": [0.0, 0.18, 0.0], "size": [0.04, 0.36, 0.04],
         "color": [0.20, 0.55, 0.18, 1]},
        # Two leaves (left/right)
        {"offset": [-0.08, 0.16, 0.0], "size": [0.10, 0.04, 0.06],
         "color": [0.22, 0.60, 0.20, 1]},
        {"offset": [ 0.08, 0.16, 0.0], "size": [0.10, 0.04, 0.06],
         "color": [0.22, 0.60, 0.20, 1]},
        # Red bloom (center)
        {"offset": [0.0, 0.40, 0.0], "size": [0.18, 0.08, 0.18],
         "color": [0.90, 0.18, 0.18, 1]},
        # Yellow center
        {"offset": [0.0, 0.44, 0.0], "size": [0.08, 0.04, 0.08],
         "color": [0.95, 0.82, 0.15, 1]},
    ],
}

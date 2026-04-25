"""Flower (red) — small stem with a red bloom on top.

Coordinates are centered at the annotation's base (sits on top of the host
block). +Y is up. Whole model fits within a 1x1 block footprint and under
0.5 blocks tall.
"""

model = {
    "id": "flower_red",
    "parts": [
        {"offset": [0.0, 0.18, 0.0], "size": [0.04, 0.36, 0.04], "color": [0.2, 0.55, 0.18, 1.0]},
        {"offset": [-0.08, 0.16, 0.0], "size": [0.1, 0.04, 0.06], "color": [0.22, 0.6, 0.2, 1.0]},
        {"offset": [0.08, 0.16, 0.0], "size": [0.1, 0.04, 0.06], "color": [0.22, 0.6, 0.2, 1.0]},
        {"offset": [0.0, 0.4, 0.0], "size": [0.18, 0.08, 0.18], "color": [0.9, 0.18, 0.18, 1.0]},
        {"offset": [0.0, 0.44, 0.0], "size": [0.08, 0.04, 0.08], "color": [0.95, 0.82, 0.15, 1.0]},
    ],
}

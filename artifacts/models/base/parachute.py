"""Parachute — folded pack worn on the back.

Deploys automatically during a fall to slow descent. Red fabric.
"""

model = {
    "id": "parachute",
    "height": 0.4,
    "parts": [
        # Main pack — folded red fabric
        {"offset": [0, 0.14, 0], "size": [0.16, 0.22, 0.08], "color": [0.85, 0.25, 0.20, 1]},
        # Flap — top cover
        {"offset": [0, 0.26, 0], "size": [0.18, 0.03, 0.10], "color": [0.75, 0.20, 0.16, 1]},
        # Buckle — center clasp
        {"offset": [0, 0.14, 0.04], "size": [0.04, 0.04, 0.02], "color": [0.55, 0.50, 0.40, 1]},
        # Strap — webbing
        {"offset": [0, 0.08, 0.04], "size": [0.12, 0.02, 0.02], "color": [0.25, 0.22, 0.20, 1]},
    ]
}

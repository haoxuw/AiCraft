"""Helmet — iron helm with visor and nose guard."""

model = {
    "id": "helmet",
    "height": 0.5,
    "parts": [
        # Dome
        {"offset": [0, 0.22, 0], "size": [0.22, 0.18, 0.22], "color": [0.55, 0.55, 0.58, 1]},
        # Brim
        {"offset": [0, 0.12, 0], "size": [0.24, 0.03, 0.24], "color": [0.50, 0.50, 0.52, 1]},
        # Nose guard
        {"offset": [0, 0.16, -0.20], "size": [0.03, 0.12, 0.03], "color": [0.52, 0.52, 0.55, 1]},
        # Crest (ridge on top)
        {"offset": [0, 0.36, 0], "size": [0.03, 0.06, 0.16], "color": [0.58, 0.58, 0.60, 1]},
        # Left cheek guard
        {"offset": [-0.18, 0.12, -0.04], "size": [0.04, 0.10, 0.10], "color": [0.52, 0.52, 0.55, 1]},
        # Right cheek guard
        {"offset": [0.18, 0.12, -0.04], "size": [0.04, 0.10, 0.10], "color": [0.52, 0.52, 0.55, 1]},
    ]
}

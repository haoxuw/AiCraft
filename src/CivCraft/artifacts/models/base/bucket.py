"""Bucket — simple metal pail."""

model = {
    "id": "bucket",
    "height": 0.5,
    "equip": {
        "rotation": [0, 0, 0],
        "offset": [0, -0.12, -0.04],
        "scale": 0.6,
    },
    "parts": [
        # Body (tapered cylinder approximation)
        {"offset": [0, 0.10, 0], "size": [0.16, 0.20, 0.16], "color": [0.60, 0.60, 0.62, 1]},
        # Rim
        {"offset": [0, 0.21, 0], "size": [0.18, 0.03, 0.18], "color": [0.55, 0.55, 0.58, 1]},
        # Handle
        {"offset": [0, 0.28, 0], "size": [0.12, 0.02, 0.02], "color": [0.50, 0.50, 0.52, 1]},
        # Bottom
        {"offset": [0, 0.00, 0], "size": [0.14, 0.02, 0.14], "color": [0.52, 0.52, 0.55, 1]},
    ]
}

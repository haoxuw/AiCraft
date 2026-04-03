"""Boots — sturdy leather footwear."""

model = {
    "id": "boots",
    "height": 0.4,
    "parts": [
        # Left boot
        {"offset": [-0.08, 0.08, 0], "size": [0.08, 0.16, 0.12], "color": [0.42, 0.28, 0.14, 1]},
        # Left sole
        {"offset": [-0.08, 0.01, 0.01], "size": [0.09, 0.03, 0.14], "color": [0.30, 0.20, 0.10, 1]},
        # Right boot
        {"offset": [0.08, 0.08, 0], "size": [0.08, 0.16, 0.12], "color": [0.42, 0.28, 0.14, 1]},
        # Right sole
        {"offset": [0.08, 0.01, 0.01], "size": [0.09, 0.03, 0.14], "color": [0.30, 0.20, 0.10, 1]},
        # Left buckle
        {"offset": [-0.08, 0.12, -0.07], "size": [0.04, 0.03, 0.01], "color": [0.70, 0.65, 0.45, 1]},
        # Right buckle
        {"offset": [0.08, 0.12, -0.07], "size": [0.04, 0.03, 0.01], "color": [0.70, 0.65, 0.45, 1]},
    ]
}

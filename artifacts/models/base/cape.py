"""Cape — flowing cloth cloak."""

model = {
    "id": "cape",
    "height": 1.2,
    "parts": [
        # Cape body (wide, flat cloth)
        {"offset": [0, 0.45, 0.05], "size": [0.30, 0.70, 0.04], "color": [0.55, 0.12, 0.15, 1]},
        # Collar
        {"offset": [0, 0.82, 0.02], "size": [0.22, 0.06, 0.06], "color": [0.62, 0.15, 0.18, 1]},
        # Clasp (gold)
        {"offset": [0, 0.82, -0.04], "size": [0.04, 0.04, 0.02], "color": [0.85, 0.72, 0.20, 1]},
        # Bottom hem (slightly wider)
        {"offset": [0, 0.08, 0.06], "size": [0.32, 0.04, 0.03], "color": [0.48, 0.10, 0.12, 1]},
    ]
}

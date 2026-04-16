"""Wire — thin red line like redstone dust.

Carries signals between logic gates. Glows when powered.
"""

model = {
    "id": "wire",
    "height": 0.1,
    "parts": [
        # Wire line — thin red strip on the ground
        {"offset": [0, 0.01, 0], "size": [0.80, 0.02, 0.04], "color": [0.75, 0.12, 0.10, 1]},
        # Cross line — perpendicular connection point
        {"offset": [0, 0.01, 0], "size": [0.04, 0.02, 0.80], "color": [0.75, 0.12, 0.10, 1]},
    ]
}

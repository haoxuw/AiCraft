"""Wire — thin red line like redstone dust.

Carries signals between logic gates. Glows when powered.
"""

model = {
    "id": "wire",
    "parts": [
        {"offset": [0.0, 0.01, 0.0], "size": [1.0, 0.02, 0.04], "color": [0.75, 0.12, 0.1, 1.0]},
        {"offset": [0.0, 0.01, 0.0], "size": [0.04, 0.02, 1.0], "color": [0.75, 0.12, 0.1, 1.0]},
    ],
}

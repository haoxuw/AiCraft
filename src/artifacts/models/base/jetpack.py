"""Jetpack — two fuel tanks with crossbar and nozzles.

Worn on the back, allows flight when activated.
Steel gray body with dark accents.
"""

model = {
    "id": "jetpack",
    "height": 0.6,
    "equip": {
        "rotation": [0, 0, 0],
        "offset": [0, 0, 0.02],
        "scale": 0.9,
    },
    "parts": [
        # Left fuel tank — cylinder
        {"offset": [-0.08, 0.18, 0], "size": [0.10, 0.30, 0.10], "color": [0.38, 0.38, 0.42, 1]},
        # Right fuel tank — cylinder
        {"offset": [0.08, 0.18, 0], "size": [0.10, 0.30, 0.10], "color": [0.38, 0.38, 0.42, 1]},
        # Crossbar — connecting bar between tanks
        {"offset": [0, 0.24, 0], "size": [0.22, 0.06, 0.06], "color": [0.25, 0.22, 0.22, 1]},
        # Left tank cap — top
        {"offset": [-0.08, 0.34, 0], "size": [0.08, 0.03, 0.08], "color": [0.25, 0.22, 0.22, 1]},
        # Right tank cap — top
        {"offset": [0.08, 0.34, 0], "size": [0.08, 0.03, 0.08], "color": [0.25, 0.22, 0.22, 1]},
        # Left nozzle — exhaust
        {"offset": [-0.08, 0.02, 0], "size": [0.06, 0.06, 0.06], "color": [0.25, 0.22, 0.22, 1]},
        # Right nozzle — exhaust
        {"offset": [0.08, 0.02, 0], "size": [0.06, 0.06, 0.06], "color": [0.25, 0.22, 0.22, 1]},
        # Back plate — mounting bracket
        {"offset": [0, 0.18, -0.06], "size": [0.20, 0.24, 0.03], "color": [0.32, 0.30, 0.32, 1]},
    ]
}

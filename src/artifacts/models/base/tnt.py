"""TNT — red explosive block with white band.

Ignite to cause an explosion. Handle with care!
"""

model = {
    "id": "tnt",
    "height": 1.0,
    "parts": [
        # Red body
        {"offset": [0, 0.5, 0], "size": [0.80, 0.80, 0.80], "color": [0.85, 0.20, 0.15, 1]},
        # White band — label strip around the middle
        {"offset": [0, 0.5, 0], "size": [0.82, 0.16, 0.82], "color": [0.90, 0.88, 0.85, 1]},
        # Fuse — thin stick on top
        {"offset": [0, 0.92, 0], "size": [0.03, 0.10, 0.03], "color": [0.25, 0.22, 0.18, 1]},
    ]
}

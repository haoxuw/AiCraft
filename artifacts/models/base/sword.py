"""Sword — basic melee weapon model.

A simple sword with blade, handle, and crossguard.
Edit the parts to change the sword's appearance!
"""

model = {
    "id": "sword",
    "height": 1.0,
    "parts": [
        # Blade — long silver rectangle
        {"offset": [0, 0.35, 0], "size": [0.06, 0.60, 0.06], "color": [0.72, 0.72, 0.78, 1]},
        # Handle — short brown cylinder
        {"offset": [0, 0.04, 0], "size": [0.04, 0.12, 0.08], "color": [0.40, 0.28, 0.12, 1]},
        # Crossguard — wide thin bar
        {"offset": [0, 0.08, 0], "size": [0.16, 0.03, 0.03], "color": [0.60, 0.55, 0.45, 1]},
    ]
}

"""Sword — basic melee weapon model.

A big, visible sword with blade, handle, crossguard, and pommel.
Edit the parts to change the sword's appearance!
"""

model = {
    "id": "sword",
    "height": 1.2,
    "parts": [
        # Blade — long silver rectangle (visibly big!)
        {"offset": [0, 0.55, 0], "size": [0.10, 0.90, 0.10], "color": [0.75, 0.75, 0.82, 1]},
        # Handle — brown grip
        {"offset": [0, 0.06, 0], "size": [0.06, 0.20, 0.12], "color": [0.40, 0.28, 0.12, 1]},
        # Crossguard — wide bar
        {"offset": [0, 0.14, 0], "size": [0.24, 0.05, 0.05], "color": [0.60, 0.55, 0.45, 1]},
        # Pommel — round end
        {"offset": [0, -0.02, 0], "size": [0.08, 0.06, 0.08], "color": [0.55, 0.50, 0.40, 1]},
    ]
}

"""Apple — red fruit with a stem and green leaf.

A classic health-restoring food item.
"""

model = {
    "id": "apple",
    "height": 0.4,
    "parts": [
        # Body — round red apple
        {"offset": [0, 0.10, 0], "size": [0.18, 0.16, 0.18], "color": [0.82, 0.15, 0.12, 1]},
        # Bottom indent — darker dimple
        {"offset": [0, 0.02, 0], "size": [0.06, 0.02, 0.06], "color": [0.60, 0.10, 0.08, 1]},
        # Stem — thin brown stick
        {"offset": [0, 0.22, 0], "size": [0.02, 0.08, 0.02], "color": [0.40, 0.28, 0.12, 1]},
        # Leaf — small green shape
        {"offset": [0.03, 0.24, 0], "size": [0.06, 0.03, 0.02], "color": [0.30, 0.62, 0.18, 1]},
        # Highlight — shiny spot
        {"offset": [-0.04, 0.14, 0.04], "size": [0.04, 0.04, 0.04], "color": [0.95, 0.35, 0.30, 0.8]},
    ]
}

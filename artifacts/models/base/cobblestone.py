"""Cobblestone — rough gray stone with lighter patches.

Obtained by mining stone. Common building material.
"""

model = {
    "id": "cobblestone",
    "height": 1.0,
    "parts": [
        # Main body — gray stone
        {"offset": [0, 0.5, 0], "size": [0.80, 0.80, 0.80], "color": [0.42, 0.42, 0.44, 1]},
        # Lighter patch — surface detail
        {"offset": [0.10, 0.55, 0.12], "size": [0.30, 0.30, 0.30], "color": [0.52, 0.52, 0.54, 1]},
    ]
}

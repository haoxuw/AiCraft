"""Wood — brown log block with lighter ring detail.

Tree trunk block. Can be crafted into planks.
"""

model = {
    "id": "wood",
    "height": 1.0,
    "parts": [
        # Log body — dark brown bark
        {"offset": [0, 0.5, 0], "size": [0.80, 0.80, 0.80], "color": [0.50, 0.35, 0.15, 1]},
        # Ring detail — lighter inner wood visible on top
        {"offset": [0, 0.90, 0], "size": [0.60, 0.02, 0.60], "color": [0.65, 0.52, 0.30, 1]},
    ]
}

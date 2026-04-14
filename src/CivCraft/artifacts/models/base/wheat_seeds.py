"""Wheat Seeds — tiny green seedlings just planted.

The earliest stage of wheat growth.
"""

model = {
    "id": "wheat_seeds",
    "height": 0.2,
    "equip": {
        "rotation": [10, 15, 0],    # tilt slightly so the cluster reads from above
        "offset": [0, -0.06, -0.02],
        "scale": 1.0,               # seeds are tiny — keep at full to make them visible
    },
    "parts": [
        # Small green sprouts
        {"offset": [0, 0.04, 0], "size": [0.10, 0.08, 0.10], "color": [0.35, 0.58, 0.22, 1]},
    ]
}

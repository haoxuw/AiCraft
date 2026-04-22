"""Corner Stair — bottom slab + 3/4 upper level (L-footprint in plan)."""

model = {
    "id": "corner_stair",
    "height": 1.0,
    "parts": [
        # Bottom full slab
        {"offset": [0,     0.25, 0],     "size": [0.80, 0.40, 0.80], "color": [0.68, 0.52, 0.30, 1]},
        # Upper level: two quarters forming an L (missing +X+Z corner)
        {"offset": [-0.20, 0.70, 0],     "size": [0.40, 0.40, 0.80], "color": [0.60, 0.44, 0.24, 1]},
        {"offset": [0.20,  0.70, -0.20], "size": [0.40, 0.40, 0.40], "color": [0.60, 0.44, 0.24, 1]},
    ]
}

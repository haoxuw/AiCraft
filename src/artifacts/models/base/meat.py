"""Meat — raw drumstick dropped by animals on death.

A chunk of raw meat with a visible bone handle: large irregular meat body
at the top, pale bone shaft below, knobbed bone tip at the bottom.
Classic food-item silhouette readable at any scale.

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
"""

model = {
    "id": "meat",
    "height": 0.55,
    "equip": {
        "rotation": [15, 20, 5],   # tilted slightly toward camera when held
        "offset": [0.02, -0.06, -0.02],
        "scale": 0.60,
    },
    "parts": [
        # ── Meat body ──────────────────────────────────────────────────────────
        # Main chunk — dark raw red
        {"offset": [0, 0.39, 0], "size": [0.22, 0.22, 0.18],
         "color": [0.70, 0.18, 0.10, 1]},
        # Upper irregular lump
        {"offset": [0.04, 0.48, 0.02], "size": [0.16, 0.14, 0.14],
         "color": [0.62, 0.14, 0.08, 1]},
        # Pink inner flesh cross-section (front face)
        {"offset": [0, 0.40, -0.085], "size": [0.16, 0.14, 0.04],
         "color": [0.85, 0.48, 0.38, 1]},
        # Fat marbling spot
        {"offset": [-0.05, 0.36, 0.07], "size": [0.07, 0.06, 0.05],
         "color": [0.88, 0.76, 0.60, 1]},

        # ── Bone ───────────────────────────────────────────────────────────────
        # Shaft — slim, off-white
        {"offset": [0, 0.17, 0], "size": [0.07, 0.28, 0.07],
         "color": [0.88, 0.84, 0.75, 1]},
        # Bottom knob — wider than shaft
        {"offset": [0, 0.03, 0], "size": [0.13, 0.09, 0.11],
         "color": [0.84, 0.80, 0.70, 1]},
    ]
}

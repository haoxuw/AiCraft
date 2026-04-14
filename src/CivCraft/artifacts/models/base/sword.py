"""Sword — Frostmourne-inspired runic greatsword.

A massive frost-enchanted blade with a skull crossguard, runic engravings,
and a dark leather grip. The blade has an icy blue glow running through
layered steel. Unmistakable silhouette — long, wide, and menacing.

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
"""

model = {
    "id": "sword",
    "height": 1.4,
    "equip": {
        # Blade points up and slightly forward from the grip (Minecraft-style).
        # Z tilt kept at 0 so the blade doesn't lie sideways across the body.
        "rotation": [-10, 0, 0],
        "offset": [0.0, 0.0, -0.02],
        "scale": 0.65,
    },
    "parts": [
        # ── Blade ──────────────────────────────────────────────────────────────
        # Core blade — depth 0.10 so it stays readable at ground scale (0.25×)
        {"offset": [0, 0.65, 0], "size": [0.14, 1.00, 0.10],
         "color": [0.48, 0.52, 0.60, 1]},
        # Frost edge highlight (front face, slightly proud)
        {"offset": [0, 0.65, -0.052], "size": [0.10, 0.96, 0.012],
         "color": [0.75, 0.82, 0.95, 1]},
        # Blade tip
        {"offset": [0, 1.18, 0], "size": [0.07, 0.10, 0.07],
         "color": [0.82, 0.88, 0.98, 1]},

        # ── Rune channel (frost glow, embedded in front face) ────────────────
        {"offset": [0, 0.60, -0.056], "size": [0.035, 0.65, 0.008],
         "color": [0.25, 0.60, 0.95, 1]},

        # ── Crossguard ───────────────────────────────────────────────────────
        # Main bar — thick enough to see at any angle
        {"offset": [0, 0.17, 0], "size": [0.44, 0.07, 0.12],
         "color": [0.35, 0.36, 0.42, 1]},
        # Swept wing tips (angled down)
        {"offset": [-0.24, 0.12, 0], "size": [0.06, 0.10, 0.10],
         "color": [0.28, 0.29, 0.36, 1]},
        {"offset": [ 0.24, 0.12, 0], "size": [0.06, 0.10, 0.10],
         "color": [0.28, 0.29, 0.36, 1]},
        # Skull face (center detail)
        {"offset": [0, 0.18, -0.065], "size": [0.10, 0.10, 0.022],
         "color": [0.76, 0.72, 0.65, 1]},
        # Eye sockets
        {"offset": [-0.025, 0.20, -0.076], "size": [0.028, 0.028, 0.006],
         "color": [0.15, 0.25, 0.48, 1]},
        {"offset": [ 0.025, 0.20, -0.076], "size": [0.028, 0.028, 0.006],
         "color": [0.15, 0.25, 0.48, 1]},

        # ── Grip ────────────────────────────────────────────────────────────
        {"offset": [0, 0.06, 0], "size": [0.08, 0.20, 0.08],
         "color": [0.22, 0.15, 0.08, 1]},
        # Wrap band
        {"offset": [0, 0.07, -0.040], "size": [0.065, 0.025, 0.018],
         "color": [0.32, 0.22, 0.12, 1]},

        # ── Pommel ──────────────────────────────────────────────────────────
        {"offset": [0, -0.04, 0], "size": [0.12, 0.10, 0.12],
         "color": [0.36, 0.36, 0.44, 1]},
        # Frost crystal
        {"offset": [0, -0.04, -0.062], "size": [0.05, 0.05, 0.012],
         "color": [0.28, 0.62, 0.95, 1]},
    ]
}

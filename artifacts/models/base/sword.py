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
        "rotation": [-75, 0, 10],   # blade angled forward and slightly out
        "offset": [0, -0.02, -0.08], # snug to hand
        "scale": 0.80,
    },
    "parts": [
        # ── Blade (3 layers for depth) ──────────────────────────
        # Core blade — dark steel
        {"offset": [0, 0.65, 0], "size": [0.12, 1.00, 0.04],
         "color": [0.45, 0.48, 0.55, 1]},
        # Blade edge — bright steel with frost tint
        {"offset": [0, 0.65, -0.015], "size": [0.14, 0.96, 0.015],
         "color": [0.72, 0.78, 0.88, 1]},
        # Blade back edge
        {"offset": [0, 0.65, 0.015], "size": [0.14, 0.96, 0.015],
         "color": [0.68, 0.72, 0.82, 1]},
        # Blade tip — narrower, brighter
        {"offset": [0, 1.18, 0], "size": [0.06, 0.10, 0.03],
         "color": [0.80, 0.85, 0.95, 1]},

        # ── Rune channel (frost glow running up the blade) ──────
        {"offset": [0, 0.50, -0.025], "size": [0.03, 0.50, 0.008],
         "color": [0.30, 0.65, 0.95, 1]},
        {"offset": [0, 0.80, -0.025], "size": [0.03, 0.20, 0.008],
         "color": [0.45, 0.75, 1.00, 1]},
        # Rune marks (4 dots along the blade)
        {"offset": [0, 0.40, -0.028], "size": [0.04, 0.03, 0.005],
         "color": [0.20, 0.55, 0.90, 1]},
        {"offset": [0, 0.55, -0.028], "size": [0.04, 0.03, 0.005],
         "color": [0.20, 0.55, 0.90, 1]},
        {"offset": [0, 0.70, -0.028], "size": [0.04, 0.03, 0.005],
         "color": [0.20, 0.55, 0.90, 1]},
        {"offset": [0, 0.85, -0.028], "size": [0.04, 0.03, 0.005],
         "color": [0.20, 0.55, 0.90, 1]},

        # ── Crossguard (skull-wing shape) ───────────────────────
        # Main crossbar — wide, dark metal
        {"offset": [0, 0.17, 0], "size": [0.36, 0.05, 0.06],
         "color": [0.35, 0.35, 0.40, 1]},
        # Crossguard wings (swept down)
        {"offset": [-0.17, 0.14, 0], "size": [0.08, 0.08, 0.05],
         "color": [0.32, 0.32, 0.38, 1]},
        {"offset": [0.17, 0.14, 0], "size": [0.08, 0.08, 0.05],
         "color": [0.32, 0.32, 0.38, 1]},
        # Wing tips (pointed down)
        {"offset": [-0.20, 0.10, 0], "size": [0.04, 0.06, 0.04],
         "color": [0.28, 0.28, 0.35, 1]},
        {"offset": [0.20, 0.10, 0], "size": [0.04, 0.06, 0.04],
         "color": [0.28, 0.28, 0.35, 1]},

        # ── Skull (center of crossguard) ────────────────────────
        # Skull face
        {"offset": [0, 0.17, -0.035], "size": [0.09, 0.08, 0.02],
         "color": [0.75, 0.72, 0.65, 1]},
        # Eye sockets (dark)
        {"offset": [-0.02, 0.18, -0.045], "size": [0.025, 0.02, 0.005],
         "color": [0.15, 0.25, 0.45, 1]},
        {"offset": [0.02, 0.18, -0.045], "size": [0.025, 0.02, 0.005],
         "color": [0.15, 0.25, 0.45, 1]},

        # ── Grip (dark leather wrap) ────────────────────────────
        {"offset": [0, 0.06, 0], "size": [0.06, 0.18, 0.06],
         "color": [0.22, 0.15, 0.08, 1]},
        # Leather wrap bands
        {"offset": [0, 0.10, -0.025], "size": [0.05, 0.02, 0.015],
         "color": [0.30, 0.20, 0.10, 1]},
        {"offset": [0, 0.04, -0.025], "size": [0.05, 0.02, 0.015],
         "color": [0.30, 0.20, 0.10, 1]},

        # ── Pommel (heavy counterweight) ────────────────────────
        # Main pommel sphere
        {"offset": [0, -0.04, 0], "size": [0.09, 0.07, 0.09],
         "color": [0.35, 0.35, 0.42, 1]},
        # Pommel gem (frost crystal)
        {"offset": [0, -0.04, -0.045], "size": [0.04, 0.04, 0.01],
         "color": [0.30, 0.65, 0.95, 1]},
    ]
}

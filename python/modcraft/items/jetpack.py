"""Jetpack -- twin-tank propulsion pack with flame effects.

Client-side visual only. The server handles the game mechanic
(mid-air thrust when holding jump with jetpack in inventory).
The client reads entity prop "jetpack_active" and fires the
flame particle emitters.
"""

from modcraft.items.base import ItemVisual, ItemPiece, ActiveEffect, ParticleEmitter

STEEL      = (0.38, 0.38, 0.42, 1)
STEEL_LITE = (0.50, 0.52, 0.55, 1)
FRAME      = (0.30, 0.30, 0.32, 1)
NOZZLE     = (0.25, 0.22, 0.22, 1)
GLOW       = (0.60, 0.30, 0.08, 1)
STRAP      = (0.28, 0.25, 0.22, 1)

jetpack = ItemVisual(
    id="base:jetpack",
    name="Jetpack",
    slot="back",
    pieces=[
        # Twin fuel tanks
        ItemPiece("tank_L",  STEEL,      (-0.08, 0, 0.02),    (0.06, 0.18, 0.06)),
        ItemPiece("tank_R",  STEEL,      ( 0.08, 0, 0.02),    (0.06, 0.18, 0.06)),
        # Tank caps
        ItemPiece("cap_L",   STEEL_LITE, (-0.08, 0.18, 0.02), (0.05, 0.03, 0.05)),
        ItemPiece("cap_R",   STEEL_LITE, ( 0.08, 0.18, 0.02), (0.05, 0.03, 0.05)),
        # Frame
        ItemPiece("crossbar",FRAME,      (0, 0.08, 0),        (0.14, 0.02, 0.04)),
        ItemPiece("spine",   FRAME,      (0, -0.02, -0.02),   (0.02, 0.16, 0.02)),
        # Nozzles
        ItemPiece("nozzle_L",NOZZLE,     (-0.08, -0.20, 0.02),(0.05, 0.04, 0.05)),
        ItemPiece("nozzle_R",NOZZLE,     ( 0.08, -0.20, 0.02),(0.05, 0.04, 0.05)),
        # Inner glow (visible even when idle)
        ItemPiece("glow_L",  GLOW,       (-0.08, -0.22, 0.02),(0.03, 0.02, 0.03)),
        ItemPiece("glow_R",  GLOW,       ( 0.08, -0.22, 0.02),(0.03, 0.02, 0.03)),
        # Shoulder straps
        ItemPiece("strap_L", STRAP,      (-0.06, 0.14, -0.06),(0.02, 0.10, 0.02)),
        ItemPiece("strap_R", STRAP,      ( 0.06, 0.14, -0.06),(0.02, 0.10, 0.02)),
    ],
    effects=[
        ActiveEffect(
            trigger="jetpack_active",
            emitters=[
                # Left nozzle flame
                ParticleEmitter(
                    offset=(-0.08, -0.22, 0.02),
                    rate=4,
                    velocity=(0, -5.0, 0),
                    velocity_spread=0.6,
                    colors=[
                        (1.0, 0.95, 0.80, 1.0),  # core: white-hot
                        (1.0, 0.75, 0.15, 1.0),   # inner: bright yellow
                        (1.0, 0.35, 0.05, 0.9),   # outer: orange-red
                    ],
                    life_range=(0.08, 0.25),
                    size_range=(0.03, 0.06),
                ),
                # Right nozzle flame
                ParticleEmitter(
                    offset=(0.08, -0.22, 0.02),
                    rate=4,
                    velocity=(0, -5.0, 0),
                    velocity_spread=0.6,
                    colors=[
                        (1.0, 0.95, 0.80, 1.0),
                        (1.0, 0.75, 0.15, 1.0),
                        (1.0, 0.35, 0.05, 0.9),
                    ],
                    life_range=(0.08, 0.25),
                    size_range=(0.03, 0.06),
                ),
            ]
        )
    ]
)

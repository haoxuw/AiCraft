"""Blue Knight - a stalwart defender in steel-trimmed blue plate.

Signature: symmetric pauldrons, chest plate overlay with golden emblem,
steel gauntlets, trailing cape, shin guards.
"""

from math import pi
from base import Character, Part, Stats

# Skin tones
SKIN = (0.85, 0.70, 0.55, 1)
HAIR = (0.25, 0.18, 0.12, 1)

# Armor palette
BLUE       = (0.18, 0.32, 0.72, 1)
BLUE_DARK  = (0.12, 0.22, 0.55, 1)
CAPE_BLUE  = (0.14, 0.25, 0.60, 1)
STEEL      = (0.55, 0.58, 0.62, 1)
STEEL_DARK = (0.38, 0.40, 0.45, 1)
BELT       = (0.35, 0.25, 0.15, 1)
BOOTS      = (0.30, 0.22, 0.14, 1)
PANTS      = (0.18, 0.18, 0.28, 1)
GOLD       = (0.80, 0.70, 0.20, 1)
EYE        = (0.15, 0.25, 0.70, 1)

blue_knight = Character(
    id="base:blue_knight",
    name="Blue Knight",
    description="A stalwart defender clad in steel-trimmed blue plate.",
    stats=Stats(strength=4, stamina=4, agility=2, intelligence=2),
    parts=[
        # -- Head --
        Part("head",     (0, 1.75, 0),    (0.50, 0.50, 0.50), SKIN,
             pivot=(0, 1.5, 0), swing_axis=(1,0,0), swing_amplitude=5, swing_speed=2),
        Part("hair",     (0, 1.97, 0.02), (0.52, 0.12, 0.48), HAIR),
        Part("eye_L",    (-0.09, 1.78, -0.24), (0.07, 0.05, 0.02), EYE),
        Part("eye_R",    ( 0.09, 1.78, -0.24), (0.07, 0.05, 0.02), EYE),
        Part("mouth",    (0, 1.67, -0.24), (0.12, 0.03, 0.02), (0.65, 0.42, 0.38, 1)),

        # -- Torso (Y-axis counter-twist) --
        Part("torso",    (0, 1.08, 0),    (0.50, 0.64, 0.30), BLUE,
             pivot=(0, 1.08, 0), swing_axis=(0,1,0), swing_amplitude=4, swing_phase=pi),
        Part("chestplate",(0, 1.12, -0.14),(0.44, 0.52, 0.04), STEEL),
        Part("emblem",   (0, 1.18, -0.17),(0.10, 0.10, 0.02), GOLD),
        Part("gorget",   (0, 1.39, 0),    (0.46, 0.08, 0.30), STEEL),
        Part("belt",     (0, 0.76, 0),    (0.52, 0.08, 0.32), BELT),
        Part("buckle",   (0, 0.76, -0.15),(0.08, 0.06, 0.04), GOLD),

        # -- Left shoulder: pauldron + arm + gauntlet + hand --
        Part("pauldron_L",(-0.37, 1.38, 0),(0.28, 0.12, 0.28), STEEL,
             pivot=(-0.37, 1.40, 0), swing_axis=(1,0,0), swing_amplitude=40, swing_phase=pi),
        Part("arm_L",    (-0.37, 1.08, 0),(0.20, 0.56, 0.20), BLUE_DARK,
             pivot=(-0.37, 1.40, 0), swing_axis=(1,0,0), swing_amplitude=40, swing_phase=pi),
        Part("gauntlet_L",(-0.37, 0.86, 0),(0.24, 0.18, 0.24), STEEL_DARK,
             pivot=(-0.37, 1.40, 0), swing_axis=(1,0,0), swing_amplitude=40, swing_phase=pi),
        Part("hand_L",   (-0.37, 0.77, 0),(0.16, 0.10, 0.16), SKIN,
             pivot=(-0.37, 1.40, 0), swing_axis=(1,0,0), swing_amplitude=40, swing_phase=pi),

        # -- Right shoulder: pauldron + arm + gauntlet + hand --
        Part("pauldron_R",( 0.37, 1.38, 0),(0.28, 0.12, 0.28), STEEL,
             pivot=( 0.37, 1.40, 0), swing_axis=(1,0,0), swing_amplitude=40, swing_phase=0),
        Part("arm_R",    ( 0.37, 1.08, 0),(0.20, 0.56, 0.20), BLUE_DARK,
             pivot=( 0.37, 1.40, 0), swing_axis=(1,0,0), swing_amplitude=40, swing_phase=0),
        Part("gauntlet_R",( 0.37, 0.86, 0),(0.24, 0.18, 0.24), STEEL_DARK,
             pivot=( 0.37, 1.40, 0), swing_axis=(1,0,0), swing_amplitude=40, swing_phase=0),
        Part("hand_R",   ( 0.37, 0.77, 0),(0.16, 0.10, 0.16), SKIN,
             pivot=( 0.37, 1.40, 0), swing_axis=(1,0,0), swing_amplitude=40, swing_phase=0),

        # -- Cape (trails behind with own sway) --
        Part("cape",     (0, 0.95, 0.20),(0.44, 0.76, 0.06), CAPE_BLUE,
             pivot=(0, 1.35, 0.18), swing_axis=(1,0,0), swing_amplitude=10, swing_speed=1.2),

        # -- Left leg + shin guard + boot --
        Part("leg_L",    (-0.13, 0.40, 0),(0.22, 0.48, 0.22), PANTS,
             pivot=(-0.13, 0.70, 0), swing_axis=(1,0,0), swing_amplitude=45, swing_phase=0),
        Part("shinguard_L",(-0.13, 0.38, -0.12),(0.16, 0.28, 0.04), STEEL_DARK,
             pivot=(-0.13, 0.70, 0), swing_axis=(1,0,0), swing_amplitude=45, swing_phase=0),
        Part("boot_L",   (-0.13, 0.10, 0),(0.24, 0.20, 0.26), BOOTS,
             pivot=(-0.13, 0.70, 0), swing_axis=(1,0,0), swing_amplitude=45, swing_phase=0),

        # -- Right leg + shin guard + boot --
        Part("leg_R",    ( 0.13, 0.40, 0),(0.22, 0.48, 0.22), PANTS,
             pivot=( 0.13, 0.70, 0), swing_axis=(1,0,0), swing_amplitude=45, swing_phase=pi),
        Part("shinguard_R",( 0.13, 0.38, -0.12),(0.16, 0.28, 0.04), STEEL_DARK,
             pivot=( 0.13, 0.70, 0), swing_axis=(1,0,0), swing_amplitude=45, swing_phase=pi),
        Part("boot_R",   ( 0.13, 0.10, 0),(0.24, 0.20, 0.26), BOOTS,
             pivot=( 0.13, 0.70, 0), swing_axis=(1,0,0), swing_amplitude=45, swing_phase=pi),
    ]
)

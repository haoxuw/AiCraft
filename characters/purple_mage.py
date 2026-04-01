"""Purple Mage - wielder of arcane arts.

Signature: wide-hem robe tapering to narrow chest, tall 4-section conical
hat, bell sleeves, animated staff with glowing gem (right arm).
Animation: slow dignified walk, minimal sway (robes drape naturally).
"""

from math import pi
from base import Character, Part, Stats

ROBE        = (0.45, 0.10, 0.65, 1)
ROBE_DARK   = (0.36, 0.06, 0.54, 1)   # hem, bell sleeves
ROBE_LITE   = (0.60, 0.20, 0.84, 1)   # chest highlight
HAT         = (0.22, 0.05, 0.34, 1)
HAT_BAND    = (0.80, 0.68, 0.12, 1)   # gold
SKIN        = (0.92, 0.82, 0.70, 1)
STAFF_WOOD  = (0.40, 0.28, 0.12, 1)
GEM         = (0.38, 0.70, 1.00, 1)   # arcane blue
GEM_BRIGHT  = (0.76, 0.92, 1.00, 1)
LEG         = (0.32, 0.06, 0.48, 1)

purple_mage = Character(
    id="base:purple_mage",
    name="Purple Mage",
    description="A wielder of arcane arts, draped in star-dusted robes.",
    stats=Stats(strength=1, stamina=2, agility=3, intelligence=5),
    height=2.4,
    walk_cycle_speed=2.6,
    walk_bob_amount=0.040,
    idle_bob_amount=0.010,
    parts=[
        # -- Robe body (wide hem → narrow chest) --
        Part("hem",        (0, 0.40, 0),    (0.68, 0.20, 0.48), ROBE_DARK),
        Part("robe_low",   (0, 0.64, 0),    (0.60, 0.48, 0.44), ROBE),
        Part("robe_up",    (0, 1.08, 0),    (0.44, 0.44, 0.32), ROBE),
        Part("chest_hi",   (0, 1.08, -0.15),(0.28, 0.32, 0.04), ROBE_LITE),
        Part("belt",       (0, 0.87, -0.21),(0.36, 0.06, 0.04), HAT_BAND),
        Part("clasp",      (0, 0.87, -0.23),(0.08, 0.08, 0.02), (0.95, 0.88, 0.20, 1)),

        # -- Head --
        Part("head",       (0, 1.65, 0),    (0.44, 0.44, 0.44), SKIN,
             pivot=(0, 1.44, 0), swing_axis=(1,0,0), swing_amplitude=5, swing_speed=2),

        # -- Tall conical hat --
        Part("brim",       (0, 1.89, 0),    (0.56, 0.08, 0.56), HAT,
             pivot=(0, 1.44, 0), swing_axis=(1,0,0), swing_amplitude=5, swing_speed=2),
        Part("hat_band",   (0, 1.94, 0),    (0.42, 0.05, 0.42), HAT_BAND,
             pivot=(0, 1.44, 0), swing_axis=(1,0,0), swing_amplitude=5, swing_speed=2),
        Part("hat_low",    (0, 2.02, 0),    (0.36, 0.16, 0.36), HAT,
             pivot=(0, 1.44, 0), swing_axis=(1,0,0), swing_amplitude=5, swing_speed=2),
        Part("hat_mid",    (0, 2.16, 0),    (0.24, 0.20, 0.24), HAT,
             pivot=(0, 1.44, 0), swing_axis=(1,0,0), swing_amplitude=5, swing_speed=2),
        Part("hat_tip",    (0, 2.32, 0),    (0.14, 0.28, 0.14), HAT,
             pivot=(0, 1.44, 0), swing_axis=(1,0,0), swing_amplitude=5, swing_speed=2),
        Part("hat_star",   (0.07, 2.10, -0.12), (0.06, 0.06, 0.02), (0.95, 0.88, 0.20, 1),
             pivot=(0, 1.44, 0), swing_axis=(1,0,0), swing_amplitude=5, swing_speed=2),

        # -- Arms and bell sleeves --
        Part("arm_L",      (-0.32, 1.08, 0), (0.20, 0.60, 0.20), ROBE,
             pivot=(-0.32, 1.40, 0), swing_axis=(1,0,0), swing_amplitude=36, swing_phase=pi),
        Part("bell_L",     (-0.32, 0.82, 0), (0.28, 0.12, 0.24), ROBE_DARK,
             pivot=(-0.32, 1.40, 0), swing_axis=(1,0,0), swing_amplitude=36, swing_phase=pi),
        Part("hand_L",     (-0.32, 0.74, 0), (0.16, 0.10, 0.14), SKIN,
             pivot=(-0.32, 1.40, 0), swing_axis=(1,0,0), swing_amplitude=36, swing_phase=pi),

        Part("arm_R",      ( 0.32, 1.08, 0), (0.20, 0.60, 0.20), ROBE,
             pivot=( 0.32, 1.40, 0), swing_axis=(1,0,0), swing_amplitude=36, swing_phase=0),
        Part("bell_R",     ( 0.32, 0.82, 0), (0.28, 0.12, 0.24), ROBE_DARK,
             pivot=( 0.32, 1.40, 0), swing_axis=(1,0,0), swing_amplitude=36, swing_phase=0),
        Part("hand_R",     ( 0.32, 0.74, 0), (0.16, 0.10, 0.14), SKIN,
             pivot=( 0.32, 1.40, 0), swing_axis=(1,0,0), swing_amplitude=36, swing_phase=0),

        # -- Staff (animated with right arm) --
        Part("staff",      (0.44, 0.82, -0.08), (0.06, 0.96, 0.06), STAFF_WOOD,
             pivot=( 0.32, 1.40, 0), swing_axis=(1,0,0), swing_amplitude=36, swing_phase=0),
        Part("gem",        (0.44, 1.34, -0.08), (0.18, 0.18, 0.18), GEM,
             pivot=( 0.32, 1.40, 0), swing_axis=(1,0,0), swing_amplitude=36, swing_phase=0),
        Part("gem_core",   (0.44, 1.34, -0.08), (0.10, 0.10, 0.10), GEM_BRIGHT,
             pivot=( 0.32, 1.40, 0), swing_axis=(1,0,0), swing_amplitude=36, swing_phase=0),

        # -- Legs (peek below robe) --
        Part("leg_L",  (-0.10, 0.22, 0), (0.18, 0.44, 0.20), LEG,
             pivot=(-0.10, 0.48, 0), swing_axis=(1,0,0), swing_amplitude=30, swing_phase=0),
        Part("leg_R",  ( 0.10, 0.22, 0), (0.18, 0.44, 0.20), LEG,
             pivot=( 0.10, 0.48, 0), swing_axis=(1,0,0), swing_amplitude=30, swing_phase=pi),
    ]
)

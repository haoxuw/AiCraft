"""Giant - massive constructed guardian (renamed from Iron Golem).

Signature: enormous wide torso, giant fists, glowing orange chest crack,
orange eye slits, no neck (head sits directly on shoulders), shoulder bolts.
Animation: very slow lumbering walk with huge bounce and heavy lateral sway.

Stats: STR 5  STA 5  AGI 1  INT 1
"""

from math import pi
from base import Character, Part, Stats

IRON        = (0.45, 0.42, 0.40, 1)
IRON_LITE   = (0.48, 0.45, 0.43, 1)   # upper torso
IRON_DARK   = (0.32, 0.30, 0.28, 1)   # feet, fists
HEAD        = (0.42, 0.40, 0.38, 1)
BOLT        = (0.22, 0.20, 0.18, 1)
CRACK       = (1.00, 0.58, 0.08, 1)   # orange lava crack
CRACK_CORE  = (1.00, 0.85, 0.30, 1)   # bright inner
EYES        = (1.00, 0.55, 0.05, 1)   # orange eye slits
NOSE        = (0.32, 0.30, 0.28, 1)

giant = Character(
    id="base:giant",
    name="Giant",
    description="A massive iron guardian. Each step shakes the ground.",
    stats=Stats(strength=5, stamina=5, agility=1, intelligence=1),
    height=2.2,
    walk_cycle_speed=1.8,
    walk_bob_amount=0.09,
    idle_bob_amount=0.005,
    lateral_sway_amount=0.10,
    parts=[
        # -- Legs (thick, heavy) --
        Part("leg_L",  (-0.22, 0.28, 0), (0.36, 0.56, 0.40), IRON,
             pivot=(-0.22, 0.58, 0), swing_axis=(1,0,0), swing_amplitude=50, swing_phase=0),
        Part("leg_R",  ( 0.22, 0.28, 0), (0.36, 0.56, 0.40), IRON,
             pivot=( 0.22, 0.58, 0), swing_axis=(1,0,0), swing_amplitude=50, swing_phase=pi),
        # Feet (wide flat blocks)
        Part("foot_L", (-0.22, 0.05, 0.04), (0.40, 0.16, 0.48), IRON_DARK,
             pivot=(-0.22, 0.58, 0), swing_axis=(1,0,0), swing_amplitude=50, swing_phase=0),
        Part("foot_R", ( 0.22, 0.05, 0.04), (0.40, 0.16, 0.48), IRON_DARK,
             pivot=( 0.22, 0.58, 0), swing_axis=(1,0,0), swing_amplitude=50, swing_phase=pi),

        # -- Torso (very wide) --
        Part("torso_low", (0, 0.76, 0), (0.80, 0.52, 0.60), IRON),
        Part("torso_up",  (0, 1.18, 0), (0.84, 0.52, 0.56), IRON_LITE),
        # Chest crack (lava glow)
        Part("crack",     (0, 1.10, -0.27), (0.24, 0.36, 0.02), CRACK),
        Part("crack_in",  (0, 1.10, -0.28), (0.12, 0.20, 0.02), CRACK_CORE),
        # Rivets
        Part("rivet_L",   (-0.24, 0.92, -0.29), (0.08, 0.08, 0.02), BOLT),
        Part("rivet_R",   ( 0.24, 0.92, -0.29), (0.08, 0.08, 0.02), BOLT),
        Part("rivet_C",   (0, 0.92, -0.29),     (0.06, 0.06, 0.02), BOLT),

        # -- Head (massive, no neck) --
        Part("head",   (0, 1.62, 0), (0.60, 0.52, 0.56), HEAD,
             pivot=(0, 1.46, 0), swing_axis=(1,0,0), swing_amplitude=3, swing_speed=1.5),
        Part("eye_L",  (-0.14, 1.68, -0.27), (0.16, 0.10, 0.02), EYES,
             pivot=(0, 1.46, 0), swing_axis=(1,0,0), swing_amplitude=3, swing_speed=1.5),
        Part("eye_R",  ( 0.14, 1.68, -0.27), (0.16, 0.10, 0.02), EYES,
             pivot=(0, 1.46, 0), swing_axis=(1,0,0), swing_amplitude=3, swing_speed=1.5),
        Part("crack_f",(0.06, 1.76, -0.27),  (0.12, 0.16, 0.02), CRACK,
             pivot=(0, 1.46, 0), swing_axis=(1,0,0), swing_amplitude=3, swing_speed=1.5),
        Part("jaw",    (0, 1.58, -0.28),     (0.16, 0.12, 0.02), NOSE,
             pivot=(0, 1.46, 0), swing_axis=(1,0,0), swing_amplitude=3, swing_speed=1.5),

        # -- Shoulder bolts --
        Part("bolt_L", (-0.48, 1.34, 0), (0.10, 0.10, 0.10), BOLT),
        Part("bolt_R", ( 0.48, 1.34, 0), (0.10, 0.10, 0.10), BOLT),

        # -- Arms (MASSIVE) --
        Part("arm_L",  (-0.64, 1.00, 0), (0.40, 0.84, 0.40), IRON,
             pivot=(-0.48, 1.36, 0), swing_axis=(1,0,0), swing_amplitude=48, swing_phase=pi),
        Part("fist_L", (-0.64, 0.52, 0), (0.48, 0.40, 0.48), IRON_DARK,
             pivot=(-0.48, 1.36, 0), swing_axis=(1,0,0), swing_amplitude=48, swing_phase=pi),
        Part("arm_R",  ( 0.64, 1.00, 0), (0.40, 0.84, 0.40), IRON,
             pivot=( 0.48, 1.36, 0), swing_axis=(1,0,0), swing_amplitude=48, swing_phase=0),
        Part("fist_R", ( 0.64, 0.52, 0), (0.48, 0.40, 0.48), IRON_DARK,
             pivot=( 0.48, 1.36, 0), swing_axis=(1,0,0), swing_amplitude=48, swing_phase=0),
    ]
)

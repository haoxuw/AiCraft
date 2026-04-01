"""Skeleton - an undead warrior draped in rusted iron.

Signature: bone crown spikes on skull, asymmetric rusted pauldron
(left only), tattered cloth hanging from ribs, shield fragment
on back, rib detail bars, rusty chain belt.
"""

from math import pi
from base import Character, Part, Stats

BONE       = (0.88, 0.85, 0.78, 1)
BONE_DARK  = (0.70, 0.65, 0.58, 1)
BONE_MID   = (0.80, 0.76, 0.68, 1)
BONE_JAW   = (0.82, 0.78, 0.70, 1)
SOCKET     = (0.10, 0.05, 0.05, 1)
TEETH      = (0.92, 0.90, 0.85, 1)
RUST       = (0.52, 0.35, 0.22, 1)
RUST_DARK  = (0.42, 0.30, 0.18, 1)
CLOTH      = (0.30, 0.28, 0.25, 0.85)
EYE_GLOW   = (0.90, 0.20, 0.10, 1)

skeleton = Character(
    id="base:skeleton",
    name="Skeleton",
    description="An undead warrior draped in rusted iron.",
    stats=Stats(strength=3, stamina=2, agility=4, intelligence=3),
    walk_cycle_speed=2.7,
    walk_bob_amount=0.03,
    idle_bob_amount=0.008,
    parts=[
        # -- Skull --
        Part("skull",    (0, 1.78, 0),    (0.46, 0.46, 0.46), BONE,
             pivot=(0, 1.55, 0), swing_axis=(1,0,0), swing_amplitude=6, swing_speed=2),
        Part("socket_L", (-0.09, 1.82, -0.22), (0.10, 0.08, 0.04), SOCKET),
        Part("socket_R", ( 0.09, 1.82, -0.22), (0.10, 0.08, 0.04), SOCKET),
        Part("eye_L",   (-0.09, 1.82, -0.23), (0.05, 0.05, 0.02), EYE_GLOW),
        Part("eye_R",   ( 0.09, 1.82, -0.23), (0.05, 0.05, 0.02), EYE_GLOW),
        Part("jaw",     (0, 1.62, -0.02),(0.36, 0.10, 0.34), BONE_JAW,
             pivot=(0, 1.55, 0), swing_axis=(1,0,0), swing_amplitude=6, swing_speed=2),
        Part("teeth",   (0, 1.65, -0.18),(0.24, 0.04, 0.02), TEETH),

        # Bone crown spikes (3 jagged prongs)
        Part("crown_C", (0, 2.02, 0),       (0.10, 0.08, 0.10), BONE_JAW,
             pivot=(0, 1.55, 0), swing_axis=(1,0,0), swing_amplitude=6, swing_speed=2),
        Part("crown_L", (-0.14, 1.99, 0),   (0.08, 0.06, 0.08), BONE_JAW,
             pivot=(0, 1.55, 0), swing_axis=(1,0,0), swing_amplitude=6, swing_speed=2),
        Part("crown_R", ( 0.14, 1.99, 0),   (0.08, 0.06, 0.08), BONE_JAW,
             pivot=(0, 1.55, 0), swing_axis=(1,0,0), swing_amplitude=6, swing_speed=2),

        # -- Ribcage (Y-axis counter-twist) --
        Part("spine",   (0, 1.10, 0),    (0.12, 0.60, 0.12), BONE_DARK,
             pivot=(0, 1.10, 0), swing_axis=(0,1,0), swing_amplitude=4, swing_phase=pi),
        Part("ribs",    (0, 1.18, 0),    (0.40, 0.36, 0.22), BONE),
        Part("rib_L",   (-0.14, 1.08, -0.06),(0.16, 0.04, 0.12), BONE_MID),
        Part("rib_R",   ( 0.14, 1.08, -0.06),(0.16, 0.04, 0.12), BONE_MID),
        Part("pelvis",  (0, 0.72, 0),    (0.36, 0.10, 0.22), BONE_DARK),
        Part("cloth",   (0, 0.88, -0.08),(0.32, 0.28, 0.04), CLOTH),
        Part("chain",   (0, 0.72, -0.10),(0.40, 0.06, 0.04), RUST),

        # -- Left: rusted pauldron + thin bone arm + hand --
        Part("pauldron_L",(-0.32, 1.40, 0),(0.24, 0.12, 0.20), RUST,
             pivot=(-0.32, 1.38, 0), swing_axis=(1,0,0), swing_amplitude=38, swing_phase=pi),
        Part("arm_L",   (-0.32, 1.10, 0),(0.10, 0.52, 0.10), BONE,
             pivot=(-0.32, 1.38, 0), swing_axis=(1,0,0), swing_amplitude=38, swing_phase=pi),
        Part("hand_L",  (-0.32, 0.80, 0),(0.12, 0.08, 0.08), BONE_DARK,
             pivot=(-0.32, 1.38, 0), swing_axis=(1,0,0), swing_amplitude=38, swing_phase=pi),

        # -- Right: bare bone arm (no pauldron) + hand --
        Part("arm_R",   ( 0.32, 1.10, 0),(0.10, 0.52, 0.10), BONE,
             pivot=( 0.32, 1.38, 0), swing_axis=(1,0,0), swing_amplitude=38, swing_phase=0),
        Part("hand_R",  ( 0.32, 0.80, 0),(0.12, 0.08, 0.08), BONE_DARK,
             pivot=( 0.32, 1.38, 0), swing_axis=(1,0,0), swing_amplitude=38, swing_phase=0),

        # -- Shield fragment on back --
        Part("shield",  (0.08, 1.05, 0.16),(0.28, 0.32, 0.04), RUST_DARK),
        Part("shield_boss",(0.08, 1.05, 0.13),(0.08, 0.08, 0.04), RUST),

        # -- Left leg (thin bone) + foot --
        Part("leg_L",   (-0.12, 0.38, 0),(0.12, 0.44, 0.12), BONE,
             pivot=(-0.12, 0.66, 0), swing_axis=(1,0,0), swing_amplitude=42, swing_phase=0),
        Part("foot_L",  (-0.12, 0.08, -0.04),(0.14, 0.10, 0.22), BONE_DARK,
             pivot=(-0.12, 0.66, 0), swing_axis=(1,0,0), swing_amplitude=42, swing_phase=0),

        # -- Right leg (thin bone) + foot --
        Part("leg_R",   ( 0.12, 0.38, 0),(0.12, 0.44, 0.12), BONE,
             pivot=( 0.12, 0.66, 0), swing_axis=(1,0,0), swing_amplitude=42, swing_phase=pi),
        Part("foot_R",  ( 0.12, 0.08, -0.04),(0.14, 0.10, 0.22), BONE_DARK,
             pivot=( 0.12, 0.66, 0), swing_axis=(1,0,0), swing_amplitude=42, swing_phase=pi),
    ]
)

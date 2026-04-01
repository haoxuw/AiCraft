"""Crewmate - the iconic Among Us astronaut.

Signature: massive egg-shaped body, large teal visor, backpack hump,
tiny stubby legs, small floating arms.

Animation design:
  lateralSwayAmount=0.12 drives the entire body left-right -- this IS the
  waddle. Legs use tiny amplitude (22°) because the body sway does most of
  the work. Arms swing freely at 35°. Pronounced vertical bounce (0.055).
"""

from math import pi
from base import Character, Part, Stats

# --- Color palette ---
SUIT        = (0.85, 0.18, 0.18, 1)   # bright red suit
SUIT_MID    = (0.68, 0.14, 0.14, 1)   # arms + legs
SUIT_DARK   = (0.52, 0.10, 0.10, 1)   # backpack, shadows
SUIT_BELLY  = (0.52, 0.10, 0.10, 1)   # underside shadow strip
BOOT        = (0.42, 0.08, 0.08, 1)   # boot soles
VISOR       = (0.38, 0.92, 0.85, 1)   # classic Among Us teal
VISOR_SHINE = (0.80, 0.98, 0.96, 1)   # specular highlight
PORT        = (0.34, 0.06, 0.06, 1)   # O2 port

crewmate = Character(
    id="base:crewmate",
    name="Crewmate",
    description="The iconic Among Us astronaut. Sus.",
    stats=Stats(strength=2, stamina=3, agility=3, intelligence=4),
    height=1.65,
    walk_cycle_speed=2.0,       # slow deliberate waddle
    walk_bob_amount=0.055,      # pronounced bounce per step
    idle_bob_amount=0.015,      # suit breathing
    parts=[
        # -- Body: egg shape (3 tapered boxes, wide base → narrow dome) --
        Part("body_low",   (0, 0.64, 0),   (0.68, 0.68, 0.52), SUIT),      # widest: belly
        Part("body_mid",   (0, 1.17, 0),   (0.54, 0.44, 0.44), SUIT),      # upper
        Part("body_top",   (0, 1.50, 0),   (0.36, 0.24, 0.32), SUIT),      # dome cap
        Part("belly_shad", (0, 0.34, 0),   (0.60, 0.10, 0.44), SUIT_BELLY),# underside depth

        # -- Visor: large teal window, upper front face --
        Part("visor",      (0, 1.08, -0.21), (0.44, 0.42, 0.04), VISOR),
        Part("visor_hi",   (-0.10, 1.24, -0.22), (0.14, 0.12, 0.02), VISOR_SHINE),

        # -- Backpack hump: defining rear silhouette --
        Part("backpack",   (0, 0.82, 0.32), (0.34, 0.52, 0.24), SUIT_DARK),
        Part("o2_port",    (0, 0.92, 0.43), (0.10, 0.10, 0.04), PORT),
        Part("pack_strap", (0, 1.08, 0.26), (0.24, 0.06, 0.12), (0.42, 0.08, 0.08, 1)),

        # -- Arms: small floating mitts --
        Part("arm_L",  (-0.46, 0.82, 0), (0.20, 0.30, 0.20), SUIT_MID,
             pivot=(-0.36, 1.02, 0), swing_axis=(1,0,0), swing_amplitude=42, swing_phase=pi),
        Part("arm_R",  ( 0.46, 0.82, 0), (0.20, 0.30, 0.20), SUIT_MID,
             pivot=( 0.36, 1.02, 0), swing_axis=(1,0,0), swing_amplitude=42, swing_phase=0),

        # -- Legs: iconic stubby stumps --
        Part("leg_L",  (-0.15, 0.15, 0),      (0.28, 0.30, 0.36), SUIT_MID,
             pivot=(-0.15, 0.32, 0), swing_axis=(1,0,0), swing_amplitude=28, swing_phase=0),
        Part("leg_R",  ( 0.15, 0.15, 0),      (0.28, 0.30, 0.36), SUIT_MID,
             pivot=( 0.15, 0.32, 0), swing_axis=(1,0,0), swing_amplitude=28, swing_phase=pi),
        Part("sole_L", (-0.15, 0.02, 0.01),   (0.30, 0.06, 0.36), BOOT,
             pivot=(-0.15, 0.32, 0), swing_axis=(1,0,0), swing_amplitude=28, swing_phase=0),
        Part("sole_R", ( 0.15, 0.02, 0.01),   (0.30, 0.06, 0.36), BOOT,
             pivot=( 0.15, 0.32, 0), swing_axis=(1,0,0), swing_amplitude=28, swing_phase=pi),
    ]
)

"""Villager — field-agent / explorer (woodcutter archetype).

Characterful adventurer look: wide-brimmed leather hat with red band,
aviator goggles, scarf, olive-drab tunic with shoulder pads, bandolier,
utility belt with pouches, backpack with bedroll, gloves, and chunky boots.

Rigging notes (model_loader.h parses literal Python dicts only — no
variables, no **unpacking, no function calls, no | unions; keep it flat).

Animation model:
  - Every head-attached part (face, nose, goggles, hat) has ``"head": True``
    so the whole head assembly rotates as one around ``head_pivot`` when
    the entity looks left/right or up/down (Minecraft-style head tracking).
  - Limbs have ``name`` fields so named clips (mine/chop/dance/wave) can
    override their swing for the duration of the clip.
  - Torso adds a subtle Y-axis counter-twist during walk (secondary motion
    — knights already had this, now villager matches).
"""
import math

model = {
    "id": "villager",
    "height": 1.85,
    "scale": 1.0,
    "hand_r":  [ 0.29,  0.58, -0.18],
    "hand_l":  [-0.29,  0.58, -0.18],
    "pivot_r": [ 0.29,  1.35,  0.00],
    "pivot_l": [-0.29,  1.35,  0.00],
    "walk_speed": 3.0,
    "idle_bob":   0.012,
    "walk_bob":   0.05,

    # Head tracking pivot — roughly the base of the neck. All parts
    # flagged ``"head": True`` rotate around this point.
    "head_pivot": [0, 1.40, 0],

    "parts": [

        # ═══════════════ HEAD & FACE (nod + head-track) ═══════════════
        # Face
        {"name": "head", "head": True,
         "offset": [0, 1.62, 0], "size": [0.40, 0.40, 0.40], "color": [0.88, 0.74, 0.60, 1],
         "pivot": [0, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 3, "phase": 0, "speed": 2},
        # Nose
        {"head": True,
         "offset": [0, 1.58, -0.21], "size": [0.08, 0.10, 0.10], "color": [0.82, 0.68, 0.54, 1],
         "pivot": [0, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 3, "phase": 0, "speed": 2},

        # Goggle strap
        {"head": True,
         "offset": [0, 1.66, 0], "size": [0.42, 0.08, 0.42], "color": [0.10, 0.07, 0.04, 1],
         "pivot": [0, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 3, "phase": 0, "speed": 2},
        # Left goggle lens
        {"head": True,
         "offset": [-0.12, 1.66, -0.195], "size": [0.13, 0.12, 0.04], "color": [0.40, 0.72, 0.85, 1],
         "pivot": [0, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 3, "phase": 0, "speed": 2},
        # Right goggle lens
        {"head": True,
         "offset": [ 0.12, 1.66, -0.195], "size": [0.13, 0.12, 0.04], "color": [0.40, 0.72, 0.85, 1],
         "pivot": [0, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 3, "phase": 0, "speed": 2},
        # Left goggle rim
        {"head": True,
         "offset": [-0.12, 1.73, -0.20], "size": [0.15, 0.02, 0.03], "color": [0.10, 0.07, 0.04, 1],
         "pivot": [0, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 3, "phase": 0, "speed": 2},
        # Right goggle rim
        {"head": True,
         "offset": [ 0.12, 1.73, -0.20], "size": [0.15, 0.02, 0.03], "color": [0.10, 0.07, 0.04, 1],
         "pivot": [0, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 3, "phase": 0, "speed": 2},

        # ═══════════════ HAT (wide-brim fedora, red band) ═══════════════
        {"head": True,
         "offset": [0, 1.85, 0], "size": [0.60, 0.05, 0.60], "color": [0.40, 0.25, 0.12, 1],
         "pivot": [0, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 3, "phase": 0, "speed": 2},
        {"head": True,
         "offset": [0, 1.89, 0], "size": [0.35, 0.04, 0.35], "color": [0.80, 0.18, 0.14, 1],
         "pivot": [0, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 3, "phase": 0, "speed": 2},
        {"head": True,
         "offset": [0, 1.98, 0], "size": [0.32, 0.18, 0.32], "color": [0.40, 0.25, 0.12, 1],
         "pivot": [0, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 3, "phase": 0, "speed": 2},
        {"head": True,
         "offset": [0, 2.08, 0], "size": [0.24, 0.04, 0.24], "color": [0.40, 0.25, 0.12, 1],
         "pivot": [0, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 3, "phase": 0, "speed": 2},

        # ═══════════════ NECK & SCARF (stays with body, not head) ═══════════════
        {"offset": [0, 1.42, 0], "size": [0.30, 0.12, 0.26], "color": [0.82, 0.18, 0.16, 1]},
        {"offset": [0, 1.38, -0.16], "size": [0.12, 0.10, 0.05], "color": [0.82, 0.18, 0.16, 1]},
        {"offset": [0.06, 1.26, -0.15], "size": [0.10, 0.20, 0.04], "color": [0.82, 0.18, 0.16, 1]},

        # ═══════════════ TORSO (Y-axis counter-twist for secondary motion) ═══════════════
        {"name": "torso",
         "offset": [0, 1.15, 0], "size": [0.46, 0.36, 0.28], "color": [0.34, 0.44, 0.28, 1],
         "pivot": [0, 1.08, 0], "swing_axis": [0, 1, 0], "amplitude": 4, "phase": math.pi, "speed": 1},
        # Lower tunic (darker)
        {"offset": [0, 0.82, 0], "size": [0.46, 0.28, 0.30], "color": [0.27, 0.36, 0.22, 1]},
        # Belt
        {"offset": [0, 0.97, 0], "size": [0.50, 0.07, 0.33], "color": [0.18, 0.12, 0.08, 1]},
        # Brass buckle
        {"offset": [0, 0.97, -0.17], "size": [0.09, 0.08, 0.02], "color": [0.88, 0.72, 0.24, 1]},
        # Belt pouches
        {"offset": [-0.19, 0.92, -0.18], "size": [0.11, 0.10, 0.06], "color": [0.32, 0.22, 0.14, 1]},
        {"offset": [ 0.19, 0.92, -0.18], "size": [0.11, 0.10, 0.06], "color": [0.32, 0.22, 0.14, 1]},
        # Bandolier
        {"offset": [-0.10, 1.16, -0.15], "size": [0.08, 0.36, 0.03], "color": [0.22, 0.16, 0.09, 1]},

        # ═══════════════ BACKPACK (static) ═══════════════
        {"offset": [0, 1.12, 0.22], "size": [0.40, 0.52, 0.16], "color": [0.32, 0.22, 0.14, 1]},
        {"offset": [0, 1.34, 0.22], "size": [0.42, 0.06, 0.17], "color": [0.22, 0.16, 0.09, 1]},
        {"offset": [0, 0.90, 0.22], "size": [0.38, 0.10, 0.16], "color": [0.22, 0.16, 0.09, 1]},
        {"offset": [-0.16, 1.18, -0.145], "size": [0.05, 0.36, 0.03], "color": [0.22, 0.16, 0.09, 1]},
        {"offset": [ 0.16, 1.18, -0.145], "size": [0.05, 0.36, 0.03], "color": [0.22, 0.16, 0.09, 1]},
        {"offset": [0, 1.42, 0.22], "size": [0.38, 0.10, 0.14], "color": [0.62, 0.50, 0.32, 1]},

        # ═══════════════ LEFT ARM (phase=pi — opposite right leg) ═══════════════
        {"name": "left_upper_arm",
         "offset": [-0.30, 1.33, 0], "size": [0.22, 0.10, 0.22], "color": [0.34, 0.44, 0.28, 1],
         "pivot": [-0.29, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
        {"name": "left_upper_arm",
         "offset": [-0.30, 1.15, 0], "size": [0.16, 0.34, 0.16], "color": [0.34, 0.44, 0.28, 1],
         "pivot": [-0.29, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
        {"name": "left_forearm",
         "offset": [-0.30, 0.84, 0], "size": [0.14, 0.28, 0.14], "color": [0.88, 0.74, 0.60, 1],
         "pivot": [-0.29, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
        {"name": "left_hand",
         "offset": [-0.30, 0.64, 0], "size": [0.17, 0.13, 0.17], "color": [0.18, 0.13, 0.08, 1],
         "pivot": [-0.29, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},

        # ═══════════════ RIGHT ARM (phase=0 — tool-holding side) ═══════════════
        {"name": "right_upper_arm",
         "offset": [ 0.30, 1.33, 0], "size": [0.22, 0.10, 0.22], "color": [0.34, 0.44, 0.28, 1],
         "pivot": [0.29, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        {"name": "right_upper_arm",
         "offset": [ 0.30, 1.15, 0], "size": [0.16, 0.34, 0.16], "color": [0.34, 0.44, 0.28, 1],
         "pivot": [0.29, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        {"name": "right_forearm",
         "offset": [ 0.30, 0.84, 0], "size": [0.14, 0.28, 0.14], "color": [0.88, 0.74, 0.60, 1],
         "pivot": [0.29, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        {"name": "right_hand",
         "offset": [ 0.30, 0.64, 0], "size": [0.17, 0.13, 0.17], "color": [0.18, 0.13, 0.08, 1],
         "pivot": [0.29, 1.35, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},

        # ═══════════════ LEFT LEG (phase=0) ═══════════════
        {"name": "left_leg",
         "offset": [-0.11, 0.36, 0], "size": [0.21, 0.48, 0.21], "color": [0.26, 0.20, 0.14, 1],
         "pivot": [-0.10, 0.61, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        {"name": "left_leg",
         "offset": [-0.11, 0.08, 0.01], "size": [0.24, 0.17, 0.26], "color": [0.12, 0.08, 0.05, 1],
         "pivot": [-0.10, 0.61, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},

        # ═══════════════ RIGHT LEG (phase=pi) ═══════════════
        {"name": "right_leg",
         "offset": [ 0.11, 0.36, 0], "size": [0.21, 0.48, 0.21], "color": [0.26, 0.20, 0.14, 1],
         "pivot": [ 0.10, 0.61, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
        {"name": "right_leg",
         "offset": [ 0.11, 0.08, 0.01], "size": [0.24, 0.17, 0.26], "color": [0.12, 0.08, 0.05, 1],
         "pivot": [ 0.10, 0.61, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
    ],

    # ═══════════════ NAMED ANIMATION CLIPS ═══════════════
    # Each clip = map of part name → override. When a clip is active, matching
    # parts replace their walk-swing params with these values for the duration
    # of the clip. Unmatched parts continue walking normally.
    # Speed is in Hz (1.0 = one full sinusoid per second), applied over
    # AnimState.time so clips animate even when the entity is standing still.
    "clips": {

        # "attack" — generic melee swing: right arm lunges forward fast.
        # Used by humanoid creatures when attacking. Held items inherit
        # the swing because they're anchored at the right_hand frame.
        "attack": {
            "right_upper_arm": {"axis": [1, 0, 0], "amp": 60, "bias": -30, "speed": 3.0, "phase": 0},
            "right_forearm":   {"axis": [1, 0, 0], "amp": 60, "bias": -30, "speed": 3.0, "phase": 0},
            "right_hand":      {"axis": [1, 0, 0], "amp": 60, "bias": -30, "speed": 3.0, "phase": 0},
            "torso":           {"axis": [0, 1, 0], "amp": 10, "speed": 3.0, "phase": 0},
        },

        # "chop" — woodcutter axe swing: right arm lifts high then chops down.
        # bias holds the arm up at shoulder height so the sinusoid oscillates
        # *around* a raised position (not the T-pose rest).
        "chop": {
            "right_upper_arm": {"axis": [1, 0, 0], "amp": 35, "bias": -70, "speed": 1.2, "phase": 0},
            "right_forearm":   {"axis": [1, 0, 0], "amp": 35, "bias": -70, "speed": 1.2, "phase": 0},
            "right_hand":      {"axis": [1, 0, 0], "amp": 35, "bias": -70, "speed": 1.2, "phase": 0},
            "torso":           {"axis": [0, 1, 0], "amp": 8,  "speed": 1.2, "phase": 0},
        },

        # "mine" — pickaxe up/down, slightly faster and more forward-biased.
        "mine": {
            "right_upper_arm": {"axis": [1, 0, 0], "amp": 40, "bias": -60, "speed": 1.4, "phase": 0},
            "right_forearm":   {"axis": [1, 0, 0], "amp": 40, "bias": -60, "speed": 1.4, "phase": 0},
            "right_hand":      {"axis": [1, 0, 0], "amp": 40, "bias": -60, "speed": 1.4, "phase": 0},
            "torso":           {"axis": [0, 1, 0], "amp": 6,  "speed": 1.4, "phase": 0},
        },

        # "wave" — friendly greeting: right arm raised, swinging side-to-side.
        "wave": {
            "right_upper_arm": {"axis": [0, 0, 1], "amp": 25, "bias": -150, "speed": 2.0, "phase": 0},
            "right_forearm":   {"axis": [0, 0, 1], "amp": 25, "bias": -150, "speed": 2.0, "phase": 0},
            "right_hand":      {"axis": [0, 0, 1], "amp": 25, "bias": -150, "speed": 2.0, "phase": 0},
        },

        # "dance" — both arms waving, torso bouncing, hip sway.
        "dance": {
            "right_upper_arm": {"axis": [0, 0, 1], "amp": 40, "bias": -100, "speed": 1.5, "phase": 0},
            "right_forearm":   {"axis": [0, 0, 1], "amp": 40, "bias": -100, "speed": 1.5, "phase": 0},
            "left_upper_arm":  {"axis": [0, 0, 1], "amp": 40, "bias":  100, "speed": 1.5, "phase": 0},
            "left_forearm":    {"axis": [0, 0, 1], "amp": 40, "bias":  100, "speed": 1.5, "phase": 0},
            "torso":           {"axis": [0, 1, 0], "amp": 15, "speed": 1.5, "phase": 0},
            "head":            {"axis": [0, 1, 0], "amp": 12, "speed": 1.5, "phase": 1.5708},
        },

        # "sleep" — rest state: arms down, no swing.
        "sleep": {
            "right_upper_arm": {"axis": [1, 0, 0], "amp": 0, "bias": 0, "speed": 0.5, "phase": 0},
            "left_upper_arm":  {"axis": [1, 0, 0], "amp": 0, "bias": 0, "speed": 0.5, "phase": 0},
            "torso":           {"axis": [1, 0, 0], "amp": 2, "bias": 0, "speed": 0.5, "phase": 0},
        },
    },
}

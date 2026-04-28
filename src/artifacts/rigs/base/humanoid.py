"""Humanoid — biped rig template (root + torso + head + 2 arms + 2 legs).

15 bones total, organized as a tree rooted at `root` (model origin = feet).
Default positions are in model-space units (1 unit = 1 block). Y is up,
+Z is forward (the player's facing). Scale is "guy"-sized (2 blocks tall);
creatures bound to this template scale via per-creature transforms.

Stock clips (idle/walk/run/attack-swing/attack-thrust/wave/sit/sleep/hurt)
are first-pass values, hand-authored to validate the keyframe pipeline.
Refine after the editor lands.

Conventions per docs/MODEL_PIPELINE.md:
  - parent: None  → root bone (no parent)
  - default_pos: local position relative to parent's joint
  - clip channel: rotation around `axis` over `keys` [{t, deg}, ...]
  - clip wraps at duration; first key (t=0) and last key (t=duration) should
    match for clean looping; non-loop clips (attack swing, hurt) accept the
    seam jump
"""

template = {
    "id": "humanoid",

    # Bones — joint at default_pos relative to parent. Children inherit
    # the parent's transform, so animating "torso" rotates the whole upper body.
    "bones": [
        {"name": "root",         "parent": None,         "default_pos": [0.0, 0.0, 0.0]},

        # Torso column: pelvis → torso → head
        {"name": "pelvis",       "parent": "root",       "default_pos": [0.0, 0.875, 0.0]},
        {"name": "torso",        "parent": "pelvis",     "default_pos": [0.0, 0.4375, 0.0]},
        {"name": "head",         "parent": "torso",      "default_pos": [0.0, 0.5625, 0.0]},

        # Left arm chain (player's left, world -X)
        {"name": "l_shoulder",   "parent": "torso",      "default_pos": [-0.4625, 0.4375, 0.0]},
        {"name": "l_arm_lower",  "parent": "l_shoulder", "default_pos": [0.0, -0.4375, 0.0]},
        {"name": "l_hand",       "parent": "l_arm_lower","default_pos": [0.0, -0.4375, 0.0]},

        # Right arm chain (player's right, world +X)
        {"name": "r_shoulder",   "parent": "torso",      "default_pos": [0.4625, 0.4375, 0.0]},
        {"name": "r_arm_lower",  "parent": "r_shoulder", "default_pos": [0.0, -0.4375, 0.0]},
        {"name": "r_hand",       "parent": "r_arm_lower","default_pos": [0.0, -0.4375, 0.0]},

        # Left leg chain
        {"name": "l_leg_upper",  "parent": "pelvis",     "default_pos": [-0.15, 0.0, 0.0]},
        {"name": "l_leg_lower",  "parent": "l_leg_upper","default_pos": [0.0, -0.4375, 0.0]},
        {"name": "l_foot",       "parent": "l_leg_lower","default_pos": [0.0, -0.4375, 0.0]},

        # Right leg chain
        {"name": "r_leg_upper",  "parent": "pelvis",     "default_pos": [0.15, 0.0, 0.0]},
        {"name": "r_leg_lower",  "parent": "r_leg_upper","default_pos": [0.0, -0.4375, 0.0]},
        {"name": "r_foot",       "parent": "r_leg_lower","default_pos": [0.0, -0.4375, 0.0]},
    ],

    # Animation clips — each channel rotates one bone around `axis` over time.
    # Time is seconds; clip loops at `duration`. Degrees, not radians.
    "clips": {

        # Subtle breathing — torso & head sway gently, arms hang
        "idle": {
            "duration": 2.0,
            "channels": [
                {"bone": "torso", "axis": [0, 0, 1],
                 "keys": [{"t": 0.0, "deg": 0}, {"t": 1.0, "deg": 1.5}, {"t": 2.0, "deg": 0}]},
                {"bone": "head", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": 0}, {"t": 1.0, "deg": -2.0}, {"t": 2.0, "deg": 0}]},
            ],
        },

        # Standard walk — opposite arm/leg swing, torso counter-rotates
        "walk": {
            "duration": 1.0,
            "channels": [
                {"bone": "l_leg_upper", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": 30}, {"t": 0.5, "deg": -30}, {"t": 1.0, "deg": 30}]},
                {"bone": "r_leg_upper", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": -30}, {"t": 0.5, "deg": 30}, {"t": 1.0, "deg": -30}]},
                {"bone": "l_shoulder", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": -25}, {"t": 0.5, "deg": 25}, {"t": 1.0, "deg": -25}]},
                {"bone": "r_shoulder", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": 25}, {"t": 0.5, "deg": -25}, {"t": 1.0, "deg": 25}]},
                {"bone": "torso", "axis": [0, 1, 0],
                 "keys": [{"t": 0.0, "deg": -5}, {"t": 0.5, "deg": 5}, {"t": 1.0, "deg": -5}]},
            ],
        },

        # Run — same pattern as walk but bigger amplitude + faster cycle
        "run": {
            "duration": 0.7,
            "channels": [
                {"bone": "l_leg_upper", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": 50}, {"t": 0.35, "deg": -50}, {"t": 0.7, "deg": 50}]},
                {"bone": "r_leg_upper", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": -50}, {"t": 0.35, "deg": 50}, {"t": 0.7, "deg": -50}]},
                {"bone": "l_shoulder", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": -45}, {"t": 0.35, "deg": 45}, {"t": 0.7, "deg": -45}]},
                {"bone": "r_shoulder", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": 45}, {"t": 0.35, "deg": -45}, {"t": 0.7, "deg": 45}]},
                {"bone": "l_arm_lower", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": -50}, {"t": 0.35, "deg": -50}, {"t": 0.7, "deg": -50}]},
                {"bone": "r_arm_lower", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": -50}, {"t": 0.35, "deg": -50}, {"t": 0.7, "deg": -50}]},
                {"bone": "torso", "axis": [0, 1, 0],
                 "keys": [{"t": 0.0, "deg": -10}, {"t": 0.35, "deg": 10}, {"t": 0.7, "deg": -10}]},
                {"bone": "torso", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": 10}, {"t": 0.7, "deg": 10}]},
            ],
        },

        # Overhead chop — right arm raises then slams down
        "attack_swing": {
            "duration": 0.4,
            "channels": [
                {"bone": "r_shoulder", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": -120}, {"t": 0.15, "deg": -160},
                          {"t": 0.3, "deg": 60}, {"t": 0.4, "deg": 30}]},
                {"bone": "r_arm_lower", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": -30}, {"t": 0.15, "deg": -45},
                          {"t": 0.3, "deg": -10}, {"t": 0.4, "deg": -20}]},
                {"bone": "torso", "axis": [0, 1, 0],
                 "keys": [{"t": 0.0, "deg": -20}, {"t": 0.3, "deg": 20}, {"t": 0.4, "deg": 0}]},
            ],
        },

        # Forward thrust — arm extends straight ahead
        "attack_thrust": {
            "duration": 0.5,
            "channels": [
                {"bone": "r_shoulder", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": 0}, {"t": 0.2, "deg": -90},
                          {"t": 0.3, "deg": -90}, {"t": 0.5, "deg": 0}]},
                {"bone": "r_arm_lower", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": -30}, {"t": 0.2, "deg": 0},
                          {"t": 0.3, "deg": 0}, {"t": 0.5, "deg": -30}]},
                {"bone": "torso", "axis": [0, 1, 0],
                 "keys": [{"t": 0.0, "deg": 0}, {"t": 0.2, "deg": 15},
                          {"t": 0.5, "deg": 0}]},
            ],
        },

        # Wave — right arm raised, hand sways side to side
        "wave": {
            "duration": 1.2,
            "channels": [
                {"bone": "r_shoulder", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": -150}, {"t": 1.2, "deg": -150}]},
                {"bone": "r_shoulder", "axis": [0, 0, 1],
                 "keys": [{"t": 0.0, "deg": -20}, {"t": 0.6, "deg": 20}, {"t": 1.2, "deg": -20}]},
                {"bone": "r_arm_lower", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": -30}, {"t": 1.2, "deg": -30}]},
            ],
        },

        # Sit — knees bent 90°, hips back, hands resting on lap
        "sit": {
            "duration": 0.5,
            "channels": [
                {"bone": "l_leg_upper", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": 90}, {"t": 0.5, "deg": 90}]},
                {"bone": "r_leg_upper", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": 90}, {"t": 0.5, "deg": 90}]},
                {"bone": "l_leg_lower", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": -90}, {"t": 0.5, "deg": -90}]},
                {"bone": "r_leg_lower", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": -90}, {"t": 0.5, "deg": -90}]},
                {"bone": "l_shoulder", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": 35}, {"t": 0.5, "deg": 35}]},
                {"bone": "r_shoulder", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": 35}, {"t": 0.5, "deg": 35}]},
                {"bone": "torso", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": -10}, {"t": 0.5, "deg": -10}]},
            ],
        },

        # Sleep — flat on back, gentle breathing
        "sleep": {
            "duration": 4.0,
            "channels": [
                {"bone": "root", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": 90}, {"t": 4.0, "deg": 90}]},
                {"bone": "torso", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": 0}, {"t": 2.0, "deg": 2}, {"t": 4.0, "deg": 0}]},
            ],
        },

        # Hurt — sharp recoil, no loop (the seam jump back to upright is OK
        # because gameplay swaps clip back to idle/walk after the hit anim)
        "hurt": {
            "duration": 0.3,
            "channels": [
                {"bone": "torso", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": 0}, {"t": 0.1, "deg": 15},
                          {"t": 0.3, "deg": 0}]},
                {"bone": "head", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": 0}, {"t": 0.1, "deg": 18},
                          {"t": 0.3, "deg": 0}]},
                {"bone": "r_shoulder", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": 0}, {"t": 0.1, "deg": 15},
                          {"t": 0.3, "deg": 0}]},
                {"bone": "l_shoulder", "axis": [1, 0, 0],
                 "keys": [{"t": 0.0, "deg": 0}, {"t": 0.1, "deg": 15},
                          {"t": 0.3, "deg": 0}]},
            ],
        },
    },
}

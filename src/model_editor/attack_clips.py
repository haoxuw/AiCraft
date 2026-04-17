"""Keyframe combo clips — mirror of src/platform/client/attack_anim.h.

These are the 3rd-person right-arm keyframes the C++ AttackAnimPlayer plays
when a weapon's attack_animations combo fires. They are NOT part of any
per-model .py (they're game-engine data shared by every humanoid holding a
weapon). ModelCrafter loads this module so the combo swings are playable
alongside the sinusoidal clips defined on each model.

Each entry:
  duration:  seconds of the full clip
  arm_keys:  sorted list of (t, pitch_deg, yaw_deg, roll_deg) — same units as
             armKeys in attack_anim.h
    pitch = forward/back rotation around X (negative = arm swings forward)
    yaw   = lateral rotation around Y    (positive = L→R sweep)
    roll  = wrist roll around the current forearm axis (positive right-hand
            rule around the shoulder→hand vector). Used to keep the blade
            edge-in for horizontal slashes instead of flat-on.

The roll axis is *dynamic* — it's the forearm direction AFTER pitch+yaw, so
the sword stays perpendicular to the arm (construction guarantees this: the
equip rotation sets a 90° rest offset, and all subsequent rotations preserve
angles). Roll only re-orients which edge faces forward; it cannot align the
blade with the arm.

If attack_anim.h drifts, sync by eye — there's no codegen.
"""

# Backward-compat: any entry with a 3-tuple (t, pitch, yaw) gets roll=0.

ATTACK_CLIPS = {
    # Horizontal slashes — roll sweeps from +90 (edge up at windup) through
    # 0 (edge forward, slicing at peak) to -30 (follow-through), so the
    # blade carves a vertical plane across the target instead of lying flat.
    # Wrist roll chosen to keep the blade in the vertical plane during the
    # lateral sweep (|Y|-maximising around the forearm axis at each keyframe,
    # see src/model_editor/modelcrafter search). Blade-down at the peak reads as a
    # natural low-guard slash instead of the broom-held-flat look we had
    # before.
    # Horizontal side cuts — pitch stays near shoulder level (~-15°) the
    # whole arc so the hand doesn't dive to the waist. Yaw does all the
    # lateral sweep work. Compare to a descending diagonal (fendente) cut
    # which would drop pitch to -50° or more; these names promise a
    # "side" swing, so keep it level.
    "swing_left":  {"duration": 0.32,
                    "arm_keys": [
                        (0.00,   0.0,   0.0,   0.0),
                        (0.15, -10.0, -90.0, -88.0),
                        (0.50, -15.0,  60.0, -56.0),
                        (1.00, -10.0,  30.0, -88.0),
                    ]},
    "swing_right": {"duration": 0.32,
                    "arm_keys": [
                        (0.00, -10.0,  30.0, -88.0),
                        (0.15, -10.0,  90.0, -88.0),
                        (0.50, -15.0, -60.0, -56.0),
                        (1.00, -10.0, -30.0, -88.0),
                    ]},
    # Cleave is a vertical strike — arm stays in the X=0 plane, so roll
    # doesn't need to re-orient anything. Leave at 0.
    "cleave":      {"duration": 0.50,
                    "arm_keys": [
                        (0.00,   0.0, 0.0, 0.0),
                        (0.25, 120.0, 0.0, 0.0),
                        (0.55, -90.0, 0.0, 0.0),
                        (1.00, -30.0, 0.0, 0.0),
                    ]},
    "jab":         {"duration": 0.22,
                    "arm_keys": [
                        (0.00,   0.0, 0.0, 0.0),
                        (0.20,  20.0, 0.0, 0.0),
                        (0.55, -80.0, 0.0, 0.0),
                        (1.00,   0.0, 0.0, 0.0),
                    ]},
    "slam":        {"duration": 0.55,
                    "arm_keys": [
                        (0.00,    0.0, 0.0, 0.0),
                        (0.20,  100.0, 0.0, 0.0),
                        (0.55,  -90.0, 0.0, 0.0),
                        (1.00,  -25.0, 0.0, 0.0),
                    ]},
    "stab":        {"duration": 0.18,
                    "arm_keys": [
                        (0.00,   0.0, 0.0, 0.0),
                        (0.25,  25.0, 0.0, 0.0),
                        (0.55, -85.0, 0.0, 0.0),
                        (1.00,   0.0, 0.0, 0.0),
                    ]},
    "swipe":       {"duration": 0.30,
                    "arm_keys": [
                        (0.00,   0.0,   0.0,   0.0),
                        (0.15, -10.0,  50.0, +60.0),
                        (0.50, -40.0, -65.0,   0.0),
                        (1.00, -10.0, -20.0, -30.0),
                    ]},
}


def _key_tuple(key):
    # Accept legacy 3-tuples (t, pitch, yaw) — roll defaults to 0.
    if len(key) == 3:
        return (key[0], key[1], key[2], 0.0)
    return key


def sample_arm(clip_name: str, t: float) -> tuple[float, float, float] | None:
    """Linear-interpolate (pitch, yaw, roll) at real time t for the clip.

    Returns None if clip_name isn't a combo clip. For t past duration,
    returns the final keyframe (the clip has ended — matches the C++
    AttackAnimPlayer which goes inactive and returns angles=0, but we
    want ModelCrafter to keep showing the last pose for debugging).
    """
    c = ATTACK_CLIPS.get(clip_name)
    if c is None:
        return None
    dur = c["duration"]
    keys = [_key_tuple(k) for k in c["arm_keys"]]
    u = max(0.0, min(1.0, t / dur)) if dur > 0 else 0.0
    if u <= keys[0][0]:
        return keys[0][1], keys[0][2], keys[0][3]
    if u >= keys[-1][0]:
        return keys[-1][1], keys[-1][2], keys[-1][3]
    for i in range(len(keys) - 1):
        t0, p0, y0, r0 = keys[i]
        t1, p1, y1, r1 = keys[i + 1]
        if t0 <= u <= t1:
            span = t1 - t0
            frac = (u - t0) / span if span > 0 else 0.0
            return (p0 + (p1 - p0) * frac,
                    y0 + (y1 - y0) * frac,
                    r0 + (r1 - r0) * frac)
    return keys[-1][1], keys[-1][2], keys[-1][3]

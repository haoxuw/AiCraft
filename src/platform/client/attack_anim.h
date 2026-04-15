#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <cmath>

namespace civcraft {

// ── ClipKey ───────────────────────────────────────────────────────────────────
// One keyframe in an attack animation. posOffset and rotEuler are DELTAS
// applied on top of the base viewmodel transform, not absolute values.
struct ClipKey {
    float     t;           // normalized time [0, 1]
    glm::vec3 posOffset;   // position delta from base item position (blocks)
    glm::vec3 rotEuler;    // rotation delta in degrees, applied as X→Y→Z
};

// ── ArmKey ────────────────────────────────────────────────────────────────────
// One keyframe for the 3rd-person right-arm joint.
// pitch: forward/back rotation in degrees (negative = arm swings forward).
// yaw  : lateral rotation in degrees (positive = arm swings left / R-to-L sweep).
// roll : wrist roll around the rest-pose forearm axis, in degrees. Used to keep
//        a held sword's edge facing the direction of motion during a lateral
//        slash (otherwise the blade lies flat mid-swing). Gated globally by
//        AttackAnimPlayer::kEnableWristRoll — flip off to disable the feature.
struct ArmKey {
    float t;
    float pitch;
    float yaw;
    float roll = 0.0f;
};

// ── AttackClip ────────────────────────────────────────────────────────────────
// One named move in a combo sequence. Python artifact fields reference clips
// by id. New clip types require C++ work; new weapon combos are purely Python.
struct AttackClip {
    std::string          id;
    float                duration;   // seconds for one full clip
    std::vector<ClipKey> keys;       // sorted by t — FPS viewmodel delta
    std::vector<ArmKey>  armKeys;    // sorted by t — 3rd-person right-arm rotation
    float                hitStart;   // fraction of duration: hit window opens
    float                hitEnd;     // fraction of duration: hit window closes
};

// ── AttackAnimPlayer ──────────────────────────────────────────────────────────
// Owns the current clip + combo state for one entity (local player).
//
// Usage:
//   registerBuiltins();              // once at startup
//   player.setCombo({"swing_right", "swing_left", "jab"}); // from Python artifact
//   player.trigger();                // on left-click (no target required)
//   player.update(dt);               // each frame
//   player.currentDelta(pos, rot);   // feed into FPS viewmodel transform
//   player.phase()                   // feed into AnimState.attackPhase
//
class AttackAnimPlayer {
public:
    // Register all built-in named clips. Call once before first use.
    static void registerBuiltins() {
        if (!s_clips.empty()) return;  // already done

        auto reg = [](AttackClip c) { s_clips[c.id] = std::move(c); };

        // ── Default sword combo: swing_left → swing_right → cleave ────────────
        // Arm angles are intentionally large (±70–90°) so the whole shoulder
        // joint clearly participates — the hand/wrist alone can't sell a sword
        // swing, the arm has to travel.
        //
        // swing_left: horizontal side cut (combo opener).
        // Pitch stays near shoulder level (~-10°) so the arc is truly lateral
        // instead of a descending diagonal. Verified in tools/modelcrafter
        // (see tools/attack_clips.py comment block).
        reg({ "swing_left", 0.32f,
            // FPS viewmodel keyframes
            {
                {0.00f, { 0.00f,  0.00f,  0.00f}, {  0.f,   0.f,   0.f}},
                {0.15f, {-0.05f,  0.02f,  0.01f}, { 10.f, -30.f, -25.f}},  // wind-up left
                {0.50f, { 0.06f, -0.02f,  0.00f}, {-10.f,  40.f,  45.f}},  // peak slash right
                {1.00f, { 0.02f, -0.01f,  0.00f}, { -5.f,  15.f,  15.f}},
            },
            // 3rd-person right-arm keyframes (pitch = fwd/back, yaw = lateral, roll = wrist).
            // Roll values mirror tools/attack_clips.py — keep blade edge in
            // the vertical plane during the lateral sweep.
            {
                {0.00f,   0.f,   0.f,   0.f},
                {0.15f, -10.f, -90.f, -36.f},  // wind-up: arm drawn across to left
                {0.50f, -15.f,  60.f,  -5.f},  // peak: level sweep to right + forward
                {1.00f, -10.f,  30.f, -25.f},  // follow-through
            },
            0.18f, 0.65f });

        // swing_right: horizontal side cut, mirror of swing_left (combo 2nd hit).
        reg({ "swing_right", 0.32f,
            {
                {0.00f, { 0.02f, -0.01f,  0.00f}, { -5.f,  15.f,  15.f}},
                {0.15f, { 0.05f,  0.02f,  0.01f}, { 15.f,  30.f,  25.f}},  // wind-up right
                {0.50f, {-0.06f, -0.02f,  0.00f}, {-20.f, -40.f, -50.f}},  // peak slash left
                {1.00f, {-0.02f, -0.01f,  0.00f}, { -8.f, -12.f, -15.f}},
            },
            {
                {0.00f, -10.f,  30.f, -25.f},
                {0.15f, -10.f,  90.f, -36.f},  // wind-up: arm cocked back to right
                {0.50f, -15.f, -60.f,  -5.f},  // peak: level sweep to left + forward
                {1.00f, -10.f, -30.f, -25.f},
            },
            0.18f, 0.65f });

        // cleave: overhead two-handed finisher — arm winds high behind head,
        // then crashes down through vertical. Huge pitch arc (+120° → -90°)
        // sells the "whole arm + body committed" read.
        reg({ "cleave", 0.50f,
            {
                {0.00f, { 0.00f,  0.00f,  0.00f}, {  0.f,   0.f,   0.f}},
                {0.25f, { 0.00f,  0.08f,  0.03f}, {-80.f,   5.f,   0.f}},  // wind-up overhead
                {0.55f, { 0.00f, -0.05f, -0.04f}, { 95.f,  -5.f,   0.f}},  // crash down through
                {1.00f, { 0.00f, -0.02f,  0.00f}, { 25.f,   0.f,   0.f}},  // follow-through low
            },
            {
                {0.00f,    0.f,  0.f},
                {0.25f,  120.f,  0.f},  // arm raised high behind head
                {0.55f,  -90.f,  0.f},  // full downward crash
                {1.00f,  -30.f,  0.f},
            },
            0.40f, 0.75f });

        // jab: forward thrust finisher (combo 3rd hit)
        reg({ "jab", 0.22f,
            {
                {0.00f, { 0.00f,  0.00f,  0.00f}, { 0.f, 0.f,  0.f}},
                {0.20f, { 0.00f,  0.00f,  0.05f}, { 5.f, 0.f,  0.f}},  // pull back
                {0.55f, { 0.00f,  0.00f, -0.15f}, {-8.f, 0.f,  0.f}},  // thrust forward
                {1.00f, { 0.00f,  0.00f,  0.00f}, { 0.f, 0.f,  0.f}},
            },
            {
                {0.00f,   0.f,  0.f},
                {0.20f,  20.f,  0.f},  // pull arm back
                {0.55f, -80.f,  0.f},  // thrust forward
                {1.00f,   0.f,  0.f},
            },
            0.35f, 0.70f });

        // ── Other weapon archetypes ───────────────────────────────────────────
        // slam: overhead smash (axe, war-hammer)
        reg({ "slam", 0.55f,
            {
                {0.00f, { 0.00f,  0.00f,  0.00f}, {  0.f,  0.f,  0.f}},
                {0.20f, { 0.00f,  0.05f,  0.02f}, {-70.f,  0.f,  5.f}},  // raise overhead
                {0.55f, { 0.00f, -0.03f, -0.03f}, { 85.f,  0.f, -5.f}},  // smash down
                {1.00f, { 0.00f, -0.01f,  0.00f}, { 20.f,  0.f,  0.f}},
            },
            {
                {0.00f,    0.f, 0.f},
                {0.20f,  100.f, 0.f},  // raise overhead (backward)
                {0.55f,  -90.f, 0.f},  // smash down (forward)
                {1.00f,  -25.f, 0.f},
            },
            0.40f, 0.75f });

        // stab: fast dagger thrust
        reg({ "stab", 0.18f,
            {
                {0.00f, { 0.00f,  0.00f,  0.00f}, { 0.f, 0.f,  0.f}},
                {0.25f, { 0.00f,  0.00f,  0.04f}, { 3.f, 0.f,  0.f}},
                {0.55f, { 0.00f,  0.00f, -0.18f}, {-5.f, 0.f,  0.f}},
                {1.00f, { 0.00f,  0.00f,  0.00f}, { 0.f, 0.f,  0.f}},
            },
            {
                {0.00f,   0.f, 0.f},
                {0.25f,  25.f, 0.f},
                {0.55f, -85.f, 0.f},
                {1.00f,   0.f, 0.f},
            },
            0.30f, 0.65f });

        // swipe: wide arc (claws, unarmed)
        reg({ "swipe", 0.30f,
            {
                {0.00f, { 0.00f,  0.00f,  0.00f}, {  0.f,   0.f,  0.f}},
                {0.15f, { 0.03f,  0.01f,  0.01f}, { 10.f,  15.f, 15.f}},
                {0.50f, {-0.06f, -0.01f,  0.00f}, {-45.f, -20.f,-30.f}},
                {1.00f, {-0.01f,  0.00f,  0.00f}, {-10.f,  -5.f, -8.f}},
            },
            {
                {0.00f,   0.f,   0.f},
                {0.15f, -10.f,  50.f},
                {0.50f, -40.f, -65.f},
                {1.00f, -10.f, -20.f},
            },
            0.20f, 0.65f });
    }

    static const AttackClip* findClip(const std::string& id) {
        auto it = s_clips.find(id);
        return (it != s_clips.end()) ? &it->second : nullptr;
    }

    // Set the combo sequence from the Python artifact's attack_animations field.
    // Resets combo index; does NOT interrupt an active clip.
    void setCombo(const std::vector<std::string>& names) {
        m_combo = names;
        m_comboIdx = 0;
    }

    const std::vector<std::string>& combo() const { return m_combo; }

    // Combo-aware trigger. Call on left-click (entity attack path).
    // - Idle → start clip 0
    // - Active, in combo window (last 30%) → advance to next clip
    // - Active, too early → restart from clip 0
    // Returns true if a new clip started (caller plays swing sound, sets cooldown).
    bool trigger() {
        if (m_combo.empty()) return false;

        if (!m_active) {
            m_comboIdx   = 0;
            m_timer      = 0.f;
            m_active     = true;
            m_peakFired  = false;
            return true;
        }

        const AttackClip* cur = currentClip();
        if (!cur) { m_active = false; return false; }

        float windowStart = cur->duration * (1.f - kComboWindowFrac);
        if (m_timer >= windowStart) {
            // In window: chain to next clip
            m_comboIdx = (m_comboIdx + 1) % (int)m_combo.size();
        } else {
            // Too early: restart
            m_comboIdx = 0;
        }
        m_timer     = 0.f;
        m_active    = true;
        m_peakFired = false;
        return true;
    }

    // Single-clip trigger, no combo state change. Used for block-break swings.
    bool triggerOnce(const std::string& clipId) {
        const AttackClip* c = findClip(clipId);
        if (!c) return false;
        m_overrideClipId = clipId;
        m_useOverride    = true;
        m_timer          = 0.f;
        m_active         = true;
        m_peakFired      = false;
        return true;
    }

    void update(float dt) {
        if (!m_active) return;
        const AttackClip* cur = currentClip();
        if (!cur) { m_active = false; return; }

        float prev = m_timer;
        m_timer += dt;

        // Peak event: fired once per clip when time crosses the clip's peak
        // fraction (kPeakFrac of duration). Used by the FX emitter to drop
        // a shockwave at the strike moment.
        float peakT = cur->duration * kPeakFrac;
        if (!m_peakFired && prev < peakT && m_timer >= peakT) {
            m_peakPending = true;
            m_peakFired   = true;
        }

        if (m_timer >= cur->duration) {
            m_active      = false;
            m_timer       = 0.f;
            m_useOverride = false;
            m_peakFired   = false;
        }
    }

    // Returns true EXACTLY ONCE per clip, on the frame the peak is crossed.
    // Caller spawns the shockwave/impact FX.
    bool consumePeakEvent() {
        if (!m_peakPending) return false;
        m_peakPending = false;
        return true;
    }

    bool  active()  const { return m_active; }
    float phase()   const {
        if (!m_active) return 0.f;
        const AttackClip* cur = currentClip();
        return cur ? std::clamp(m_timer / cur->duration, 0.f, 1.f) : 0.f;
    }

    bool inHitWindow() const {
        if (!m_active) return false;
        const AttackClip* cur = currentClip();
        if (!cur) return false;
        float t = m_timer / cur->duration;
        return t >= cur->hitStart && t <= cur->hitEnd;
    }

    const AttackClip* currentClip() const {
        if (m_useOverride) return findClip(m_overrideClipId);
        if (m_combo.empty()) return nullptr;
        return findClip(m_combo[m_comboIdx % (int)m_combo.size()]);
    }

    // Interpolated 3rd-person right-arm angles for the current frame (degrees).
    // outPitch: forward/back (negative = arm swings forward).
    // outYaw:   lateral      (positive = right-to-left sweep).
    // outRoll:  wrist roll around forearm axis. Always 0 when
    //           kEnableWristRoll is false (Tier-0 toggle).
    void currentArmAngles(float& outPitch, float& outYaw, float& outRoll) const {
        outPitch = 0.f;
        outYaw   = 0.f;
        outRoll  = 0.f;
        if (!m_active) return;
        const AttackClip* cur = currentClip();
        if (!cur || cur->armKeys.empty()) return;

        float t = std::clamp(phase(), 0.f, 1.f);
        const auto& keys = cur->armKeys;

        auto pickFront = [&]() {
            outPitch = keys.front().pitch;
            outYaw   = keys.front().yaw;
            outRoll  = kEnableWristRoll ? keys.front().roll : 0.f;
        };
        auto pickBack = [&]() {
            outPitch = keys.back().pitch;
            outYaw   = keys.back().yaw;
            outRoll  = kEnableWristRoll ? keys.back().roll : 0.f;
        };

        if (keys.size() == 1 || t <= keys.front().t) { pickFront(); return; }
        if (t >= keys.back().t)                       { pickBack();  return; }
        for (int i = 0; i + 1 < (int)keys.size(); ++i) {
            if (t >= keys[i].t && t <= keys[i + 1].t) {
                float span = keys[i + 1].t - keys[i].t;
                float frac = (span > 0.f) ? (t - keys[i].t) / span : 0.f;
                outPitch = keys[i].pitch + (keys[i+1].pitch - keys[i].pitch) * frac;
                outYaw   = keys[i].yaw   + (keys[i+1].yaw   - keys[i].yaw)   * frac;
                if (kEnableWristRoll)
                    outRoll = keys[i].roll + (keys[i+1].roll - keys[i].roll) * frac;
                return;
            }
        }
    }

    // Backward-compat wrapper for callers that only want pitch/yaw.
    void currentArmAngles(float& outPitch, float& outYaw) const {
        float roll = 0.f;
        currentArmAngles(outPitch, outYaw, roll);
    }

    // Interpolated viewmodel delta for the current frame.
    // outPos and outRot are ADDED to the base item transform in the FPS renderer.
    void currentDelta(glm::vec3& outPos, glm::vec3& outRot) const {
        outPos = glm::vec3(0);
        outRot = glm::vec3(0);
        if (!m_active) return;
        const AttackClip* cur = currentClip();
        if (!cur || cur->keys.empty()) return;

        float t = std::clamp(phase(), 0.f, 1.f);
        const auto& keys = cur->keys;

        if (keys.size() == 1 || t <= keys.front().t) {
            outPos = keys.front().posOffset;
            outRot = keys.front().rotEuler;
            return;
        }
        if (t >= keys.back().t) {
            outPos = keys.back().posOffset;
            outRot = keys.back().rotEuler;
            return;
        }
        for (int i = 0; i + 1 < (int)keys.size(); ++i) {
            if (t >= keys[i].t && t <= keys[i + 1].t) {
                float span = keys[i + 1].t - keys[i].t;
                float frac = (span > 0.f) ? (t - keys[i].t) / span : 0.f;
                outPos = glm::mix(keys[i].posOffset, keys[i + 1].posOffset, frac);
                outRot = glm::mix(keys[i].rotEuler,  keys[i + 1].rotEuler,  frac);
                return;
            }
        }
    }

private:
    static inline std::unordered_map<std::string, AttackClip> s_clips;

    std::vector<std::string> m_combo;
    int   m_comboIdx      = 0;
    float m_timer         = 0.f;
    bool  m_active        = false;
    bool  m_useOverride   = false;
    bool  m_peakFired     = false;  // peak already latched this clip?
    bool  m_peakPending   = false;  // consumePeakEvent() will return true once
    std::string m_overrideClipId;

    // Input buffer: last N% of clip where a new trigger chains to next clip.
    static constexpr float kComboWindowFrac = 0.30f;
    // Peak fraction — all swing clips hit their apex around 0.50 of duration.
    // Keeping this a single constant (rather than per-clip) matches the
    // keyframe layout in attack_anim.h.
    static constexpr float kPeakFrac = 0.50f;
public:
    // ── Tier-0 wrist-roll toggle ──────────────────────────────────────────────
    // Master switch for the wrist-roll term added to ArmKey. When false,
    // currentArmAngles() always reports roll=0, effectively reverting to
    // the pre-Tier-0 (pitch+yaw only) behavior. Useful for A/B compares
    // and quick rollback if the in-game roll proves too aggressive.
    static constexpr bool kEnableWristRoll = true;
};

} // namespace civcraft

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <cmath>

namespace modcraft {

// ── ClipKey ───────────────────────────────────────────────────────────────────
// One keyframe in an attack animation. posOffset and rotEuler are DELTAS
// applied on top of the base viewmodel transform, not absolute values.
struct ClipKey {
    float     t;           // normalized time [0, 1]
    glm::vec3 posOffset;   // position delta from base item position (blocks)
    glm::vec3 rotEuler;    // rotation delta in degrees, applied as X→Y→Z
};

// ── AttackClip ────────────────────────────────────────────────────────────────
// One named move in a combo sequence. Python artifact fields reference clips
// by id. New clip types require C++ work; new weapon combos are purely Python.
struct AttackClip {
    std::string          id;
    float                duration;   // seconds for one full clip
    std::vector<ClipKey> keys;       // sorted by t, linearly interpolated
    float                hitStart;   // fraction of duration: hit window opens
    float                hitEnd;     // fraction of duration: hit window closes
    // 3rd-person arm lunge amplitudes (degrees). Drive AnimState.attackPhase.
    float                armLungeR = 90.0f;
    float                armLungeL = 45.0f;
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

        // ── Default sword combo ───────────────────────────────────────────────
        // swing_right: right-to-left diagonal slash (combo opener / lone hit)
        reg({ "swing_right", 0.35f, {
                {0.00f, { 0.00f,  0.00f,  0.00f}, {  0.f,   0.f,   0.f}},
                {0.10f, { 0.04f,  0.02f,  0.01f}, { 15.f,  20.f,  25.f}},  // wind-up right
                {0.45f, {-0.05f, -0.02f,  0.00f}, {-55.f, -30.f, -45.f}},  // peak slash
                {1.00f, {-0.02f, -0.01f,  0.00f}, {-20.f, -10.f, -15.f}},  // follow-through
            }, 0.15f, 0.65f, 90.f, 30.f });

        // swing_left: left-to-right return slash (combo 2nd hit)
        reg({ "swing_left", 0.28f, {
                {0.00f, {-0.02f, -0.01f,  0.00f}, {-20.f, -10.f, -15.f}},  // continuing from prior
                {0.15f, {-0.04f,  0.02f,  0.00f}, {-10.f, -25.f, -20.f}},  // wind-up left
                {0.50f, { 0.05f, -0.02f,  0.00f}, { 50.f,  25.f,  40.f}},  // peak slash right
                {1.00f, { 0.02f, -0.01f,  0.00f}, { 18.f,   8.f,  12.f}},  // follow-through
            }, 0.12f, 0.60f, 75.f, 90.f });

        // jab: forward thrust finisher (combo 3rd hit)
        reg({ "jab", 0.22f, {
                {0.00f, { 0.00f,  0.00f,  0.00f}, { 0.f, 0.f,  0.f}},
                {0.20f, { 0.00f,  0.00f,  0.05f}, { 5.f, 0.f,  0.f}},  // pull back
                {0.55f, { 0.00f,  0.00f, -0.15f}, {-8.f, 0.f,  0.f}},  // thrust forward
                {1.00f, { 0.00f,  0.00f,  0.00f}, { 0.f, 0.f,  0.f}},  // recover
            }, 0.35f, 0.70f, 80.f, 20.f });

        // ── Other weapon archetypes ───────────────────────────────────────────
        // slam: overhead smash (axe, war-hammer)
        reg({ "slam", 0.55f, {
                {0.00f, { 0.00f,  0.00f,  0.00f}, {  0.f,  0.f,  0.f}},
                {0.20f, { 0.00f,  0.05f,  0.02f}, {-70.f,  0.f,  5.f}},  // raise overhead
                {0.55f, { 0.00f, -0.03f, -0.03f}, { 85.f,  0.f, -5.f}},  // smash down
                {1.00f, { 0.00f, -0.01f,  0.00f}, { 20.f,  0.f,  0.f}},  // rebound
            }, 0.40f, 0.75f, 110.f, 50.f });

        // stab: fast dagger thrust
        reg({ "stab", 0.18f, {
                {0.00f, { 0.00f,  0.00f,  0.00f}, { 0.f, 0.f,  0.f}},
                {0.25f, { 0.00f,  0.00f,  0.04f}, { 3.f, 0.f,  0.f}},  // pull back
                {0.55f, { 0.00f,  0.00f, -0.18f}, {-5.f, 0.f,  0.f}},  // thrust
                {1.00f, { 0.00f,  0.00f,  0.00f}, { 0.f, 0.f,  0.f}},  // recover
            }, 0.30f, 0.65f, 60.f, 10.f });

        // swipe: wide arc (claws, unarmed)
        reg({ "swipe", 0.30f, {
                {0.00f, { 0.00f,  0.00f,  0.00f}, {  0.f,   0.f,  0.f}},
                {0.15f, { 0.03f,  0.01f,  0.01f}, { 10.f,  15.f, 15.f}},
                {0.50f, {-0.06f, -0.01f,  0.00f}, {-45.f, -20.f,-30.f}},
                {1.00f, {-0.01f,  0.00f,  0.00f}, {-10.f,  -5.f, -8.f}},
            }, 0.20f, 0.65f, 70.f, 60.f });
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
            m_comboIdx = 0;
            m_timer    = 0.f;
            m_active   = true;
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
        m_timer  = 0.f;
        m_active = true;
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
        return true;
    }

    void update(float dt) {
        if (!m_active) return;
        const AttackClip* cur = currentClip();
        if (!cur) { m_active = false; return; }

        m_timer += dt;
        if (m_timer >= cur->duration) {
            m_active      = false;
            m_timer       = 0.f;
            m_useOverride = false;
        }
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
    std::string m_overrideClipId;

    // Input buffer: last N% of clip where a new trigger chains to next clip.
    static constexpr float kComboWindowFrac = 0.30f;
};

} // namespace modcraft

#pragma once

// ComboViewsScenario — snapshots each attack-combo clip at 4 phases.
//
// The production attack path is keyframe-driven (AttackAnimPlayer), not the
// Python sinusoidal clip system — so character_views + --debug-clip can't
// preview swings like swing_left / swing_right / cleave. This scenario fills
// that gap: it hooks `triggerAttackClip` on the scenario callbacks, fires
// each named clip, and captures 4 phase shots (windup / pre-peak / peak /
// follow-through) per clip from a side-ish 3/4 view.
//
// Run:
//   make combo_views                       # default combo: swing_left swing_right cleave
//   make combo_views CLIPS="slam stab"     # any combo of registered clips
//
// Output: /tmp/debug_N_<clip>_p{0..3}.ppm (later converted to PNG).

#include "development/scenario.h"
#include <string>
#include <vector>

namespace civcraft {
namespace development {

class ComboViewsScenario : public IScenario {
public:
	explicit ComboViewsScenario(std::vector<std::string> clips,
	                            std::string handItem = "sword")
		: m_clips(std::move(clips))
		, m_handItem(std::move(handItem)) {}

	const char* name() const override { return "combo_views"; }

	bool tick(float dt, Entity* /*player*/, Camera& camera,
	          const ScenarioCallbacks& cb) override
	{
		m_timer += dt;

		switch (m_step) {
		case 0:
			if (m_timer < kSettle) return false;
			if (cb.setCharacterSkin)    cb.setCharacterSkin("player");
			if (cb.setCameraAimHeight)  cb.setCameraAimHeight(1.2f);
			setCameraMode(CameraMode::RPG, camera, cb);
			if (cb.setPlayerYaw)        cb.setPlayerYaw(-90.0f);
			// 3/4 side angle — puts the camera off the right shoulder so
			// the full sweep arc (back-behind-head for cleave especially)
			// is visible without being hidden by the torso.
			// 3/4 front view (character_views "three_q" preset, orbit 45°).
			// Side-only flattens yaw so swing_left and swing_right look
			// identical — 45° keeps enough pitch read for cleave while
			// exposing the mirrored horizontal sweep of the L/R swings.
			if (cb.setRPGCameraOrbit)   cb.setRPGCameraOrbit(45.0f, 15.0f, 4.5f);
			if (!m_handItem.empty()) {
				int slot = findHotbarSlot(cb, m_handItem);
				if (slot >= 0) cb.selectSlot(slot);
			}
			nextStep();
			break;

		default:
			// Per-clip state machine: trigger → wait → snap × 4 → next clip.
			// Each snap targets a real-time offset into the clip's duration.
			return tickClip(cb);
		}
		return false;
	}

private:
	static constexpr float kSettle = 1.2f;
	// Capture phase targets (fraction of clip duration). Matches the
	// keyframe layout in attack_anim.h: windup / pre-peak / peak / follow.
	static constexpr float kPhases[4] = {0.18f, 0.40f, 0.55f, 0.85f};

	std::vector<std::string> m_clips;
	std::string m_handItem;
	int    m_step     = 0;
	float  m_timer    = 0.0f;
	int    m_clipIdx  = 0;
	int    m_phaseIdx = 0;
	bool   m_fired    = false;
	float  m_triggerTime = 0.0f;

	void nextStep() { m_step++; m_timer = 0; }

	// Rough clip durations mirrored from attack_anim.h. If the registry
	// gains a public lookup later this should switch to querying that.
	static float clipDuration(const std::string& id) {
		if (id == "swing_left" || id == "swing_right") return 0.32f;
		if (id == "cleave") return 0.50f;
		if (id == "slam")   return 0.55f;
		if (id == "stab")   return 0.18f;
		if (id == "swipe")  return 0.30f;
		if (id == "jab")    return 0.22f;
		return 0.35f;
	}

	bool tickClip(const ScenarioCallbacks& cb) {
		if (m_clipIdx >= (int)m_clips.size()) return true;

		const std::string& id = m_clips[m_clipIdx];
		float dur = clipDuration(id);

		if (!m_fired) {
			if (cb.triggerAttackClip) cb.triggerAttackClip(id);
			m_fired = true;
			m_triggerTime = m_timer;
			return false;
		}

		float elapsed = m_timer - m_triggerTime;
		float target  = kPhases[m_phaseIdx] * dur;
		if (elapsed < target) return false;

		char suffix[64];
		snprintf(suffix, sizeof(suffix), "%s_p%d", id.c_str(), m_phaseIdx);
		cb.save(suffix);
		m_phaseIdx++;

		if (m_phaseIdx >= 4) {
			m_phaseIdx = 0;
			m_clipIdx++;
			m_fired = false;
			m_timer = 0;
		}
		return false;
	}
};

} // namespace development
} // namespace civcraft

#pragma once

// AnimationScenario — captures several frames of the FPS attack swing animation.
//
// Shot sequence (all in FPS):
//   swing_0_rest    — rest pose before swing starts
//   swing_1 … N     — frames at ~16ms intervals through the swing arc

#include "development/scenario.h"

namespace agentica {
namespace development {

class AnimationScenario : public IScenario {
public:
	explicit AnimationScenario(std::string itemId, int frameCount = 5)
		: m_itemId(std::move(itemId)), m_frameCount(frameCount) {}

	const char* name() const override { return "animation"; }

	bool tick(float dt, Entity* player, Camera& camera,
	          const ScenarioCallbacks& cb) override
	{
		m_timer += dt;

		switch (m_step) {
		// ── Step 0: setup ─────────────────────────────────────────────────
		case 0:
			if (m_timer < kSettle) return false;
			{
				int slot = findHotbarSlot(player, m_itemId);
				if (slot < 0) { return true; }
				cb.selectSlot(slot);
				setCameraMode(CameraMode::FirstPerson, camera, cb);
			}
			nextStep();
			break;

		// ── Step 1: rest pose ─────────────────────────────────────────────
		case 1:
			if (m_timer < 0.2f) return false;
			cb.save("swing_0_rest");
			cb.triggerSwing();
			nextStep();
			break;

		// ── Steps 2…N: progressive swing frames ───────────────────────────
		default:
			if (m_step > m_frameCount + 1) return true;
			if (m_timer < kFrame) return false;
			cb.save("swing_" + std::to_string(m_step - 1));
			nextStep();
			break;
		}
		return false;
	}

private:
	static constexpr float kSettle = 1.5f;
	static constexpr float kFrame  = 0.06f; // ~60fps capture rate

	std::string m_itemId;
	int         m_frameCount;
	int         m_step  = 0;
	float       m_timer = 0;

	void nextStep() { m_step++; m_timer = 0; }
};

} // namespace development
} // namespace agentica

#pragma once

// ItemViewsScenario — captures the target item in every camera view plus on-ground.
//
// Shot sequence:
//   fps     — FPS held-item view
//   tps     — third-person view (character + held item)
//   rpg     — Minecraft Dungeons isometric angle
//   rts     — top-down strategy view
//   ground  — item dropped and floating on the ground (TPS)

#include "development/scenario.h"

namespace modcraft {
namespace development {

class ItemViewsScenario : public IScenario {
public:
	explicit ItemViewsScenario(std::string itemId)
		: m_itemId(std::move(itemId)) {}

	const char* name() const override { return "item_views"; }

	bool tick(float dt, Entity* player, Camera& camera,
	          const ScenarioCallbacks& cb) override
	{
		m_timer += dt;

		switch (m_step) {
		// ── Step 0: wait for world + select item ──────────────────────────
		case 0:
			if (m_timer < kSettle) return false;
			{
				int slot = findHotbarSlot(player, m_itemId);
				if (slot < 0) {
					fprintf(stderr, "[ItemViewsScenario] '%s' not in hotbar\n",
					        m_itemId.c_str());
					return true; // abort
				}
				cb.selectSlot(slot);
				setCameraMode(CameraMode::FirstPerson, camera, cb);
			}
			nextStep();
			break;

		// ── Step 1: FPS screenshot ────────────────────────────────────────
		case 1:
			if (m_timer < kShot) return false;
			cb.save("fps");
			nextStep();
			break;

		// ── Step 2-3: switch to TPS + screenshot ──────────────────────────
		case 2:
			cb.cycleCamera(); // FPS → TPS
			nextStep();
			break;
		case 3:
			if (m_timer < kShot) return false;
			cb.save("tps");
			nextStep();
			break;

		// ── Step 4-5: switch to RPG + screenshot ──────────────────────────
		case 4:
			cb.cycleCamera(); // TPS → RPG
			nextStep();
			break;
		case 5:
			if (m_timer < kShot) return false;
			cb.save("rpg");
			nextStep();
			break;

		// ── Step 6-7: switch to RTS + screenshot ──────────────────────────
		case 6:
			cb.cycleCamera(); // RPG → RTS
			nextStep();
			break;
		case 7:
			if (m_timer < kShot) return false;
			cb.save("rts");
			nextStep();
			break;

		// ── Step 8: RPG camera + drop at feet ────────────────────────────
		// RPG avoids TPS wall-clipping at spawn. Minimal-velocity drop ensures
		// item lands at player's feet and stays in-frame.
		case 8:
			setCameraMode(CameraMode::RPG, camera, cb);
			cb.dropItem();
			nextStep();
			break;

		// ── Step 9: ground screenshot ─────────────────────────────────────
		case 9:
			if (m_timer < 1.5f) return false; // longer wait: physics settle + server round-trip
			cb.save("ground");
			nextStep();
			break;

		default:
			return true; // done
		}
		return false;
	}

private:
	static constexpr float kSettle = 1.5f;  // world settle time before first shot
	static constexpr float kShot   = 0.4f;  // minimum wait between mode switch + shot

	std::string m_itemId;
	int         m_step  = 0;
	float       m_timer = 0;

	void nextStep() { m_step++; m_timer = 0; }
};

} // namespace development
} // namespace modcraft

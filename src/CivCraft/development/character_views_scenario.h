#pragma once

// CharacterViewsScenario — captures a character model from 6 angles.
//
// Orbits the RPG camera around the local player while overriding the
// player's rendered model via the ``character_skin`` prop. The same prop
// path is used by multiplayer skin selection, so the scenario reuses the
// production render flow — no special rendering path.
//
// Shot sequence (all RPG mode; player yaw=0 (faces +X); RPG offset formula
// places the camera on the opposite side of the orbit yaw):
//   front      orbit 180°  (camera on +X side, looks at face of a +X-facer)
//   three_q    orbit 135°  (3/4 front — best silhouette test)
//   side       orbit  90°  (profile)
//   back       orbit   0°  (camera on -X side, looks at back)
//   top        angle  80°  (near top-down)
//   rts        RTS mode overhead
//
// Run with:
//   ./build/civcraft --skip-menu --debug-scenario character_views \
//                    --debug-character base:pig
//
// Or via make target:
//   make character_views CHARACTER=base:pig
//
// With --debug-clip <name>, the scenario instead shoots a single side view
// at 4 phases of the named animation clip (chop / mine / attack / wave …).
// Used to verify arm-swing direction without needing an AI target.
//
// NOTE: non-humanoid models (pig, cat, chicken, dog) lack hand/pivot
// annotations, so any held item will render at fallback coordinates. For
// this preview scenario we don't care — we're judging the character itself.

#include "development/scenario.h"

namespace civcraft {
namespace development {

class CharacterViewsScenario : public IScenario {
public:
	// aimHeight: where (above feet) the camera looks. Default 0.5 frames
	// small animals (pig/cat/chicken/dog ~0.5–0.9 tall) at their center.
	// For large humanoids pass ~1.2 via a future flag.
	explicit CharacterViewsScenario(std::string characterId,
	                                std::string clipName = {},
	                                float aimHeight = 0.6f,
	                                std::string handItem = {})
		: m_characterId(std::move(characterId))
		, m_clipName(std::move(clipName))
		, m_aimHeight(aimHeight)
		, m_handItem(std::move(handItem)) {}

	const char* name() const override { return "character_views"; }

	bool tick(float /*dt*/, Entity* /*player*/, Camera& camera,
	          const ScenarioCallbacks& cb) override
	{
		m_timer += 0.016f; // assume ~60fps; step cadence doesn't need real dt

		if (!m_clipName.empty()) return tickClip(camera, cb);

		switch (m_step) {
		// ── Step 0: wait for world to settle, apply skin override ──────────
		case 0:
			if (m_timer < kSettle) return false;
			if (cb.setCharacterSkin) cb.setCharacterSkin(m_characterId);
			if (cb.setCameraAimHeight) cb.setCameraAimHeight(m_aimHeight);
			setCameraMode(CameraMode::RPG, camera, cb);
			// -90° makes the model's designed-front (snout/face at model-space -Z)
			// point toward +X, so the orbit yaws below work the same for animals
			// and humanoids. See model definitions: heads/snouts sit at z<0.
			if (cb.setPlayerYaw) cb.setPlayerYaw(-90.0f);
			if (!m_handItem.empty()) {
				int slot = findHotbarSlot(cb, m_handItem);
				if (slot >= 0) cb.selectSlot(slot);
			}
			nextStep();
			break;

		// Each pair: configure camera, then shoot after brief settle.
		// Orbit yaw vs player yaw=-90° (model forward=-Z) calibrated so
		// orbit=90° puts the camera in front of the model's face.
		case 1: orbit(cb,  90.0f, 18.0f, 3.5f); nextStep(); break;
		case 2: if (m_timer < kShot) return false; cb.save("front");      nextStep(); break;

		case 3: orbit(cb,  45.0f, 22.0f, 3.5f); nextStep(); break;
		case 4: if (m_timer < kShot) return false; cb.save("three_q");    nextStep(); break;

		case 5: orbit(cb,   0.0f, 12.0f, 3.5f); nextStep(); break;
		case 6: if (m_timer < kShot) return false; cb.save("side");       nextStep(); break;

		case 7: orbit(cb, -90.0f, 18.0f, 3.5f); nextStep(); break;
		case 8: if (m_timer < kShot) return false; cb.save("back");       nextStep(); break;

		case 9:  orbit(cb,  0.0f, 80.0f, 5.0f); nextStep(); break;
		case 10: if (m_timer < kShot) return false; cb.save("top");       nextStep(); break;

		case 11: setCameraMode(CameraMode::RTS, camera, cb); nextStep(); break;
		case 12: if (m_timer < kShot) return false; cb.save("rts");       nextStep(); break;

		default:
			return true; // done
		}
		return false;
	}

private:
	static constexpr float kSettle = 1.5f;
	static constexpr float kShot   = 0.4f;

	std::string m_characterId;
	std::string m_clipName;     // empty = standard 6-angle views
	float       m_aimHeight = 0.6f;
	std::string m_handItem;     // empty = bare hands
	int         m_step  = 0;
	float       m_timer = 0;

	void nextStep() { m_step++; m_timer = 0; }

	static void orbit(const ScenarioCallbacks& cb, float yaw, float angle, float dist) {
		if (cb.setRPGCameraOrbit) cb.setRPGCameraOrbit(yaw, angle, dist);
	}

	// Clip-verification path: 4 side-view shots at phases 0, π/2, π, 3π/2
	// of the sin() inside the clip, so the full arm-swing arc is captured.
	// Angle = sin(time * speed * 2π + phase) * amp + bias. We drive time
	// directly through setPlayerAnimTime; phase-quarters are then t ∈
	// {0, 0.25, 0.5, 0.75} / speed. We don't know the per-clip speed here,
	// so pick t values that span a full revolution at speed=1: {0, 0.25,
	// 0.5, 0.75}. This is only approximate for fast clips, but covers the
	// visual question "does the arm swing forward or backward?".
	bool tickClip(Camera& camera, const ScenarioCallbacks& cb) {
		// Re-apply framing every tick so godDistance lerp + player physics
		// can't drift the camera between phase captures. "walk" is a pseudo
		// clip: it drives walkDistance + speed instead of anim.time so the
		// default distance-based walk cycle plays.
		const bool isWalk = (m_clipName == "walk");
		if (m_step > 0) {
			if (cb.setPlayerYaw) cb.setPlayerYaw(-90.0f);
			if (cb.setCameraAimHeight) cb.setCameraAimHeight(m_aimHeight);
			orbit(cb, 45.0f, 15.0f, 3.5f);
		}

		switch (m_step) {
		case 0:
			if (m_timer < kSettle) return false;
			if (cb.setCharacterSkin)   cb.setCharacterSkin(m_characterId);
			if (cb.setCameraAimHeight) cb.setCameraAimHeight(m_aimHeight);
			setCameraMode(CameraMode::RPG, camera, cb);
			if (cb.setPlayerYaw) cb.setPlayerYaw(-90.0f);
			// Side view: camera on +Z side, looking toward -Z (where the
			// model's face + swung arm live). Arm-forward is "toward camera
			// face-side" after the yaw fix-up.
			orbit(cb, 45.0f, 15.0f, 3.5f);
			if (cb.setPlayerClip) cb.setPlayerClip(isWalk ? "" : m_clipName);
			if (!m_handItem.empty()) {
				int slot = findHotbarSlot(cb, m_handItem);
				if (slot >= 0) cb.selectSlot(slot);
			}
			nextStep();
			break;

		case 1: applyPhase(cb, isWalk, 0.00f);  nextStep(); break;
		case 2: if (m_timer < kShot) return false; cb.save((m_clipName + "_p0").c_str());  nextStep(); break;

		case 3: applyPhase(cb, isWalk, 0.125f); nextStep(); break;
		case 4: if (m_timer < kShot) return false; cb.save((m_clipName + "_p1").c_str()); nextStep(); break;

		case 5: applyPhase(cb, isWalk, 0.25f);  nextStep(); break;
		case 6: if (m_timer < kShot) return false; cb.save((m_clipName + "_p2").c_str()); nextStep(); break;

		case 7: applyPhase(cb, isWalk, 0.375f); nextStep(); break;
		case 8: if (m_timer < kShot) return false; cb.save((m_clipName + "_p3").c_str()); nextStep(); break;

		default:
			return true;
		}
		return false;
	}

	// Drive anim.time for named clips; drive walkDistance+speed for "walk".
	// Phases 0.0/0.125/0.25/0.375 map walk-cycle quarter turns (walkPhase in
	// model.cpp uses walkDistance × walkCycleSpeed with default 1.0).
	static void applyPhase(const ScenarioCallbacks& cb, bool isWalk, float t) {
		if (isWalk) {
			if (cb.setPlayerWalk) cb.setPlayerWalk(t * 6.2831853f, 3.0f);
		} else {
			if (cb.setPlayerAnimTime) cb.setPlayerAnimTime(t);
		}
	}
};

} // namespace development
} // namespace civcraft

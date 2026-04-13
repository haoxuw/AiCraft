#pragma once

// IScenario — interface for debug screenshot scenarios.
//
// Each scenario defines a sequence of steps: move camera, select items,
// wait for rendering to settle, then fire a screenshot.
//
// Scenarios are zero-cost when inactive (the DebugCapture guard ensures
// tick() is never called in production builds unless the flag is set).

#include "client/camera.h"
#include "client/hotbar.h"
#include "shared/entity.h"
#include <string>
#include <functional>

namespace modcraft {
namespace development {

// Callbacks injected by the game into the scenario.
// This keeps scenarios decoupled from Game internals.
struct ScenarioCallbacks {
	// Save a screenshot to /tmp/debug_N_<suffix>.ppm
	std::function<void(const std::string& suffix)> save;
	// Cycle the camera mode (FPS → TPS → RPG → RTS → FPS)
	std::function<void()>                          cycleCamera;
	// Set the camera to a specific mode
	std::function<void(CameraMode)>                setCamera;
	// Select a hotbar slot on the player
	std::function<void(int slot)>                  selectSlot;
	// Drop the currently held item
	std::function<void()>                          dropItem;
	// Trigger the FPS swing animation (for animation scenario)
	std::function<void()>                          triggerSwing;
	// Client-only hotbar (for looking up slot numbers by item ID)
	const Hotbar*                                  hotbar = nullptr;

	// Override the local player's rendered model (character_views scenario).
	// Accepts "base:pig" or "pig"; resolves via the same character_skin prop
	// path used by the normal render flow.
	std::function<void(const std::string& skinId)> setCharacterSkin;
	// Set player body yaw in degrees (facing direction).
	std::function<void(float yawDeg)>              setPlayerYaw;
	// Set RPG camera orbit: yaw around player, angle above horizontal,
	// distance from player. All degrees/blocks.
	std::function<void(float orbitYaw, float angle, float distance)>
	                                               setRPGCameraOrbit;
	// Override the camera-aim height (feetPos + h). Normal render uses
	// eyeHeight*0.8; for small characters (animals) we want the camera
	// pointed near ground level instead.
	std::function<void(float h)>                   setCameraAimHeight;
	// Force a named animation clip on the local player model (e.g. "chop",
	// "mine", "attack"). Empty string clears. Lets character_views verify
	// mine/chop/attack arm direction without needing an AI target.
	std::function<void(const std::string& clip)>   setPlayerClip;
	// Override the animation clock the player model samples. This advances
	// the `time` argument of the sin() in clip evaluation, letting a scenario
	// sample different swing phases deterministically. Negative = reset to
	// real time.
	std::function<void(float t)>                   setPlayerAnimTime;
};

// Abstract base for all debug scenarios.
class IScenario {
public:
	virtual ~IScenario() = default;

	// Called once per frame while the scenario is running.
	// Returns true when the scenario is complete.
	virtual bool tick(float dt, Entity* player, Camera& camera,
	                  const ScenarioCallbacks& cb) = 0;

	// Human-readable name for log output.
	virtual const char* name() const = 0;

protected:
	// Helpers available to all derived scenarios.

	// Find the hotbar slot holding itemId. Returns -1 if not found.
	static int findHotbarSlot(const ScenarioCallbacks& cb, const std::string& itemId) {
		if (!cb.hotbar) return -1;
		for (int i = 0; i < Hotbar::SLOTS; i++) {
			if (cb.hotbar->get(i) == itemId) return i;
		}
		return -1;
	}

	// Force camera into a specific mode without cycling past it.
	static void setCameraMode(CameraMode target, Camera& camera,
	                          const ScenarioCallbacks& cb) {
		int guard = 4;
		while (camera.mode != target && guard-- > 0) cb.cycleCamera();
	}
};

} // namespace development
} // namespace modcraft

#pragma once

// Box model data types — pure data. Named clips in BoxModel.clips override
// per-part swing when AnimState.currentClip matches.

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace solarium {

struct BodyPart {
	std::string name;         // targetable by animation clip overrides
	std::string role;         // targeted by variant color overrides (e.g. "fur")
	glm::vec3 offset;         // center relative to model origin (feet)
	glm::vec3 halfSize;
	glm::vec4 color;          // RGBA (used when texture == 0)

	glm::vec3 pivot = {0,0,0};
	glm::vec3 swingAxis = {1,0,0};
	float swingAmplitude = 0.0f;   // degrees
	float swingPhase = 0.0f;       // radians
	float swingSpeed = 1.0f;

	bool isHead = false;           // rotates around BoxModel.headPivot via AnimState.look*

	unsigned int texture = 0;      // 0 = flat color
};

// Active clip overrides replace a part's default swing params.
struct ClipOverride {
	glm::vec3 axis = {1, 0, 0};
	float amplitude = 0.0f;      // degrees
	float phase = 0.0f;          // radians
	float bias = 0.0f;           // static angular offset in degrees
	float speed = 1.0f;
};

struct AnimClip {
	std::unordered_map<std::string, ClipOverride> overrides;  // keyed by BodyPart::name
};

struct EquipTransform {
	glm::vec3 offset = {0, 0, 0};
	glm::vec3 rotation = {0, 0, 0};    // Euler degrees (X,Y,Z)
};

struct BoxModel {
	std::vector<BodyPart> parts;
	float walkCycleSpeed = 8.0f;  // radians per meter traveled
	float idleBobSpeed = 1.5f;
	float idleBobAmount = 0.01f;
	float walkBobAmount = 0.03f;

	glm::vec3 headPivot = {0, 1.45f, 0};  // pivot for isHead parts; roughly base of neck

	std::unordered_map<std::string, AnimClip> clips;

	EquipTransform equip;  // how this item is held when equipped

	// Per-character hand attach points. Defaults: base player (height=2.0).
	glm::vec3 handR  = { 0.42f,  0.70f, -0.16f };
	glm::vec3 handL  = {-0.42f,  0.70f, -0.16f };
	glm::vec3 pivotR = { 0.37f,  1.40f,  0.00f };
	glm::vec3 pivotL = {-0.37f,  1.40f,  0.00f };
};

// nullptr model = nothing held. Model ownership is with the caller.
struct HeldItem {
	const BoxModel* model = nullptr;
	float scale = 1.0f;          // extra runtime scale (e.g. blocks shrink)
};

struct HeldItems {
	HeldItem rightHand;
	HeldItem leftHand;
};

struct AnimState {
	float walkDistance = 0.0f;  // drives walk cycle
	float speed = 0.0f;         // 0 = idle
	float time = 0.0f;
	float attackPhase = 0.0f;   // 0→1 during attack swing

	// Right-arm melee angles (degrees). Roll re-orients blade edge on lateral cuts.
	float armPitch = 0.0f;
	float armYaw   = 0.0f;
	float armRoll  = 0.0f;

	// Whole-body posture during attack swings.
	float torsoYaw     = 0.0f;
	float leftArmPitch = 0.0f;
	float leftArmYaw   = 0.0f;

	// Head tracking (radians), clamped externally (e.g. ±45° from body yaw).
	float lookYaw   = 0.0f;
	float lookPitch = 0.0f;

	std::string currentClip;  // empty = default walk cycle

	// HUD use: skip idle Y bob so inventory/hotbar items rotate in place
	// without floating up and down. Ground items leave this false.
	bool suppressIdleBob = false;
};

} // namespace solarium

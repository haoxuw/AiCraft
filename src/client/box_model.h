#pragma once

/**
 * Box model data types — shared between server and client.
 *
 * These define the geometry and animation parameters for box-based
 * character/entity models. Pure data, no OpenGL dependencies.
 *
 * The client's ModelRenderer (client/model.h) consumes these for rendering.
 * The server uses them for character definitions and collision sizing.
 *
 * Animation model:
 *   - Each BodyPart has default swing fields (amplitude, phase, axis, speed)
 *     which drive the walk cycle when the entity is moving.
 *   - Parts can be named ("name") to be targetable by animation clips.
 *   - Head parts ("head": True) rotate around the model's headPivot based on
 *     AnimState.lookYaw/lookPitch — used for Minecraft-style head tracking.
 *   - Named clips (mine/chop/dance/wave/…) live in BoxModel.clips as a map
 *     of clip name → map of part name → override. When AnimState.currentClip
 *     matches a clip and a part is in that clip's overrides, the part's
 *     swing params are REPLACED by the override for as long as the clip is
 *     active. Unoverridden parts continue to walk-swing normally.
 */

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace modcraft {

// A body part = a colored 3D box with optional animation.
struct BodyPart {
	std::string name;         // optional — targetable by animation clip overrides
	glm::vec3 offset;         // center relative to model origin (feet)
	glm::vec3 halfSize;       // half extents
	glm::vec4 color;          // RGBA (used when texture == 0)

	// Animation: limb swings around a pivot point
	glm::vec3 pivot = {0,0,0};     // rotation pivot (relative to model origin)
	glm::vec3 swingAxis = {1,0,0}; // axis to rotate around (X = walk swing)
	float swingAmplitude = 0.0f;   // max angle in degrees (0 = no animation)
	float swingPhase = 0.0f;       // phase offset in radians (PI = opposite leg)
	float swingSpeed = 1.0f;       // speed multiplier for this part

	// Head tracking: parts flagged isHead get an extra rotation applied
	// around BoxModel.headPivot using AnimState.lookYaw/lookPitch.
	bool isHead = false;

	// Optional texture ID (0 = flat color). Set by client at load time.
	unsigned int texture = 0;
};

// An override entry in a named animation clip. When active for a given part,
// these values REPLACE the part's default swing params for that part.
struct ClipOverride {
	glm::vec3 axis = {1, 0, 0};  // rotation axis
	float amplitude = 0.0f;      // degrees
	float phase = 0.0f;          // radians
	float bias = 0.0f;           // static angular offset in degrees (added to sinusoid)
	float speed = 1.0f;          // speed multiplier (over clip time)
};

// A named animation clip = a set of part overrides keyed by part name.
struct AnimClip {
	// Keyed by BodyPart::name. Parts not present here keep their default swing.
	std::unordered_map<std::string, ClipOverride> overrides;
};

// How an item should be held when equipped in a hand slot.
struct EquipTransform {
	glm::vec3 offset = {0, 0, 0};      // extra offset from hand position
	glm::vec3 rotation = {0, 0, 0};    // Euler angles in degrees (X, Y, Z) applied to item parts
	float scale = 1.0f;                 // uniform scale
};

// A model = list of body parts + animation config + named clips.
struct BoxModel {
	std::vector<BodyPart> parts;
	float totalHeight = 1.0f;
	float modelScale = 1.0f;      // uniform scale applied to all geometry
	float walkCycleSpeed = 8.0f;  // radians per meter traveled (2*PI / stride_length)
	float idleBobSpeed = 1.5f;    // gentle idle breathing speed
	float idleBobAmount = 0.01f;  // idle bob amplitude
	float walkBobAmount = 0.03f;  // vertical bounce per step

	// Head tracking pivot — rotation point for parts flagged isHead.
	// Typical value: roughly the base of the neck, e.g. [0, 1.45, 0].
	glm::vec3 headPivot = {0, 1.45f, 0};

	// Named animation clips (mine, chop, dance, wave, ...).
	// Active clip is selected per-frame via AnimState.currentClip.
	std::unordered_map<std::string, AnimClip> clips;

	// How this item looks when held in a hand (Python: "equip" dict)
	EquipTransform equip;

	// Hand attachment points — where the grip of a held item is placed.
	// Defined in the CHARACTER model's Python file ("hand_r", "hand_l", etc.)
	// so each creature can have correctly-proportioned hand positions.
	// Defaults calibrated for the base player model (arms at y=1.05, height=2.0).
	glm::vec3 handR  = { 0.42f,  0.70f, -0.16f }; // right hand grip position
	glm::vec3 handL  = {-0.42f,  0.70f, -0.16f }; // left hand grip position
	glm::vec3 pivotR = { 0.37f,  1.40f,  0.00f }; // right shoulder pivot (for arm swing)
	glm::vec3 pivotL = {-0.37f,  1.40f,  0.00f }; // left shoulder pivot
};

// Animation state passed to draw().
struct AnimState {
	float walkDistance = 0.0f;  // total distance traveled (drives walk cycle)
	float speed = 0.0f;        // current movement speed (0 = idle)
	float time = 0.0f;         // global time (for idle animation + clip phase)
	float attackPhase = 0.0f;  // 0→1 during attack swing (drives arm/limb lunge)

	// Clip-specific right-arm angles (degrees). Populated from AttackAnimPlayer.
	// pitch: forward/back rotation (negative = arm swings forward toward target).
	// yaw  : lateral rotation      (positive = right-to-left sweep, negative = L-to-R).
	float armPitch = 0.0f;
	float armYaw   = 0.0f;

	// Head tracking (radians). Head-tagged parts rotate by these around headPivot.
	// Clamped externally (e.g. ±45° from body yaw) before being passed in.
	float lookYaw   = 0.0f;
	float lookPitch = 0.0f;

	// Named animation clip currently playing. Empty = default walk cycle.
	// Parts present in BoxModel.clips[currentClip].overrides get clip params;
	// other parts keep walking normally.
	std::string currentClip;
};

} // namespace modcraft

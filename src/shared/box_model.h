#pragma once

/**
 * Box model data types — shared between server and client.
 *
 * These define the geometry and animation parameters for box-based
 * character/entity models. Pure data, no OpenGL dependencies.
 *
 * The client's ModelRenderer (client/model.h) consumes these for rendering.
 * The server uses them for character definitions and collision sizing.
 */

#include <glm/glm.hpp>
#include <vector>

namespace aicraft {

// A body part = a colored 3D box with optional animation.
struct BodyPart {
	glm::vec3 offset;         // center relative to model origin (feet)
	glm::vec3 halfSize;       // half extents
	glm::vec4 color;          // RGBA (used when texture == 0)

	// Animation: limb swings around a pivot point
	glm::vec3 pivot = {0,0,0};     // rotation pivot (relative to model origin)
	glm::vec3 swingAxis = {1,0,0}; // axis to rotate around (X = walk swing)
	float swingAmplitude = 0.0f;   // max angle in degrees (0 = no animation)
	float swingPhase = 0.0f;       // phase offset in radians (PI = opposite leg)
	float swingSpeed = 1.0f;       // speed multiplier for this part

	// Optional texture ID (0 = flat color). Set by client at load time.
	unsigned int texture = 0;
};

// A model = list of body parts + animation config.
struct BoxModel {
	std::vector<BodyPart> parts;
	float totalHeight = 1.0f;
	float modelScale = 1.0f;      // uniform scale applied to all geometry
	float walkCycleSpeed = 8.0f;  // radians per meter traveled (2*PI / stride_length)
	float idleBobSpeed = 1.5f;    // gentle idle breathing speed
	float idleBobAmount = 0.01f;  // idle bob amplitude
	float walkBobAmount = 0.03f;  // vertical bounce per step
};

// Animation state passed to draw().
struct AnimState {
	float walkDistance = 0.0f;  // total distance traveled (drives walk cycle)
	float speed = 0.0f;        // current movement speed (0 = idle)
	float time = 0.0f;         // global time (for idle animation)
};

} // namespace aicraft

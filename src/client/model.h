#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include "client/shader.h"

namespace aicraft {

// A body part = a colored 3D box with optional animation.
struct BodyPart {
	glm::vec3 offset;         // center relative to model origin (feet)
	glm::vec3 halfSize;       // half extents
	glm::vec4 color;          // RGBA

	// Animation: limb swings around a pivot point
	glm::vec3 pivot = {0,0,0};     // rotation pivot (relative to model origin)
	glm::vec3 swingAxis = {1,0,0}; // axis to rotate around (X = walk swing)
	float swingAmplitude = 0.0f;   // max angle in degrees (0 = no animation)
	float swingPhase = 0.0f;       // phase offset in radians (PI = opposite leg)
	float swingSpeed = 1.0f;       // speed multiplier for this part
};

// A model = list of body parts + animation config.
struct BoxModel {
	std::vector<BodyPart> parts;
	float totalHeight = 1.0f;
	float modelScale = 1.0f;      // uniform scale applied to all geometry (pivot, offset, halfSize)
	float walkCycleSpeed = 8.0f;  // radians per meter traveled (2*PI / stride_length)
	float idleBobSpeed = 1.5f;    // gentle idle breathing speed
	float idleBobAmount = 0.01f;  // idle bob amplitude
	float walkBobAmount = 0.03f;  // vertical bounce per step
	float lateralSwayAmount = 0.0f; // side-to-side body sway (Among Us-style weight shift)
};

// Animation state passed to draw().
struct AnimState {
	float walkDistance = 0.0f;  // total distance traveled (drives walk cycle)
	float speed = 0.0f;        // current movement speed (0 = idle)
	float time = 0.0f;         // global time (for idle animation)
};

// Renders box-based models with animation.
class ModelRenderer {
public:
	bool init(Shader* highlightShader);
	void shutdown();

	void draw(const BoxModel& model, const glm::mat4& viewProj,
	          glm::vec3 feetPos, float yaw, const AnimState& anim = {},
	          glm::vec3 sunDir = glm::normalize(glm::vec3(0.5f, 0.85f, 0.3f)));

private:
	Shader* m_shader = nullptr;
	GLuint m_cubeVAO = 0, m_cubeVBO = 0, m_cubeEBO = 0;
};

} // namespace aicraft

#include "client/entity_drawer.h"

#include <glm/glm.hpp>
#include <stdexcept>
#include <string>

namespace modcraft {

// Goal-keyword → animation-clip map. Modders can extend behaviors with
// new goalText keywords; matching clip names must exist in the model's
// `clips` table for the pose to play.
static const char* pickClip(const std::string& goal) {
	if (goal.find("Chopping")   != std::string::npos) return "chop";
	if (goal.find("Mining")     != std::string::npos) return "mine";
	if (goal.find("Sleeping")   != std::string::npos) return "sleep";
	if (goal.find("Depositing") != std::string::npos) return "wave";
	if (goal.find("Dancing")    != std::string::npos) return "dance";
	return "";
}

EntityDrawer::EntityDrawer(ModelRenderer& mr) : m_mr(mr) {}

void EntityDrawer::draw(const Entity& e, const BoxModel& model,
                        const glm::mat4& viewProj, float globalTime,
                        float damageFlashTint) {
	if (e.goalText.empty()) {
		throw std::runtime_error(
			"EntityDrawer::draw: entity " + std::to_string(e.id()) +
			" (" + e.typeId() + ") has empty goalText — "
			"animation clip selection requires a non-empty goal");
	}

	float mobSpeed = glm::length(glm::vec2(e.velocity.x, e.velocity.z));
	float mobDist  = e.getProp<float>(Prop::WalkDistance, 0.0f);

	AnimState anim = {};
	anim.walkDistance = mobDist;
	anim.speed        = mobSpeed;
	anim.time         = globalTime;
	anim.currentClip  = pickClip(e.goalText);

	// Remote head tracking — split lookYaw into head (±45°) + body overflow.
	// Non-living entities don't track the head; body yaw is just e.yaw.
	float bodyYaw = e.yaw;
	if (e.def().isLiving()) {
		constexpr float kHeadYawMax = 45.0f;
		float rel = e.lookYaw - e.yaw;
		while (rel >  180.0f) rel -= 360.0f;
		while (rel < -180.0f) rel += 360.0f;
		float headDeg = glm::clamp(rel, -kHeadYawMax, kHeadYawMax);
		bodyYaw       = e.yaw + (rel - headDeg);
		anim.lookYaw   = glm::radians(-headDeg);
		anim.lookPitch = glm::radians(glm::clamp(e.lookPitch, -45.0f, 45.0f));
	}

	m_mr.draw(model, viewProj, e.position, bodyYaw, anim, damageFlashTint);
}

} // namespace modcraft

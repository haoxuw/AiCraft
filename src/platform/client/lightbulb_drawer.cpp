#include "client/lightbulb_drawer.h"
#include "client/box_model.h"

#include <stdexcept>
#include <string>
#include <cmath>

namespace modcraft {

namespace {
constexpr glm::vec4 kHealthyTint = {1.0f, 0.92f, 0.3f, 0.9f};
constexpr glm::vec4 kErrorTint   = {1.0f, 0.2f,  0.2f, 0.9f};

// charW at scale=1 is 0.018 NDC. Budget 0.5 NDC ≈ 27 chars at full size;
// longer strings scale down proportionally, floored at kMinScale.
constexpr float kCharWidthAtScale1 = 0.018f;
constexpr float kMaxWidthNdc       = 0.5f;
constexpr float kMinScale          = 0.45f;

const BoxModel& prototype() {
	static const BoxModel k = []() {
		BoxModel m; m.totalHeight = 0.4f;
		auto mk = [](glm::vec3 off, glm::vec3 half, glm::vec4 col) {
			BodyPart p; p.offset = off; p.halfSize = half; p.color = col; return p;
		};
		m.parts.push_back(mk({0,0.15f,0},{0.08f,0.10f,0.08f}, kHealthyTint));
		m.parts.push_back(mk({0,0.27f,0},{0.05f,0.04f,0.05f},{1.0f,1.0f,0.7f,0.95f}));
		m.parts.push_back(mk({0,0.04f,0},{0.06f,0.05f,0.06f},{0.5f,0.5f,0.5f,0.9f}));
		return m;
	}();
	return k;
}
} // namespace

LightbulbDrawer::LightbulbDrawer(ModelRenderer& mr, TextRenderer& tr)
    : m_mr(mr), m_tr(tr) {}

void LightbulbDrawer::draw(const Entity& e, const glm::mat4& viewProj,
                           float globalTime, float cameraLookYaw, float aspect) {
	if (e.goalText.empty()) {
		throw std::runtime_error(
			"LightbulbDrawer::draw: entity " + std::to_string(e.id()) +
			" (" + e.typeId() + ") has empty goalText — "
			"every NPC must expose a goal before its lightbulb can be drawn");
	}

	float entityTop = e.def().collision_box_max.y;
	float bobY      = std::sin(globalTime * 2.0f + e.id() * 0.7f) * 0.05f;
	glm::vec3 pos   = e.position + glm::vec3(0, entityTop + 0.3f + bobY, 0);
	glm::vec4 tint  = e.hasError ? kErrorTint : kHealthyTint;

	BoxModel model = prototype();
	if (e.hasError) for (auto& p : model.parts) p.color = tint;
	m_mr.draw(model, viewProj, pos, cameraLookYaw, {});

	glm::vec4 clip = viewProj * glm::vec4(pos, 1.0f);
	if (clip.w <= 0.01f) return; // behind camera
	float ndcX = clip.x / clip.w;
	float ndcY = clip.y / clip.w;
	if (ndcX < -1.2f || ndcX > 1.2f || ndcY < -1.2f || ndcY > 1.2f) return;

	float rawWidth = e.goalText.size() * kCharWidthAtScale1;
	float scale    = rawWidth > kMaxWidthNdc
	                     ? std::max(kMinScale, kMaxWidthNdc / rawWidth)
	                     : 1.0f;
	float textX = ndcX - rawWidth * scale * 0.5f; // center above the bulb
	float textY = ndcY + 0.06f;
	m_tr.drawText(e.goalText, textX, textY, scale, tint, aspect);
}

} // namespace modcraft

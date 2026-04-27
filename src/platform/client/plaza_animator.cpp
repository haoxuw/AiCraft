#include "client/plaza_animator.h"

#include "client/menu_plaza.h"

#include <cmath>

namespace solarium::vk {

namespace {

// Spawn schedule. Index = stage. nullptr mascot id ⇒ "tree-only" stage
// (the renderer reads stage() and conditionally emits its tree boxes; we
// just push a Puff at the tree's location so the cloud animates).
struct Spawn {
	glm::vec3   pos;
	const char* mascot;
	float       yaw;
};
constexpr Spawn kSpawns[PlazaAnimator::kStages] = {
	{ glm::vec3( 8.0f, 1.0f,  4.0f), nullptr,    0.0f},
	{ glm::vec3(-5.0f, 1.0f,  7.0f), nullptr,    0.0f},
	{ glm::vec3(-9.0f, 1.0f,  1.0f), nullptr,    0.0f},
	{ glm::vec3( 2.5f, 1.0f,  2.5f), "dog",      0.0f},
	{ glm::vec3(-2.5f, 1.0f, -2.0f), "cat",     90.0f},
	{ glm::vec3( 0.0f, 2.5f, -3.5f), "bee",    180.0f},
};

} // namespace

void PlazaAnimator::reset(MenuPlaza* plaza) {
	m_stage = 0;
	m_puffs.clear();
	if (plaza) plaza->clearMascots();
}

void PlazaAnimator::tick(float progress, float wallTime,
                         MenuPlaza* plaza, BehaviorStore* behaviors) {
	int wantStage = (int)std::floor(progress * (float)kStages);
	if (wantStage > kStages) wantStage = kStages;

	while (m_stage < wantStage) {
		const auto& sp = kSpawns[m_stage++];
		m_puffs.push_back({sp.pos, wallTime});
		if (sp.mascot && plaza && behaviors)
			plaza->spawnMascotById(sp.mascot, sp.pos, sp.yaw, *behaviors);
	}
}

} // namespace solarium::vk

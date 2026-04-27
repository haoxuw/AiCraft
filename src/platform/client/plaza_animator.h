#pragma once

// PlazaAnimator — drives the loading-screen "stuff happening" animation.
//
// Default plaza state (Menu): two trees, no mascots. Once the user enters a
// loading screen, this class advances a 0..6 stage counter as world-gen
// progress climbs and emits one new piece of scenery + one puff cloud per
// step (3 trees → dog → cat → bee). The renderer reads stage() to decide
// which trees to draw and queries puffs() for active clouds.
//
// Owned by Game; one instance lives for the process lifetime. reset() is
// called whenever we leave Menu→Connecting (fresh attempt) or fall back to
// the title (returnToMainMenu) so each loading entry replays the show.

#include <glm/glm.hpp>

#include <vector>
#include <string>

namespace solarium { class BehaviorStore; }

namespace solarium::vk {

class MenuPlaza;

class PlazaAnimator {
public:
	struct Puff {
		glm::vec3 pos;
		float     birthTime = 0.0f;
	};

	// Six progressive stages (3 trees + 3 mascots). Renderer renders trees
	// 0/1 always; index N>=2 of the gating array means stage>=N-1 (one
	// stage per tree past the baseline).
	static constexpr int kStages = 6;

	// Reset to "title" state: stage 0, no puffs. Drops any spawned mascots
	// from the menu plaza so the next loading screen plays from zero.
	void reset(MenuPlaza* plaza);

	// Drive the animation forward. Called every frame while the loading
	// screen is up. `progress` is 0..1 (LoadingScreen::aggregateDisplay()),
	// `wallTime` is monotonic seconds (for puff age-out). Spawns new
	// mascots into `plaza` via its lazy-spawn API.
	void tick(float progress, float wallTime,
	          MenuPlaza* plaza, BehaviorStore* behaviors);

	int                       stage() const { return m_stage; }
	const std::vector<Puff>&  puffs() const { return m_puffs; }

private:
	int               m_stage = 0;
	std::vector<Puff> m_puffs;
};

} // namespace solarium::vk

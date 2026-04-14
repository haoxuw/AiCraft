// CellCraft — ambient chalk-dust motes drifting in the background.
// Purely decorative: small faint short segments that slowly drift + fade,
// respawning when they leave the screen or expire. Rendered through the
// chalk ribbon so they inherit the same aesthetic.
#pragma once

#include <vector>
#include <glm/glm.hpp>

#include "CellCraft/client/chalk_stroke.h"

namespace civcraft::cellcraft {

class ChalkRenderer;

class AmbientParticles {
public:
	void init(unsigned seed = 0xA11CEu);
	// dt in seconds; w/h in pixels.
	void update(float dt, int w, int h);
	void draw(ChalkRenderer* r, int w, int h);

private:
	struct Mote {
		glm::vec2 p;    // pixel space
		glm::vec2 v;    // pixel/s
		float     life;
		float     life_max;
		float     half_width;
		float     shade;
	};
	std::vector<Mote> motes_;
	unsigned          rng_state_ = 0;
	int               last_w_    = 1280;
	int               last_h_    = 800;

	// Reusable scratch so we don't reallocate per frame.
	std::vector<ChalkStroke> strokes_;

	float rand01_();
	void  spawn_one_(int w, int h, bool initial);
};

} // namespace civcraft::cellcraft

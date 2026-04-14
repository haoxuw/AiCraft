// LifeCraft — render the little chalk symbols overlayed on a monster
// for each Part it has. Used by the playing view and the monster select
// preview. Pure presentation — reads sim types but writes nothing.

#pragma once

#include <functional>
#include <vector>

#include <glm/glm.hpp>

#include "LifeCraft/client/chalk_stroke.h"
#include "LifeCraft/sim/part.h"

namespace civcraft::lifecraft {

// Append part glyphs to `out` as screen-space chalk strokes.
// local_to_screen turns a monster-local position into a pixel coordinate.
void appendPartStrokes(const std::vector<sim::Part>& parts,
                       const glm::vec3& body_color,
                       const std::function<glm::vec2(glm::vec2)>& local_to_screen,
                       float px_per_unit,
                       float time_seconds,
                       std::vector<ChalkStroke>& out);

} // namespace civcraft::lifecraft

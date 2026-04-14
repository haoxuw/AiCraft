// CellCraft — kid-mode name generator. 24 adjectives × 24 nouns;
// returns "Adjective Noun" picked via the supplied RNG.

#pragma once

#include <random>
#include <string>

namespace civcraft::cellcraft {

// Generate a friendly two-word creature name.
std::string generateName(std::mt19937& rng);

} // namespace civcraft::cellcraft

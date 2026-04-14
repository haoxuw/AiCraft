#include "CellCraft/client/name_generator.h"

namespace civcraft::cellcraft {

namespace {
const char* kAdjectives[24] = {
	"Bloopy", "Pointy", "Zoomy", "Pink", "Mega", "Tiny",
	"Wiggly", "Sneaky", "Bouncy", "Spiky", "Fuzzy", "Squishy",
	"Brave", "Sleepy", "Hungry", "Crunchy", "Mighty", "Goofy",
	"Sparkly", "Shouty", "Wobbly", "Jumpy", "Stinky", "Royal",
};
const char* kNouns[24] = {
	"Blob", "King", "Wizard", "Destroyer", "Champ", "Noodle",
	"Pickle", "Beast", "Chomper", "Squid", "Bug", "Goblin",
	"Knight", "Cookie", "Doodle", "Spider", "Toad", "Worm",
	"Hugger", "Boss", "Snail", "Dragon", "Muffin", "Hero",
};
} // namespace

std::string generateName(std::mt19937& rng) {
	std::uniform_int_distribution<int> ad(0, 23);
	std::uniform_int_distribution<int> nd(0, 23);
	return std::string(kAdjectives[ad(rng)]) + " " + kNouns[nd(rng)];
}

} // namespace civcraft::cellcraft

#pragma once

#include <string>
#include <vector>
#include <unordered_map>

/**
 * WorldGenConfig — runtime overrides for world generation.
 *
 * Terrain shape, village layout, and tree parameters come from
 * Python artifacts (artifacts/worlds/base/village.py).  This struct
 * holds only the things the player can tune at world-creation time
 * that are NOT in the Python config: mob counts/types and
 * per-creature behavior / starting-item overrides.
 */

namespace agentworld {

struct MobSpawn {
	std::string typeId;
	int         count;
	float       radius = -1.0f;  // < 0 = use template Python config radius
};

struct WorldGenConfig {
	// Mob overrides: when non-empty, replaces the template's mob list entirely.
	// Each entry gives the creature type, desired count, and spawn radius.
	// If empty, the template's Python config mobs are used.
	std::vector<MobSpawn> mobs = {
		{"base:pig",      4},
		{"base:chicken",  3},
		{"base:dog",      1},
		{"base:cat",      2},
		{"base:villager", 2},
	};
	float mobSpawnRadius = 30.0f;

	// Per-creature behavior overrides (typeId → behaviorId)
	std::unordered_map<std::string, std::string> behaviorOverrides;

	// Per-creature starting items (typeId → [(itemId, count), ...])
	std::unordered_map<std::string, std::vector<std::pair<std::string, int>>> startingItems;

	// Gameplay tuning
	float pickupRange = 1.5f;  // how close player must be to pick up items (blocks)
};

} // namespace agentworld

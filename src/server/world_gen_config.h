#pragma once

#include <string>
#include <vector>
#include <unordered_map>

/**
 * WorldGenConfig — all world generation parameters in one struct.
 *
 * Defaults match the original hardcoded values exactly, so existing
 * worlds generate identically. The ImGui menu exposes a subset as
 * "Advanced Options" when creating a new world.
 */

namespace agentworld {

struct MobSpawn {
	std::string typeId;
	int         count;
	float       radius = -1.0f;  // < 0 = use WorldGenConfig::mobSpawnRadius
};

struct WorldGenConfig {
	// Terrain biomes
	int   waterLevel           = -2;
	int   snowThreshold        = 18;
	int   dirtDepth            = 4;

	// Terrain noise (4 octaves)
	float terrainScale         = 1.0f;   // multiplier for all amplitudes

	// Trees
	float treeDensity          = 0.03f;  // probability per eligible block
	int   trunkHeightBase      = 7;
	int   trunkHeightVariation = 4;
	int   leafRadius           = 3;

	// Village
	int   villageClearingRadius = 28;
	int   houseHeight          = 7;      // wall height (raised from 5 for 3-block characters)
	int   houseDoorHeight      = 4;      // door opening (raised from 3)
	int   houseWindowRow       = 3;      // Y-level for windows within house
	int   houseCount           = 4;

	// Mobs — order controls spawn ring offset angle
	std::vector<MobSpawn> mobs = {
		{"base:pig",      4},
		{"base:chicken",  3},
		{"base:dog",      1},
		{"base:cat",      2},
		{"base:villager", 2},
	};
	float mobSpawnRadius       = 30.0f;

	// Per-creature behavior overrides (typeId → behaviorId)
	// "custom:xxx" means a custom Python behavior was generated.
	std::unordered_map<std::string, std::string> behaviorOverrides;

	// Per-creature starting items (typeId → [(itemId, count), ...])
	std::unordered_map<std::string, std::vector<std::pair<std::string, int>>> startingItems;
};

} // namespace agentworld

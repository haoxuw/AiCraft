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

namespace modcraft {

struct MobSpawn {
	std::string typeId;
	int         count;
	float       radius = -1.0f;  // < 0 = use template Python config radius
	std::unordered_map<std::string, std::string> props;  // extra spawn props from world config
};

struct WorldGenConfig {
	// Mob list — seeded from the Python template's mobs config.
	// The GUI menu populates this from the selected template, allowing
	// the player to adjust counts before starting. If empty at init(),
	// the server loads directly from the template.
	std::vector<MobSpawn> mobs;
	float mobSpawnRadius = 30.0f;

	// Per-creature behavior overrides (typeId → behaviorId)
	std::unordered_map<std::string, std::string> behaviorOverrides;

	// Per-creature starting items (typeId → [(itemId, count), ...])
	std::unordered_map<std::string, std::vector<std::pair<std::string, int>>> startingItems;

	// Gameplay tuning
	float pickupRange = 16.0f; // server-wide max distance any entity can pick up items (blocks)
	float storeRange  = 5.0f;  // max distance to store/take items from an entity inventory (blocks)
};

} // namespace modcraft

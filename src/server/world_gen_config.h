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
 * that are NOT in the Python config: mob counts/types, per-mob
 * spawn-anchor (where in the world each group is placed), and
 * per-creature behavior / starting-item overrides.
 */

namespace modcraft {

// Where a group of mobs is positioned at world init. The actual
// anchor coordinate is resolved by the WorldTemplate: Monument is
// the village center trident tower, Barn is the barn interior,
// Portal is the player's spawn gateway, VillageCenter is the legacy
// ring placement (back-compat for worlds without a monument/barn).
enum class SpawnAnchor {
	VillageCenter,  // legacy default — circular ring around villageCenter()
	Monument,       // ring around the village monument tower
	Barn,           // scattered inside the barn interior (home_x/z set to barn)
	Portal,         // near the player spawn gateway
};

struct MobSpawn {
	std::string typeId;
	int         count  = 1;
	float       radius = -1.0f;                     // < 0 = use anchor's default spacing
	SpawnAnchor anchor = SpawnAnchor::VillageCenter;
	std::unordered_map<std::string, std::string> props;  // extra spawn props from world config
};

struct WorldGenConfig {
	// Mob list — seeded from the Python template's mobs config.
	// The GUI menu populates this from the selected template, allowing
	// the player to adjust counts before starting. If empty at init(),
	// the server loads directly from the template.
	std::vector<MobSpawn> mobs;
	float mobSpawnRadius = 30.0f;

	// Player spawns at the portal gateway (preferredSpawn). When true,
	// the player's initial yaw is computed from the gateway→monument
	// vector so they spawn already looking at the village. Falls back
	// to the template's default yaw when there is no village/monument.
	bool  playerFacesMonument = true;

	// Per-creature behavior overrides (typeId → behaviorId)
	std::unordered_map<std::string, std::string> behaviorOverrides;

	// Per-creature starting items (typeId → [(itemId, count), ...])
	std::unordered_map<std::string, std::vector<std::pair<std::string, int>>> startingItems;

	// Gameplay tuning
	float pickupRange = 16.0f; // server-wide max distance any entity can pick up items (blocks)
	float storeRange  = 5.0f;  // max distance to store/take items from an entity inventory (blocks)
};

inline SpawnAnchor parseSpawnAnchor(const std::string& s) {
	if (s == "monument") return SpawnAnchor::Monument;
	if (s == "barn")     return SpawnAnchor::Barn;
	if (s == "portal")   return SpawnAnchor::Portal;
	return SpawnAnchor::VillageCenter;
}

} // namespace modcraft

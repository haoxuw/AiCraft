#pragma once

#include <string>
#include <vector>
#include <unordered_map>

// Runtime world-gen overrides. Terrain/village shape lives in Python artifacts;
// this struct holds what the player can tune at creation time plus per-creature
// behavior/starting-item overrides.

namespace civcraft {

// WorldTemplate resolves these to coords at init.
enum class SpawnAnchor {
	VillageCenter,  // legacy ring around villageCenter()
	Monument,       // ring around the monument tower
	Barn,           // scattered inside the barn
	Portal,         // near the player spawn gateway
};

struct MobSpawn {
	std::string typeId;
	int         count  = 1;
	float       radius = -1.0f;   // <0 = anchor default spacing
	SpawnAnchor anchor = SpawnAnchor::VillageCenter;
	float       yOffset = 0.0f;   // blocks above ground (flyers hover +3)
	std::unordered_map<std::string, std::string> props;
};

struct WorldGenConfig {
	// Seeded from Python template; menu can tune counts. Empty → server loads direct.
	std::vector<MobSpawn> mobs;
	float mobSpawnRadius = 30.0f;

	// Initial yaw = gateway→monument so player spawns facing the village.
	bool  playerFacesMonument = true;

	std::unordered_map<std::string, std::string> behaviorOverrides;
	std::unordered_map<std::string, std::vector<std::pair<std::string, int>>> startingItems;

	float pickupRange = 16.0f;  // server-wide max pickup distance (blocks)
	float storeRange  = 5.0f;   // max distance for inventory store/take
};

inline SpawnAnchor parseSpawnAnchor(const std::string& s) {
	if (s == "monument") return SpawnAnchor::Monument;
	if (s == "barn")     return SpawnAnchor::Barn;
	if (s == "portal")   return SpawnAnchor::Portal;
	return SpawnAnchor::VillageCenter;
}

} // namespace civcraft

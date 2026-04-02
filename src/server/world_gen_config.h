#pragma once

/**
 * WorldGenConfig — all world generation parameters in one struct.
 *
 * Defaults match the original hardcoded values exactly, so existing
 * worlds generate identically. The ImGui menu exposes a subset as
 * "Advanced Options" when creating a new world.
 */

namespace agentworld {

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

	// Mobs
	int   pigCount             = 4;
	int   chickenCount         = 3;
	int   dogCount             = 1;
	int   catCount             = 2;
	int   villagerCount        = 2;
	float mobSpawnRadius       = 30.0f;
};

} // namespace agentworld

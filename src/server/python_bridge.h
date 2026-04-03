#pragma once

/**
 * Python bridge — embeds CPython via pybind11 for behavior execution.
 *
 * This is the connection between C++ server simulation and Python
 * game content. All behaviors, actions, and item definitions are
 * Python code loaded and executed through this bridge.
 *
 * Lifecycle:
 *   1. PythonBridge::init() — starts interpreter, adds python/ to path
 *   2. loadBehavior(code) — compiles Python source, returns handle
 *   3. callDecide(handle, worldView) — calls decide(), returns actions
 *   4. PythonBridge::shutdown() — stops interpreter
 *
 * The bridge runs on the SERVER only. Client never executes Python
 * game logic (it only renders based on entity properties).
 */

#include "shared/entity.h"
#include "shared/action.h"
#include "server/behavior.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

namespace agentworld {

// ============================================================
// WorldPyConfig — world template parameters loaded from Python.
// Defaults here match the base game values so C++ falls back
// gracefully when Python is unavailable.
// ============================================================
struct WorldPyConfig {
	// Terrain
	std::string terrainType      = "natural";  // "flat" or "natural"
	float surfaceY               = 4.0f;       // flat world only

	float continentScale         = 0.004f;
	float continentAmplitude     = 18.0f;
	float hillScale              = 0.024f;
	float hillAmplitude          = 6.0f;
	float detailScale            = 0.09f;
	float detailAmplitude        = 2.2f;
	float microScale             = 0.26f;
	float microAmplitude         = 0.7f;
	float waterLevel             = -2.0f;
	float snowThreshold          = 22.0f;
	int   dirtDepth              = 4;

	// Trees
	float treeDensity            = 0.025f;
	int   trunkHeightMin         = 5;
	int   trunkHeightMax         = 9;
	int   leafRadius             = 3;

	// Spawn search
	float spawnSearchX           = 30.0f;
	float spawnSearchZ           = 30.0f;
	float spawnMinH              = 2.0f;
	float spawnMaxH              = 12.0f;

	// Village
	bool  hasVillage             = true;
	float villageOffsetX         = 40.0f;
	float villageOffsetZ         = 12.0f;
	int   clearingRadius         = 28;

	struct HouseLayout { int cx = 0, cz = 0, w = 8, d = 8; };
	std::vector<HouseLayout> houses = {
		{  0,   0,  9, 9},
		{ 20,  -6,  7, 8},
		{-18,   7,  8, 7},
		{  7,  22,  7, 8},
		{-14, -19,  8, 7},
	};
	std::string wallBlock        = "base:cobblestone";
	std::string roofBlock        = "base:wood";
	std::string floorBlock       = "base:cobblestone";
	std::string pathBlock        = "base:cobblestone";
	int   houseHeight            = 5;
	int   doorHeight             = 3;
	int   windowRow              = 2;

	// Flat world: chest offset from spawn (village world: chest inside main house)
	float chestOffsetX           = 5.0f;
	float chestOffsetZ           = 0.0f;

	// Mobs
	struct MobConfig { std::string type; int count = 0; float radius = 20.0f; };
	std::vector<MobConfig> mobs = {
		{"base:villager", 3, 10.0f},
		{"base:pig",      4, 22.0f},
		{"base:chicken",  3, 18.0f},
		{"base:dog",      2, 14.0f},
		{"base:cat",      2, 12.0f},
	};
};

// Load world template config from a Python artifact file.
// Returns true and fills 'out' on success.
// Returns false (out stays at C++ defaults) if Python is not
// initialized, file not found, or any parse error occurs.
bool loadWorldConfig(const std::string& filePath, WorldPyConfig& out);

using BehaviorHandle = int;

class PythonBridge {
public:
	// Start the Python interpreter. Call once at startup.
	bool init(const std::string& pythonPath = "python");

	// Stop the interpreter. Call once at shutdown.
	void shutdown();

	bool isInitialized() const { return m_initialized; }

	// Load a behavior from Python source code string.
	// Returns a handle for calling decide() later.
	// On error: returns -1, errorOut contains the traceback.
	BehaviorHandle loadBehavior(const std::string& sourceCode, std::string& errorOut);

	// Call decide() on a loaded behavior.
	// Provides a WorldView context with nearby entities and block info.
	// Returns the BehaviorAction the Python code chose.
	// On error: returns Idle, errorOut contains the traceback.
	// Block info passed to Python behaviors
	struct NearbyBlock {
		int x, y, z;
		std::string typeId;
		float distance;
	};

	BehaviorAction callDecide(BehaviorHandle handle,
	                           Entity& self,
	                           const std::vector<NearbyEntity>& nearby,
	                           const std::vector<NearbyBlock>& nearbyBlocks,
	                           float dt,
	                           std::string& goalOut,
	                           std::string& errorOut);

	// Get the source code of a loaded behavior
	std::string getSource(BehaviorHandle handle) const;

	// Unload a behavior (free Python objects)
	void unloadBehavior(BehaviorHandle handle);

private:
	bool m_initialized = false;
	int m_nextHandle = 1;

	struct LoadedBehavior {
		std::string source;
		// pybind11 objects stored as opaque pointers (impl in .cpp)
		void* moduleObj = nullptr;
		void* instanceObj = nullptr;
	};

	std::unordered_map<int, LoadedBehavior> m_behaviors;
};

// Global bridge instance (owned by server)
PythonBridge& pythonBridge();

} // namespace agentworld

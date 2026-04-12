#pragma once

/**
 * Python bridge — embeds CPython via pybind11.
 *
 * NEW ARCHITECTURE:
 *   PythonBridge owns the interpreter lifecycle (init/shutdown) and provides
 *   behavior loading. AgentClient calls decide() on a background thread with
 *   GIL management.
 *
 *   The old callDecide() that returned a single BehaviorAction has been removed.
 *   The new flow:
 *     1. loadBehavior(code) → handle (unchanged)
 *     2. AgentClient acquires GIL, calls Python decide(), gets Plan back
 *     3. AgentClient releases GIL, executes Plan steps as ActionProposals
 *
 *   Thread safety: static block query callbacks (s_blockQueryFn, s_scanBlocksFn)
 *   must be protected by a mutex since multiple agent ticks may overlap.
 *
 * Also provides world config and structure blueprint loading from Python artifacts.
 */

#include "shared/entity.h"
#include "shared/action.h"
#include "server/behavior.h"
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <unordered_map>

namespace modcraft {

// ============================================================
// WorldPyConfig — world template parameters loaded from Python.
// Defaults here match the base game values so C++ falls back
// gracefully when Python is unavailable.
// ============================================================
struct WorldPyConfig {
	// Metadata
	std::string name;
	std::string description;

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

	// Spawn portal (set false for minimal test worlds → just a spawn-point block).
	bool  hasPortal              = true;

	// Village
	bool  hasVillage             = true;
	float villageOffsetX         = 40.0f;
	float villageOffsetZ         = 12.0f;
	int   clearingRadius         = 28;

	struct HouseLayout {
		int cx = 0, cz = 0, w = 8, d = 8, stories = 1;
		std::string type;       // "" = house, "barn" = open barn (no walls, pillars only)
		std::string wallBlock;  // "" = use village default
		std::string roofBlock;  // "" = use village default
	};
	std::vector<HouseLayout> houses = {
		{  0,   0, 14, 14, 2, "",          ""},
		{ 26,  -8, 12, 10, 1, "base:wood", "base:wood"},
		{-22,   9, 10, 12, 1, "base:wood", "base:wood"},
		{  9,  28, 12, 10, 1, "",          ""},
		{-20, -26, 12, 12, 2, "",          ""},
	};
	std::string wallBlock        = "base:cobblestone";
	std::string roofBlock        = "base:wood";
	std::string floorBlock       = "base:cobblestone";
	std::string pathBlock        = "base:cobblestone";
	int   storyHeight            = 6;
	int   doorHeight             = 5;
	int   windowRow              = 2;

	// Flat world: chest offset from spawn (village world: chest inside main house)
	float chestOffsetX           = 5.0f;
	float chestOffsetZ           = 0.0f;

	// Mobs
	//   spawnAt: optional "monument" | "barn" | "portal" string from Python.
	//     Translated to WorldGenConfig::SpawnAnchor in server init. Empty
	//     string falls back to legacy circular ring around villageCenter().
	struct MobConfig {
		std::string type; int count = 0; float radius = 20.0f;
		std::string spawnAt;  // e.g. "monument", "barn", "portal", "" (village ring)
		std::unordered_map<std::string, std::string> props;
	};
	std::vector<MobConfig> mobs = {
		{"base:villager", 3, 10.0f, "monument"},
		{"base:pig",      4, 22.0f, ""},
		{"base:chicken",  3, 18.0f, ""},
		{"base:dog",      2, 14.0f, "barn"},
		{"base:cat",      2, 12.0f, "barn"},
	};
};

// Load world template config from a Python artifact file.
bool loadWorldConfig(const std::string& filePath, WorldPyConfig& out);

// Forward-declare StructureBlueprint (defined in structure_blueprint.h).
struct StructureBlueprint;

// Load a structure blueprint from a Python artifact file.
bool loadStructureBlueprint(const std::string& filePath, StructureBlueprint& out);

using BehaviorHandle = int;

// Immutable snapshot of an entity's decide()-relevant state. Built on the
// main thread before pushing to DecideWorker so the worker reads no shared
// mutable state. Inventory and props are copied here to avoid iterating
// live containers from another thread (unordered_map rehash = UB).
struct EntitySnapshot {
	EntityId    id       = ENTITY_NONE;
	std::string typeId;
	glm::vec3   position = {0, 0, 0};
	glm::vec3   velocity = {0, 0, 0};
	float       yaw      = 0;
	float       lookYaw  = 0;
	float       lookPitch= 0;
	int         hp       = 0;
	int         maxHp    = 0;
	float       walkSpeed= 0;
	bool        onGround = false;
	std::vector<std::pair<std::string, int>>       inventory;  // itemId -> count
	std::vector<std::pair<std::string, PropValue>> props;      // copy of entity props
};

class PythonBridge {
public:
	// Start the Python interpreter. Call once at startup.
	bool init(const std::string& pythonPath = "python");

	// Stop the interpreter. Call once at shutdown.
	void shutdown();

	bool isInitialized() const { return m_initialized; }

	// Load a behavior from Python source code string.
	// Returns a handle for calling decide() later.
	BehaviorHandle loadBehavior(const std::string& sourceCode, std::string& errorOut);

	// Get the source code of a loaded behavior
	std::string getSource(BehaviorHandle handle) const;

	// Unload a behavior (free Python objects)
	void unloadBehavior(BehaviorHandle handle);

	// Block query function types — set per-call, protected by mutex
	using BlockQueryFn = std::function<std::string(int, int, int)>;
	using ScanBlocksFn = std::function<std::vector<BlockSample>(const std::string&, glm::vec3, float, int)>;

	// Call decide() on a loaded behavior.
	// Returns a Plan (list of PlanSteps). Backward compatible: if the behavior
	// returns old-format (action, goal_str), it's converted to a single-step Plan.
	//
	// Thread-safe. Internally acquires the GIL, so callers must NOT hold it.
	// Typically invoked from DecideWorker's thread.
	// lastOutcome/lastGoal/lastReason describe the outcome of the previous
	// plan (event-driven decide loop). See python/local_world.py for the
	// exposed Python fields (world.last_outcome / last_goal / last_reason).
	Plan callDecide(BehaviorHandle handle,
	                const EntitySnapshot& self,
	                const std::vector<NearbyEntity>& nearby,
	                float dt, float timeOfDay,
	                std::string& goalOut,
	                std::string& errorOut,
	                BlockQueryFn blockQueryFn = nullptr,
	                ScanBlocksFn scanBlocksFn = nullptr,
	                const std::string& lastOutcome = "none",
	                const std::string& lastGoal    = "",
	                const std::string& lastReason  = "");

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

	// Cached pydantic class references for LocalWorld and SelfEntity.
	void* m_localWorldClass  = nullptr;
	void* m_selfEntityClass  = nullptr;

	// Main thread's GIL release, held between init() and shutdown() so
	// background threads (DecideWorker) can acquire the GIL on demand.
	// Stored as void* to avoid leaking pybind11 into the header.
	void* m_gilReleaser = nullptr;
};

// Global bridge instance
PythonBridge& pythonBridge();

} // namespace modcraft

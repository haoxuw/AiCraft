#pragma once

// CPython via pybind11. Owns interpreter lifecycle; loads behaviors + world
// configs + structure blueprints from Python artifacts. callDecidePlan() returns
// a Plan and is GIL-safe (callers must NOT hold the GIL).

#include "logic/entity.h"
#include "logic/action.h"
#include "agent/behavior.h"
#include "agent/outcome.h"
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <unordered_map>

namespace solarium {

// Defaults = base game so builds without Python still produce a playable world.
struct WorldPyConfig {
	std::string name;
	std::string description;

	std::string terrainType      = "natural";  // "flat" | "natural" | "voxel_earth"
	float surfaceY               = 4.0f;       // flat only

	// voxel_earth only: absolute path to a baked VEAR region file (see
	// solarium-voxel-bake). Coords inside the file are voxel cells = 1 block.
	std::string voxelEarthRegion;
	// Alternative to voxelEarthRegion: directory of independent .vtil
	// shards (one per 16×16-chunk tile). When set, the engine streams
	// chunks from the shards on demand instead of mmap-ing the monolithic
	// blocks.bin. region_lat/region_lng pin the regional ENU frame.
	std::string voxelEarthTileDir;
	int  voxelEarthRegionLat = 0;
	int  voxelEarthRegionLng = 0;
	// World-block offset added when looking up voxels. lets you shift the
	// region's (0,0,0) anywhere in Solarium world space.
	int voxelEarthOffsetX        = 0;
	int voxelEarthOffsetY        = 60;   // park ground around y=60 by default
	int voxelEarthOffsetZ        = 0;

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

	float treeDensity            = 0.025f;
	int   trunkHeightMin         = 5;
	int   trunkHeightMax         = 9;
	int   leafRadius             = 3;

	float spawnSearchX           = 30.0f;
	float spawnSearchZ           = 30.0f;
	float spawnMinH              = 2.0f;
	float spawnMaxH              = 12.0f;

	// false = minimal test world (spawn block only, no portal).
	bool  hasPortal              = true;

	bool  hasVillage             = true;
	float villageOffsetX         = 40.0f;
	float villageOffsetZ         = 12.0f;
	int   clearingRadius         = 28;

	struct HouseLayout {
		int cx = 0, cz = 0, w = 8, d = 8, stories = 1;
		std::string type;       // "" = house, "barn" = open (no walls)
		std::string wallBlock;  // "" = village default
		std::string roofBlock;
	};
	std::vector<HouseLayout> houses = {
		{  0,   0, 14, 14, 2, "",          ""},
		{ 26,  -8, 12, 10, 1, "wood", "wood"},
		{-22,   9, 10, 12, 1, "wood", "wood"},
		{  9,  28, 12, 10, 1, "",          ""},
		{-20, -26, 12, 12, 2, "",          ""},
	};
	std::string wallBlock        = "cobblestone";
	std::string roofBlock        = "wood";
	std::string floorBlock       = "cobblestone";
	std::string pathBlock        = "cobblestone";
	int   storyHeight            = 6;
	int   doorHeight             = 5;
	int   windowRow              = 2;

	// Flat: chest offset from spawn. Village: chest inside main house.
	float chestOffsetX           = 5.0f;
	float chestOffsetZ           = 0.0f;

	// (2R+1)² preload grid before admit. Post-spawn uses STREAM_R/STREAM_FAR_R.
	int   preloadRadiusChunks    = 11;

	// Real-time seconds for full day/night. Python key: day_length_ticks.
	int   dayLengthTicks         = 1200;

	// In-game day the world starts on. Feeds seasonFromDay() so a fresh
	// world can open in e.g. autumn (4) instead of Spring (0).
	int   startingDay            = 0;

	// spawnAt: "monument"|"barn"|"portal", "" = legacy ring around village center.
	struct MobConfig {
		std::string type; int count = 0; float radius = 20.0f;
		std::string spawnAt;
		float yOffset = 0.0f;  // blocks above ground (flyers +3)
		std::unordered_map<std::string, std::string> props;
	};
	std::vector<MobConfig> mobs = {
		{"villager", 3, 6.0f,  "monument"},
		{"pig",      4, 4.0f,  "barn"},
		{"chicken",  3, 4.0f,  "barn"},
		{"dog",      2, 4.0f,  "barn"},
		{"cat",      2, 4.0f,  "barn"},
	};

	// Empty = static "clear". Set via world_config `weather:` key.
	// See artifacts/worlds/base/weather/temperate.py.
	std::string weatherSchedule;
};

// Markov-chain schedule read by WeatherController each tick.
struct WeatherPyConfig {
	struct Kind {
		std::string name;           // "clear" | "rain" | "snow" | "leaves" | ...
		float       meanSeconds  = 300.0f;  // exponential mean
		float       minIntensity = 0.0f;    // uniform in [min,max]
		float       maxIntensity = 1.0f;
		// Transition weights (controller normalises, don't need to sum to 1).
		std::vector<std::pair<std::string, float>> next;
	};
	std::vector<Kind> kinds = {
		{"clear", 600.0f, 0.0f, 0.0f, {{"clear", 1.0f}}},
	};
	// Wind: XZ base + low-frequency sinusoid.
	float baseWindX      = 0.0f;
	float baseWindZ      = 0.0f;
	float windNoiseAmp   = 0.0f;
	float windNoiseScale = 30.0f;  // seconds/cycle

	// Must match a kinds[].name; falls back to kinds[0].
	std::string initialKind = "clear";
};

bool loadWorldConfig(const std::string& filePath, WorldPyConfig& out);

// Returns false on parse error; `out` unchanged on failure.
bool loadWeatherSchedule(const std::string& filePath, WeatherPyConfig& out);

struct StructureBlueprint;
bool loadStructureBlueprint(const std::string& filePath, StructureBlueprint& out);

using BehaviorHandle = int;

// Immutable snapshot built on main thread; DecideWorker reads no shared state.
// Inventory/props are copied to avoid cross-thread map rehash UB.
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
	float       inventoryCapacity = 0;
	bool        onGround = false;
	std::vector<std::pair<std::string, int>>       inventory;  // itemId -> count
	std::vector<std::pair<std::string, PropValue>> props;      // copy of entity props
};

class PythonBridge {
public:
	// Call once at startup/shutdown.
	bool init(const std::string& pythonPath = "python");
	void shutdown();

	bool isInitialized() const { return m_initialized; }

	BehaviorHandle loadBehavior(const std::string& sourceCode, std::string& errorOut);
	std::string getSource(BehaviorHandle handle) const;
	void unloadBehavior(BehaviorHandle handle);

	// Per-call, mutex-protected.
	using BlockQueryFn      = std::function<std::string(int, int, int)>;
	using AppearanceQueryFn = std::function<int(int, int, int)>;
	using ScanBlocksFn      = std::function<std::vector<BlockSample>(const std::string&, glm::vec3, float, int)>;
	using ScanEntitiesFn    = std::function<std::vector<NearbyEntity>(const std::string&, glm::vec3, float, int)>;
	// Same shape as blocks; hits are decorator positions (flowers, moss) by typeId.
	using ScanAnnotationsFn = std::function<std::vector<BlockSample>(const std::string&, glm::vec3, float, int)>;

	// GIL-safe (caller must NOT hold it). Old (action, goal_str) tuples are
	// converted to a single-step Plan for backward compat.
	// lastOutcome/lastGoal/lastReason describe the previous plan (event-driven
	// loop); see python/local_world.py for exposed fields. lastState is the
	// typed ExecState enum — bridge stringifies once for display and calls
	// isNavFailed() for the bool, so adding a Failed_* variant only touches
	// outcome.h. lastFailStreak is the number of consecutive Failed_* outcomes
	// (reset on Success).
	Plan callDecidePlan(BehaviorHandle handle,
	                const EntitySnapshot& self,
	                const std::vector<NearbyEntity>& nearby,
	                float dt, float timeOfDay,
	                std::string& goalOut,
	                std::string& errorOut,
	                BlockQueryFn blockQueryFn = nullptr,
	                ScanBlocksFn scanBlocksFn = nullptr,
	                ScanEntitiesFn scanEntitiesFn = nullptr,
	                ScanAnnotationsFn scanAnnotationsFn = nullptr,
	                const std::string& lastOutcome = "none",
	                const std::string& lastGoal    = "",
	                const std::string& lastReason  = "",
	                ExecState          lastState   = ExecState::Idle,
	                int                lastFailStreak = 0,
	                AppearanceQueryFn appearanceQueryFn = nullptr);

	// Signal-driven react path. Mirrors callDecidePlan but invokes
	// Behavior.react(entity, world, signal) instead of decide_plan(). Returns
	// true when react returned a plan (outPlan/goalOut populated), false
	// when react returned None (signal ignored, existing plan keeps
	// running). errorOut is non-empty on Python exception.
	// GIL-safe (caller must NOT hold it).
	bool callReact(BehaviorHandle handle,
	               const EntitySnapshot& self,
	               const std::vector<NearbyEntity>& nearby,
	               float dt, float timeOfDay,
	               const std::string& signalKind,
	               const std::vector<std::pair<std::string, std::string>>& signalPayload,
	               Plan& outPlan,
	               std::string& goalOut,
	               std::string& errorOut,
	               BlockQueryFn blockQueryFn = nullptr,
	               ScanBlocksFn scanBlocksFn = nullptr,
	               ScanEntitiesFn scanEntitiesFn = nullptr,
	               ScanAnnotationsFn scanAnnotationsFn = nullptr,
	               AppearanceQueryFn appearanceQueryFn = nullptr);

private:
	bool m_initialized = false;
	int m_nextHandle = 1;

	struct LoadedBehavior {
		std::string source;
		// pybind11 objects — opaque to avoid leaking pybind into header.
		void* moduleObj = nullptr;
		void* instanceObj = nullptr;
	};

	std::unordered_map<int, LoadedBehavior> m_behaviors;

	// Cached pydantic classes.
	void* m_localWorldClass  = nullptr;
	void* m_selfEntityClass  = nullptr;

	// Main-thread GIL release held init()→shutdown() so DecideWorker can
	// acquire the GIL on demand. Opaque to keep pybind11 out of header.
	void* m_gilReleaser = nullptr;
};

PythonBridge& pythonBridge();

} // namespace solarium

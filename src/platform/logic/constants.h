#pragma once

// Rule 2: Entity = Living + Item. LivingName/ItemName mutually exclusive.
// BlockType strings map to BlockId (uint16) via BlockRegistry — blocks are NOT entities.

#include <cmath>
#include <cstdint>

namespace civcraft {

enum class EntityKind {
	Living,    // moves, HP, inventory (players, NPCs, animals)
	Item,      // on ground or in inventory
	Structure, // multi-block; valid only while all blueprint blocks present
};

using EntityId = uint32_t;
constexpr EntityId ENTITY_NONE = 0;

namespace LivingName {
	// No "player" entry: any living with EntityDef.playable=true is a valid
	// player character. Client picks at join and the server spawns that type.
	constexpr const char* Pig          = "pig";
	constexpr const char* Chicken      = "chicken";
	constexpr const char* Dog          = "dog";
	constexpr const char* Cat          = "cat";
	constexpr const char* Villager     = "villager";
	constexpr const char* Knight       = "knight";
	constexpr const char* Mage         = "mage";
	constexpr const char* Skeleton     = "skeleton";
	constexpr const char* Giant        = "giant";
	constexpr const char* BraveChicken = "brave_chicken"; // todo: remove this, MOD from python side
	constexpr const char* Squirrel     = "squirrel";
	constexpr const char* Raccoon      = "raccoon";
	constexpr const char* Beaver       = "beaver";
	constexpr const char* Bee          = "bee";
	constexpr const char* Owl          = "owl";
}

// Orthogonal flags from Python artifacts; any subset allowed.
namespace FeatureTag {
	constexpr const char* Humanoid   = "humanoid";   // bipedal → walk anim, follow AI
	constexpr const char* Hostile    = "hostile";     // aggressive → flee/combat AI
	constexpr const char* Invincible = "invincible";
}

namespace StructureName {
	constexpr const char* Chest    = "chest";    // owns inventory
	constexpr const char* Bed      = "bed";
	constexpr const char* Tree     = "tree";
	constexpr const char* House    = "house";
	constexpr const char* Spawner  = "spawner";
	constexpr const char* Monument = "monument"; // village-center flame anchor
}

namespace ItemName {
	constexpr const char* ItemEntity   = "item_entity"; // wrapper for any ground item

	// Equipment
	constexpr const char* Jetpack      = "jetpack";
	constexpr const char* Parachute    = "parachute";

	// Tools
	constexpr const char* WoodPickaxe  = "wood_pickaxe";
	constexpr const char* StonePickaxe = "stone_pickaxe";
	constexpr const char* WoodAxe      = "wood_axe";
	constexpr const char* WoodShovel   = "wood_shovel";

	// Consumables
	constexpr const char* Apple        = "apple";
	constexpr const char* Bread        = "bread";
	constexpr const char* Egg          = "egg";
}

namespace BlockType {
	constexpr const char* Air          = "air";
	constexpr const char* Stone        = "stone";
	constexpr const char* Cobblestone  = "cobblestone";
	constexpr const char* Granite      = "granite";
	constexpr const char* Marble       = "marble";
	constexpr const char* Sandstone    = "sandstone";
	constexpr const char* Dirt         = "dirt";
	constexpr const char* Grass        = "grass";
	constexpr const char* Sand         = "sand";
	constexpr const char* Water        = "water";
	constexpr const char* Wood         = "wood";
	constexpr const char* Log          = "logs";
	// Default leaves block (summer green). SeasonalLeaves feature swaps every
	// tree's leaves between this and the seasonal variants below.
	constexpr const char* Leaves       = "leaves";
	constexpr const char* LeavesSpring = "leaves_spring";     // fresh bright green
	constexpr const char* LeavesSummer = "leaves_summer";     // deep summer green
	constexpr const char* LeavesGold   = "leaves_gold";       // early autumn
	constexpr const char* LeavesOrange = "leaves_orange";     // autumn pumpkin
	constexpr const char* LeavesRed    = "leaves_red";        // autumn maple
	constexpr const char* LeavesBare   = "leaves_bare";       // late-autumn branches
	constexpr const char* LeavesSnow   = "leaves_snow";       // winter frosted
	constexpr const char* Snow         = "snow";
	constexpr const char* TNT          = "tnt";
	constexpr const char* Wheat        = "wheat";
	constexpr const char* TallGrass    = "tall_grass";    // decoration: Bezier tuft on top of grass
	constexpr const char* Wire         = "wire";
	constexpr const char* NANDGate     = "nand_gate";
	constexpr const char* WheatSeeds   = "wheat_seeds";
	constexpr const char* Chest        = "chest";
	constexpr const char* Planks       = "planks";
	constexpr const char* Bed          = "bed";
	constexpr const char* Fence        = "fence";
	constexpr const char* Farmland     = "farmland";
	constexpr const char* Stair        = "stair";
	constexpr const char* Glass        = "glass";
	constexpr const char* Door         = "door";
	constexpr const char* DoorOpen     = "door_open";
	constexpr const char* Portal       = "portal";
	constexpr const char* ArcaneStone  = "arcane_stone";
	constexpr const char* SpawnPoint   = "spawn_point";
	constexpr const char* BeeNest      = "beenest";
}

namespace Prop {
	constexpr const char* HP           = "hp";
	constexpr const char* Hunger       = "hunger";
	constexpr const char* Age          = "age";
	constexpr const char* SelectedSlot = "selected_slot";
	constexpr const char* Owner        = "owner";
	constexpr const char* Goal         = "goal";
	constexpr const char* BehaviorId   = "behavior_id";
	constexpr const char* WanderTimer  = "wander_timer";
	constexpr const char* WanderYaw    = "wander_yaw";
	constexpr const char* WalkDistance = "walk_distance";
	constexpr const char* ItemType     = "item_type";
	constexpr const char* Count        = "count";
	constexpr const char* DespawnTime  = "despawn_time";
	constexpr const char* FuseTicks    = "fuse_ticks";
	constexpr const char* Lit          = "lit";
	constexpr const char* GrowthStage  = "growth_stage";
	constexpr const char* MaxStage     = "max_stage";
	constexpr const char* Power        = "power";
	constexpr const char* MaxPower     = "max_power";
	constexpr const char* InputA       = "input_a";
	constexpr const char* InputB       = "input_b";
	constexpr const char* Output       = "output";
}

namespace Sound {
	constexpr const char* DigStone     = "dig_stone";
	constexpr const char* DigDirt      = "dig_dirt";
	constexpr const char* DigSand      = "dig_sand";
	constexpr const char* DigWood      = "dig_wood";
	constexpr const char* DigLeaves    = "dig_leaves";
	constexpr const char* DigSnow      = "dig_snow";
	constexpr const char* StepStone    = "step_stone";
	constexpr const char* StepDirt     = "step_dirt";
	constexpr const char* StepGrass    = "step_grass";
	constexpr const char* StepSand     = "step_sand";
	constexpr const char* StepWood     = "step_wood";
	constexpr const char* StepSnow     = "step_snow";
}

constexpr int CIVCRAFT_DISCOVER_PORT = 7778;

// ── Calendar ─────────────────────────────────────────────────────────
// Four-section day — matches src/python/conditions_lib.py exactly so
// Python behaviour checks (IsMorning etc.) and C++ UI agree on boundaries.
//   Night     [0.00, 0.25)   midnight → dawn
//   Morning   [0.25, 0.50)   dawn     → noon
//   Afternoon [0.50, 0.75)   noon     → dusk
//   Evening   [0.75, 1.00)   dusk     → midnight
enum class DayPhase : uint32_t { Night = 0, Morning = 1, Afternoon = 2, Evening = 3, Count = 4 };

inline DayPhase dayPhaseFromWorldTime(float wt) {
	float f = wt - std::floor(wt);
	if (f < 0.25f) return DayPhase::Night;
	if (f < 0.50f) return DayPhase::Morning;
	if (f < 0.75f) return DayPhase::Afternoon;
	return DayPhase::Evening;
}

inline const char* dayPhaseName(DayPhase p) {
	switch (p) {
		case DayPhase::Night:     return "Night";
		case DayPhase::Morning:   return "Morning";
		case DayPhase::Afternoon: return "Afternoon";
		case DayPhase::Evening:   return "Evening";
		default:                  return "?";
	}
}

// Four seasons, cycle on a short rotation so a single play session crosses
// all of them. 2 in-game days per season × 4 seasons = 8-day year ≈ 160
// real-minutes at the default 20-min day. Tune via kDaysPerSeason.
enum class Season : uint32_t { Spring = 0, Summer = 1, Autumn = 2, Winter = 3, Count = 4 };
constexpr uint32_t kDaysPerSeason = 2;

inline Season seasonFromDay(uint32_t dayCount) {
	return (Season)((dayCount / kDaysPerSeason) % (uint32_t)Season::Count);
}

// Continuous 0..4 float: integer part = current season index, fractional
// part = progress through the current season. Shader lerps between palettes.
inline float seasonPhase(uint32_t dayCount, float worldTimePhase) {
	uint32_t cycle = kDaysPerSeason * (uint32_t)Season::Count;
	float d = (float)(dayCount % cycle) + worldTimePhase;
	return d / (float)kDaysPerSeason;
}

inline const char* seasonName(Season s) {
	switch (s) {
		case Season::Spring: return "Spring";
		case Season::Summer: return "Summer";
		case Season::Autumn: return "Autumn";
		case Season::Winter: return "Winter";
		default:             return "?";
	}
}

} // namespace civcraft

#pragma once

// Rule 2: Entity = Living + Item. LivingName/ItemName mutually exclusive.
// BlockType strings map to BlockId (uint16) via BlockRegistry — blocks are NOT entities.

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
	constexpr const char* Player       = "player";
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
	constexpr const char* Dirt         = "dirt";
	constexpr const char* Grass        = "grass";
	constexpr const char* Sand         = "sand";
	constexpr const char* Water        = "water";
	constexpr const char* Wood         = "wood";
	constexpr const char* Log          = "logs";
	constexpr const char* Leaves       = "leaves";
	constexpr const char* Snow         = "snow";
	constexpr const char* TNT          = "tnt";
	constexpr const char* Wheat        = "wheat";
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

} // namespace civcraft

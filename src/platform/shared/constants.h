#pragma once

/**
 * constants.h — single source of truth for all identifiers and enums.
 *
 * Classification model (Entity = Living + Item):
 *
 *   EntityKind (enum):
 *     Living  — moves, has HP, has inventory (players, NPCs, animals)
 *     Item    — on ground or in inventory
 *
 *   LivingName — string IDs for Living entities: "player", "chicken", etc.
 *   ItemName   — string IDs for Item entities: "egg", "apple", etc.
 *   These are mutually exclusive: every entity is either Living or Item.
 *
 *   BlockType — string IDs for world blocks: "stone", "door", etc.
 *   Blocks are NOT entities. Mapped to BlockId (uint16) by BlockRegistry.
 */

#include <cstdint>

namespace civcraft {

// ================================================================
// EntityKind — the two kinds of entity
// ================================================================
enum class EntityKind {
	Living,    // moves, has HP, has inventory (players, NPCs, animals)
	Item,      // on ground or in inventory
	Structure, // made of blocks; valid only while all blueprint blocks are present
};

using EntityId = uint32_t;
constexpr EntityId ENTITY_NONE = 0;

// ================================================================
// Living entity names (species/variant)
// ================================================================
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

// ================================================================
// Feature tags — orthogonal flags declared on Python artifacts.
// An entity can have any subset. Used by behaviors, animation, etc.
// ================================================================
namespace FeatureTag {
	constexpr const char* Humanoid   = "humanoid";   // bipedal (head/arms/legs) — walk anim, follow AI
	constexpr const char* Hostile    = "hostile";     // aggressive to players — flee/combat AI
	constexpr const char* Invincible = "invincible";  // immune to damage
}

// ================================================================
// Structure entity names (multi-block assemblages in the world)
// ================================================================
namespace StructureName {
	constexpr const char* Chest   = "chest";    // single block; owns an inventory
	constexpr const char* Bed     = "bed";      // two-block head + foot
	constexpr const char* Tree    = "tree";     // trunk + leaf canopy
	constexpr const char* House   = "house";    // walls + roof + floor
	constexpr const char* Spawner = "spawner";  // spawner block structure
}

// ================================================================
// Item names (equipment, tools, consumables, dropped blocks)
// ================================================================
namespace ItemName {
	// The item entity type (wrapper for any item on the ground)
	constexpr const char* ItemEntity   = "item_entity";

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

// ================================================================
// Block Type IDs (world blocks in the chunk grid)
// ================================================================
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

// ================================================================
// Property names (entity key-value store)
// ================================================================
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

// ================================================================
// Sound IDs
// ================================================================
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

// ================================================================
// Networking
// ================================================================
constexpr int CIVCRAFT_DISCOVER_PORT = 7778;

} // namespace civcraft

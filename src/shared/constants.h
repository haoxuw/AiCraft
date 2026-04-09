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
 *   LivingName — string IDs for Living entities: "base:player", "base:chicken", etc.
 *   ItemName   — string IDs for Item entities: "base:egg", "base:apple", etc.
 *   These are mutually exclusive: every entity is either Living or Item.
 *
 *   BlockType — string IDs for world blocks: "base:stone", "base:door", etc.
 *   Blocks are NOT entities. Mapped to BlockId (uint16) by BlockRegistry.
 */

#include <cstdint>

namespace modcraft {

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
	constexpr const char* Player       = "base:player";
	constexpr const char* Pig          = "base:pig";
	constexpr const char* Chicken      = "base:chicken";
	constexpr const char* Dog          = "base:dog";
	constexpr const char* Villager     = "base:villager";
	constexpr const char* Cat          = "base:cat";
	constexpr const char* BraveChicken = "base:brave_chicken"; // todo: remove this, MOD from python side
}

// ================================================================
// Structure entity names (multi-block assemblages in the world)
// ================================================================
namespace StructureName {
	constexpr const char* Chest   = "base:chest";    // single block; owns an inventory
	constexpr const char* Bed     = "base:bed";      // two-block head + foot
	constexpr const char* Tree    = "base:tree";     // trunk + leaf canopy
	constexpr const char* House   = "base:house";    // walls + roof + floor
	constexpr const char* Spawner = "base:spawner";  // spawner block structure
}

// ================================================================
// Item names (equipment, tools, consumables, dropped blocks)
// ================================================================
namespace ItemName {
	// The item entity type (wrapper for any item on the ground)
	constexpr const char* ItemEntity   = "base:item_entity";

	// Equipment
	constexpr const char* Jetpack      = "base:jetpack";
	constexpr const char* Parachute    = "base:parachute";

	// Tools
	constexpr const char* WoodPickaxe  = "base:wood_pickaxe";
	constexpr const char* StonePickaxe = "base:stone_pickaxe";
	constexpr const char* WoodAxe      = "base:wood_axe";
	constexpr const char* WoodShovel   = "base:wood_shovel";

	// Consumables
	constexpr const char* Apple        = "base:apple";
	constexpr const char* Bread        = "base:bread";
	constexpr const char* Egg          = "base:egg";
}

// ================================================================
// Block Type IDs (world blocks in the chunk grid)
// ================================================================
namespace BlockType {
	constexpr const char* Air          = "base:air";
	constexpr const char* Stone        = "base:stone";
	constexpr const char* Cobblestone  = "base:cobblestone";
	constexpr const char* Dirt         = "base:dirt";
	constexpr const char* Grass        = "base:grass";
	constexpr const char* Sand         = "base:sand";
	constexpr const char* Water        = "base:water";
	constexpr const char* Wood         = "base:wood";
	constexpr const char* Log          = "base:trunk";
	constexpr const char* Leaves       = "base:leaves";
	constexpr const char* Snow         = "base:snow";
	constexpr const char* TNT          = "base:tnt";
	constexpr const char* Wheat        = "base:wheat";
	constexpr const char* Wire         = "base:wire";
	constexpr const char* NANDGate     = "base:nand_gate";
	constexpr const char* WheatSeeds   = "base:wheat_seeds";
	constexpr const char* Chest        = "base:chest";
	constexpr const char* Planks       = "base:planks";
	constexpr const char* Bed          = "base:bed";
	constexpr const char* Fence        = "base:fence";
	constexpr const char* Farmland     = "base:farmland";
	constexpr const char* Stair        = "base:stair";
	constexpr const char* Glass        = "base:glass";
	constexpr const char* Door         = "base:door";
	constexpr const char* DoorOpen     = "base:door_open";
	constexpr const char* Portal       = "base:portal";
	constexpr const char* ArcaneStone  = "base:arcane_stone";
	constexpr const char* SpawnPoint   = "base:spawn_point";
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
constexpr int MODCRAFT_DISCOVER_PORT = 7778;

} // namespace modcraft

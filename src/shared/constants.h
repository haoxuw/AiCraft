#pragma once

/**
 * All string constants used as identifiers throughout the engine.
 * Single source of truth -- never use raw string literals for IDs.
 *
 * These match the Python definitions in python/agentworld/objects/ and python/agentworld/actions/.
 * When the pybind11 bridge is connected, these constants will be validated against
 * the Python registry at startup.
 */

namespace agentworld {

// ============================================================
// Entity Type IDs
// ============================================================
namespace EntityType {
	constexpr const char* Player      = "base:player";
	constexpr const char* Pig         = "base:pig";
	constexpr const char* Chicken     = "base:chicken";
	constexpr const char* Dog         = "base:dog";
	constexpr const char* Villager    = "base:villager";
	constexpr const char* Cat         = "base:cat";
	constexpr const char* ItemEntity  = "base:item_entity";
}

// ============================================================
// Block Type IDs
// ============================================================
namespace BlockType {
	constexpr const char* Air         = "base:air";
	constexpr const char* Stone       = "base:stone";
	constexpr const char* Cobblestone = "base:cobblestone";
	constexpr const char* Dirt        = "base:dirt";
	constexpr const char* Grass       = "base:grass";
	constexpr const char* Sand        = "base:sand";
	constexpr const char* Water       = "base:water";
	constexpr const char* Wood        = "base:wood";
	constexpr const char* Leaves      = "base:leaves";
	constexpr const char* Snow        = "base:snow";
	constexpr const char* TNT         = "base:tnt";
	constexpr const char* Wheat       = "base:wheat";
	constexpr const char* Wire        = "base:wire";
	constexpr const char* NANDGate    = "base:nand_gate";
	constexpr const char* WheatSeeds  = "base:wheat_seeds";
	constexpr const char* Chest       = "base:chest";
}

// ============================================================
// Item IDs (equipment, tools, consumables)
// Blocks are also items when in inventory. These are non-block items.
// ============================================================
namespace ItemId {
	// Equipment
	constexpr const char* Jetpack     = "base:jetpack";
	constexpr const char* Parachute   = "base:parachute";

	// Tools
	constexpr const char* WoodPickaxe = "base:wood_pickaxe";
	constexpr const char* StonePickaxe= "base:stone_pickaxe";
	constexpr const char* WoodAxe     = "base:wood_axe";
	constexpr const char* WoodShovel  = "base:wood_shovel";

	// Consumables
	constexpr const char* Apple       = "base:apple";
	constexpr const char* Bread       = "base:bread";
}

// ============================================================
// Categories (shared by blocks and entities)
// ============================================================
namespace Category {
	constexpr const char* System      = "system";
	constexpr const char* Terrain     = "terrain";
	constexpr const char* Plant       = "plant";
	constexpr const char* Crafted     = "crafted";
	constexpr const char* Active      = "active";
	constexpr const char* Crop        = "crop";
	constexpr const char* Signal      = "signal";
	constexpr const char* Player      = "player";
	constexpr const char* Animal      = "animal";
	constexpr const char* Item        = "item";
}

// ============================================================
// Property Names (entity + block state keys)
// ============================================================
namespace Prop {
	// Living
	constexpr const char* HP           = "hp";
	constexpr const char* Hunger       = "hunger";
	constexpr const char* Age          = "age";

	// Player
	constexpr const char* SelectedSlot = "selected_slot";

	// Behavior
	constexpr const char* Goal         = "goal";
	constexpr const char* BehaviorId   = "behavior_id";

	// Mob AI (legacy, used by built-in WanderBehavior)
	constexpr const char* WanderTimer  = "wander_timer";
	constexpr const char* WanderYaw    = "wander_yaw";
	constexpr const char* WalkDistance = "walk_distance";

	// Item entity
	constexpr const char* ItemType     = "item_type";
	constexpr const char* Count        = "count";
	constexpr const char* DespawnTime  = "despawn_time";

	// TNT
	constexpr const char* FuseTicks    = "fuse_ticks";
	constexpr const char* Lit          = "lit";

	// Crop
	constexpr const char* GrowthStage  = "growth_stage";
	constexpr const char* MaxStage     = "max_stage";

	// Signal
	constexpr const char* Power        = "power";
	constexpr const char* MaxPower     = "max_power";
	constexpr const char* InputA       = "input_a";
	constexpr const char* InputB       = "input_b";
	constexpr const char* Output       = "output";
}

// ============================================================
// Groups (block game-mechanic tags)
// ============================================================
namespace Group {
	constexpr const char* Cracky       = "cracky";
	constexpr const char* Crumbly      = "crumbly";
	constexpr const char* Choppy       = "choppy";
	constexpr const char* Snappy       = "snappy";
	constexpr const char* Stone        = "stone";
	constexpr const char* Soil         = "soil";
	constexpr const char* Sand         = "sand";
	constexpr const char* Falling      = "falling";
	constexpr const char* Liquid       = "liquid";
	constexpr const char* WaterGrp     = "water";
	constexpr const char* Tree         = "tree";
	constexpr const char* Flammable    = "flammable";
	constexpr const char* Snowy        = "snowy";
	constexpr const char* Spreadable   = "spreadable";
	constexpr const char* Tnt          = "tnt";
	constexpr const char* SignalGrp    = "signal";
	constexpr const char* Logic        = "logic";
	constexpr const char* Unbreakable  = "unbreakable";
}

// ============================================================
// Tool Groups
// ============================================================
namespace Tool {
	constexpr const char* Pickaxe      = "pickaxe";
	constexpr const char* Shovel       = "shovel";
	constexpr const char* Axe          = "axe";
}

// ============================================================
// Sounds
// ============================================================
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

// ============================================================
// Assets (model/texture file names)
// ============================================================
namespace Asset {
	constexpr const char* PlayerModel   = "character.gltf";
	constexpr const char* PlayerTexture = "character.png";
	constexpr const char* PigModel      = "pig.gltf";
	constexpr const char* PigTexture    = "pig.png";
	constexpr const char* ChickenModel  = "chicken.gltf";
	constexpr const char* ChickenTexture= "chicken.png";
}

// ============================================================
// Python behavior class IDs (for pybind11 bridge)
// ============================================================
namespace PyClass {
	constexpr const char* TNTBlock       = "base:tnt_block";
	constexpr const char* WheatCrop      = "base:wheat_crop";
	constexpr const char* WireBlock      = "base:wire_block";
	constexpr const char* NANDGateBlock  = "base:nand_gate_block";
}

} // namespace agentworld

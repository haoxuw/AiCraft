#pragma once

/**
 * All string constants used as identifiers throughout the engine.
 * Single source of truth -- never use raw string literals for IDs.
 *
 * These match the Python definitions in python/modcraft/objects/ and python/modcraft/actions/.
 * When the pybind11 bridge is connected, these constants will be validated against
 * the Python registry at startup.
 */

namespace modcraft {

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
	constexpr const char* BraveChicken = "base:brave_chicken";
	constexpr const char* ItemEntity  = "base:item_entity";
	constexpr const char* ChestEntity = "base:chest_entity";
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
	constexpr const char* Log         = "base:trunk";
	constexpr const char* Leaves      = "base:leaves";
	constexpr const char* Snow        = "base:snow";
	constexpr const char* TNT         = "base:tnt";
	constexpr const char* Wheat       = "base:wheat";
	constexpr const char* Wire        = "base:wire";
	constexpr const char* NANDGate    = "base:nand_gate";
	constexpr const char* WheatSeeds  = "base:wheat_seeds";
	constexpr const char* Chest       = "base:chest";
	constexpr const char* Planks      = "base:planks";
	constexpr const char* Bed         = "base:bed";
	constexpr const char* Fence       = "base:fence";
	constexpr const char* Farmland    = "base:farmland";
	constexpr const char* Stair       = "base:stair";
	constexpr const char* Glass       = "base:glass";
	constexpr const char* Door        = "base:door";
	constexpr const char* DoorOpen    = "base:door_open";
	constexpr const char* Portal      = "base:portal";
	constexpr const char* ArcaneStone = "base:arcane_stone";
	constexpr const char* SpawnPoint  = "base:spawn_point";
}

// ============================================================
// Design note: Blocks vs Items
// ============================================================
// BlockType = PLACEABLE blocks. When broken they drop an item entity
// with item_type = block string_id, which can be picked up and re-placed.
// BlockRegistry is the source of truth for what is placeable.
//
// ItemId = PURE ITEMS (eggs, potions, tools). They float on the ground as
// item entities but cannot be placed as world blocks.
// Pure items are NEVER registered in BlockRegistry.
// ============================================================

// ============================================================
// Item IDs (equipment, tools, consumables)
// These are non-block items — they cannot be placed in the world.
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
	constexpr const char* Egg         = "base:egg";
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
	constexpr const char* Chest       = "chest";
}

// ============================================================
// Property Names (entity + block state keys)
// ============================================================
namespace Prop {
	// Living
	constexpr const char* HP           = "hp";
	constexpr const char* Hunger       = "hunger";
	constexpr const char* Age          = "age";

	// Player / Ownership
	constexpr const char* SelectedSlot = "selected_slot";
	constexpr const char* Owner        = "owner";  // EntityId of owning player (0 = unowned/world)

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
	constexpr const char* PlayerModel   = "player.gltf";
	constexpr const char* PlayerTexture = "player.png";
	constexpr const char* PigModel      = "pig.gltf";
	constexpr const char* PigTexture    = "pig.png";
	constexpr const char* ChickenModel  = "chicken.gltf";
	constexpr const char* ChickenTexture= "chicken.png";
	constexpr const char* DogModel      = "dog.gltf";
	constexpr const char* CatModel      = "cat.gltf";
	constexpr const char* VillagerModel = "villager.gltf";
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

// ============================================================
// Networking
// ============================================================
constexpr int MODCRAFT_DISCOVER_PORT = 7778; // UDP LAN discovery broadcast port

} // namespace modcraft

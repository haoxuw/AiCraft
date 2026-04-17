#include "server/builtin.h"
#include "logic/block_registry.h"
#include "server/entity_manager.h"
#include "logic/constants.h"
#include "logic/material_values.h"

namespace civcraft {

void registerAllBuiltins(BlockRegistry& blocks, EntityManager& entities) {
	namespace BT = BlockType;
	namespace SN = Sound;
	namespace PR = Prop;

	// Blocks — Air must be ID 0.

	// Terrain
	blocks.registerBlock({BT::Air, "Air", {0,0,0},{0,0,0},{0,0,0}, false, true, "",64,0, "","",""});
	blocks.registerBlock({BT::Stone, "Stone", {0.48f,0.48f,0.50f},{0.48f,0.48f,0.50f},{0.48f,0.48f,0.50f}, true,false, BT::Cobblestone,64,0, "",SN::DigStone,SN::StepStone});
	blocks.registerBlock({BT::Cobblestone, "Cobblestone", {0.42f,0.42f,0.44f},{0.42f,0.42f,0.44f},{0.42f,0.42f,0.44f}, true,false, "",64,0, "",SN::DigStone,SN::StepStone});
	blocks.registerBlock({BT::Dirt, "Dirt", {0.52f,0.34f,0.20f},{0.52f,0.34f,0.20f},{0.52f,0.34f,0.20f}, true,false, "",64,0, "",SN::DigDirt,SN::StepDirt});
	blocks.registerBlock({BT::Grass, "Grass Block", {0.30f,0.58f,0.18f},{0.40f,0.38f,0.20f},{0.52f,0.34f,0.20f}, true,false, BT::Dirt,64,0, "",SN::DigDirt,SN::StepGrass});
	blocks.registerBlock({BT::Sand, "Sand", {0.82f,0.77f,0.50f},{0.82f,0.77f,0.50f},{0.82f,0.77f,0.50f}, true,false, "",64,0, "",SN::DigSand,SN::StepSand});
	blocks.registerBlock({BT::Water, "Water", {0.12f,0.35f,0.70f},{0.12f,0.35f,0.70f},{0.12f,0.35f,0.70f}, false,true, "",64,0, "","",""});
	blocks.registerBlock({BT::Snow, "Snow", {0.93f,0.95f,0.97f},{0.90f,0.92f,0.94f},{0.90f,0.92f,0.94f}, true,false, "",64,0, "",SN::DigSnow,SN::StepSnow});
	blocks.registerBlock({BT::Fence, "Fence", {0.55f,0.40f,0.22f},{0.50f,0.35f,0.18f},{0.55f,0.40f,0.22f}, true,false, "",64,0, "",SN::DigWood,SN::StepWood});
	blocks.registerBlock({BT::Farmland, "Farmland", {0.40f,0.28f,0.14f},{0.40f,0.28f,0.14f},{0.40f,0.28f,0.14f}, true,false, BT::Dirt,64,0, "",SN::DigDirt,SN::StepDirt});
	blocks.registerBlock({BT::Glass, "Glass", {0.62f,0.83f,0.88f},{0.58f,0.78f,0.84f},{0.62f,0.83f,0.88f}, true,true, "",64,0, "","",""});
	blocks.registerBlock({BT::Portal, "Portal", {0.65f,0.0f,1.0f},{0.55f,0.0f,0.90f},{0.65f,0.0f,1.0f}, false,true, "",64,0, "","",""});
	blocks.registerBlock({.string_id=BT::ArcaneStone, .display_name="Arcane Stone", .color_top={0.28f,0.06f,0.48f}, .color_side={0.22f,0.08f,0.40f}, .color_bottom={0.16f,0.05f,0.32f}, .solid=true, .stack_max=64, .surface_glow=true});
	blocks.registerBlock({.string_id=BT::SpawnPoint, .display_name="Spawn Point", .color_top={0.95f,0.78f,0.05f}, .color_side={0.85f,0.65f,0.02f}, .color_bottom={0.70f,0.50f,0.01f}, .solid=true, .stack_max=1, .surface_glow=true});
	blocks.registerBlock({BT::Chest, "Chest", {0.55f,0.40f,0.20f},{0.50f,0.35f,0.18f},{0.45f,0.30f,0.15f}, true,false, "",1,0, "",SN::DigWood,SN::StepWood});
	blocks.registerBlock({BT::BeeNest, "Bee Nest", {0.85f,0.60f,0.15f},{0.75f,0.50f,0.12f},{0.60f,0.40f,0.10f}, true,false, "",64,0, "",SN::DigWood,SN::StepWood});

	// Plants
	blocks.registerBlock({BT::Log, "Log", {0.38f,0.28f,0.14f},{0.52f,0.38f,0.20f},{0.38f,0.28f,0.14f}, true,false, "",64,0, "",SN::DigWood,SN::StepWood});
	blocks.registerBlock({BT::Wood, "Wood", {0.50f,0.38f,0.18f},{0.42f,0.28f,0.12f},{0.50f,0.38f,0.18f}, true,false, "",64,0, "",SN::DigWood,SN::StepWood});
	blocks.registerBlock({BT::Planks, "Planks", {0.68f,0.52f,0.30f},{0.64f,0.47f,0.24f},{0.68f,0.52f,0.30f}, true,false, "",64,0, "",SN::DigWood,SN::StepWood});
	blocks.registerBlock({BT::Leaves, "Leaves", {0.18f,0.48f,0.10f},{0.20f,0.45f,0.12f},{0.20f,0.45f,0.12f}, true,false, "",64,0, "",SN::DigLeaves,""});

	// Active (with behavior)
	blocks.registerBlock({BT::TNT, "TNT", {0.80f,0.25f,0.20f},{0.80f,0.25f,0.20f},{0.80f,0.25f,0.20f}, true,false, "",64,0, "","","", BlockBehavior::Active, {{PR::FuseTicks,0},{PR::Lit,0}}, "tnt_block"});
	blocks.registerBlock({BT::Wheat, "Wheat", {0.70f,0.65f,0.20f},{0.55f,0.50f,0.15f},{0.60f,0.55f,0.18f}, true,false, BT::WheatSeeds,64,0, "","","", BlockBehavior::Active, {{PR::GrowthStage,0},{PR::MaxStage,7}}, "wheat_crop"});
	blocks.registerBlock({BT::Wire, "Wire", {0.60f,0.10f,0.10f},{0.60f,0.10f,0.10f},{0.60f,0.10f,0.10f}, false,false, BT::Wire,64,0, "","","", BlockBehavior::Active, {{PR::Power,0},{PR::MaxPower,15}}, "wire_block"});
	blocks.registerBlock({BT::NANDGate, "NAND Gate", {0.30f,0.30f,0.35f},{0.30f,0.30f,0.35f},{0.30f,0.30f,0.35f}, true,false, BT::NANDGate,64,0, "","","", BlockBehavior::Active, {{PR::InputA,0},{PR::InputB,0},{PR::Output,1}}, "nand_gate_block"});

	// Furniture
	blocks.registerBlock({BT::Bed, "Bed", {0.62f,0.14f,0.14f},{0.55f,0.54f,0.57f},{0.60f,0.44f,0.24f}, true,false, "",1,0, "",SN::DigWood,SN::StepWood});
	blocks.registerBlock({BT::Stair, "Stair", {0.68f,0.52f,0.30f},{0.60f,0.44f,0.24f},{0.60f,0.44f,0.24f}, true,false, "",64,0, "",SN::DigWood,SN::StepWood, BlockBehavior::Passive, {}, "", 0.5f, MeshType::Stair, Param2Type::FourDir});
	blocks.registerBlock({BT::Door, "Door", {0.60f,0.44f,0.24f},{0.55f,0.40f,0.20f},{0.55f,0.40f,0.20f}, true,false, "",1,0, "",SN::DigWood,SN::StepWood, BlockBehavior::Passive, {}, "", 1.0f, MeshType::Door});
	blocks.registerBlock({BT::DoorOpen, "Door (Open)", {0.60f,0.44f,0.24f},{0.55f,0.40f,0.20f},{0.55f,0.40f,0.20f}, false,false, BT::Door,1,0, "",SN::DigWood,SN::StepWood, BlockBehavior::Passive, {}, "", 0.0f, MeshType::DoorOpen});

	// Entities

	// No hardcoded "player" type: any Living with playable=true can be spawned
	// as a player character. Client sends its chosen type in C_HELLO; server
	// validates it against EntityDef.playable.

	// Playable humanoids — only the identity + hunger/HP/inventory slots come
	// from C++. Physics (walk/run speed, collision box, eye height, gravity)
	// are filled in by EntityManager::applyLivingStats() from Python artifacts,
	// so `artifacts/living/base/knight.py` is the single source of truth.
	// If applyLivingStats isn't called (headless tools), these fall back to
	// the EntityDef struct defaults in logic/entity.h.
	auto humanoid = [&](const char* id, const char* name, const char* model,
	                     glm::vec3 color) {
		EntityDef def;
		def.string_id = id;
		def.display_name = name;
		def.kind = EntityKind::Living;
		def.model = model;
		def.color = color;
		def.max_hp = (int)getMaterialValue(id);
		def.playable = true;
		def.pickup_range = 1.5f;
		def.inventory_capacity = getMaterialValue(id);
		def.default_props = {
			{PR::HP, def.max_hp}, {PR::Hunger, 20.0f},
			{PR::Age, 0.0f}, {PR::WalkDistance, 0.0f},
		};
		entities.registerType(def);
	};

	humanoid(LivingName::Knight,   "Knight",   "knight",   {0.85f,0.70f,0.55f});
	humanoid(LivingName::Villager, "Villager", "villager", {0.85f,0.75f,0.60f});
	humanoid(LivingName::Mage,     "Mage",     "mage",     {0.85f,0.70f,0.55f});
	humanoid(LivingName::Skeleton, "Skeleton", "skeleton", {0.90f,0.88f,0.80f});
	humanoid(LivingName::Giant,    "Giant",    "giant",    {0.80f,0.70f,0.55f});

	// Animals — all Living. Flyers pass gravityScale=0 to hover at spawn Y.
	auto animal = [&](const char* id, const char* name, const char* model,
	                   glm::vec3 color, glm::vec3 boxMin, glm::vec3 boxMax,
	                   float walkSpeed, float runSpeed,
	                   const char* behavior, const char* soundGroup = "",
	                   const char* texture = "", float soundVol = 0.12f,
	                   float gravityScale = 1.0f) {
		EntityDef def;
		def.string_id = id;
		def.display_name = name;
		def.kind = EntityKind::Living;
		def.model = model;
		if (texture[0]) def.texture = texture;
		def.color = color;
		def.sound_group = soundGroup;
		def.sound_volume = soundVol;
		def.collision_box_min = boxMin;
		def.collision_box_max = boxMax;
		def.gravity_scale = gravityScale;
		def.walk_speed = walkSpeed;
		def.run_speed = runSpeed;
		def.max_hp = (int)getMaterialValue(id);
		def.inventory_capacity = getMaterialValue(id);
		def.pickup_range = def.inventory_capacity > 0.0f ? 1.5f : 0.0f;
		def.default_props = {
			{PR::HP, def.max_hp}, {PR::Age, 0.0f}, {PR::WalkDistance, 0.0f},
			{PR::WanderTimer, 0.0f},
			{PR::BehaviorId, std::string(behavior)},
		};
		entities.registerType(def);
	};

	animal(LivingName::Pig, "Pig", "pig", {0.9f,0.7f,0.7f},
		{-0.4f,0,-0.4f},{0.4f,0.9f,0.4f}, 2.0f,5.0f, "wander",
		"creature_pig", "pig.png", 0.15f);

	animal(LivingName::Chicken, "Chicken", "chicken", {0.95f,0.95f,0.90f},
		{-0.2f,0,-0.2f},{0.2f,0.6f,0.2f}, 2.5f,6.0f, "peck",
		"creature_chicken", "chicken.png", 0.12f);

	animal(LivingName::BraveChicken, "Brave Chicken", "chicken", {1.0f,0.85f,0.30f},
		{-0.2f,0,-0.2f},{0.2f,0.6f,0.2f}, 3.0f,7.0f, "brave_chicken",
		"creature_chicken", "chicken.png", 0.15f);

	animal(LivingName::Cat, "Cat", "cat", {0.90f,0.55f,0.20f},
		{-0.2f,0,-0.2f},{0.2f,0.5f,0.2f}, 3.5f,7.0f, "prowl",
		"creature_cat", "", 0.10f);

	animal(LivingName::Dog, "Dog", "dog", {0.75f,0.55f,0.35f},
		{-0.3f,0,-0.3f},{0.3f,0.7f,0.3f}, 4.0f,8.0f, "follow",
		"creature_dog", "", 0.15f);

	// Villager is registered above via humanoid() (it's playable); no animal() here.

	// Altar animals — wander placeholder; real behaviors later.
	animal(LivingName::Squirrel, "Squirrel", "squirrel", {0.55f,0.32f,0.15f},
		{-0.12f,0,-0.12f},{0.12f,0.35f,0.12f}, 4.0f,9.0f, "wander", "", "", 0.10f);
	animal(LivingName::Raccoon, "Raccoon", "raccoon", {0.45f,0.45f,0.48f},
		{-0.2f,0,-0.2f},{0.2f,0.55f,0.2f}, 3.0f,6.5f, "wander", "", "", 0.10f);
	animal(LivingName::Beaver, "Beaver", "beaver", {0.42f,0.26f,0.14f},
		{-0.2f,0,-0.2f},{0.2f,0.5f,0.2f}, 2.5f,5.0f, "wander", "", "", 0.10f);

	// Flyers — gravity_scale=0; flyer_wander emits "Flying" goalText → fly clip.
	animal(LivingName::Bee, "Bee", "bee", {0.95f,0.78f,0.15f},
		{-0.1f,0,-0.1f},{0.1f,0.3f,0.1f}, 4.0f,8.0f, "flyer_wander", "", "", 0.10f, 0.0f);
	animal(LivingName::Owl, "Owl", "owl", {0.45f,0.30f,0.15f},
		{-0.2f,0,-0.2f},{0.2f,0.8f,0.2f}, 3.0f,6.0f, "flyer_wander", "", "", 0.10f, 0.0f);

	// Structure entities
	{
		EntityDef def;
		def.string_id = StructureName::Chest;
		def.display_name = "Chest";
		def.kind = EntityKind::Structure;
		def.has_inventory = true;
		def.collision_box_min = {0, 0, 0};
		def.collision_box_max = {1, 1, 1};
		entities.registerType(def);
	}

	// Monument — trident tower at village center. Flame FX client-side (Rule 5).
	// GrowthStage seeds future growth loop; starts at current towerH for parity.
	{
		EntityDef def;
		def.string_id = StructureName::Monument;
		def.display_name = "Monument";
		def.kind = EntityKind::Structure;
		def.has_inventory = false;
		def.collision_box_min = {-0.1f, -0.1f, -0.1f};
		def.collision_box_max = { 0.1f,  0.1f,  0.1f};
		def.default_props = {
			{PR::GrowthStage, 18},  // current towerH
			{PR::MaxStage,    32},  // future growth cap
		};
		entities.registerType(def);
	}

	// Dropped-item wrapper.
	{
		EntityDef def;
		def.string_id = ItemName::ItemEntity;
		def.display_name = "Item";
		def.kind = EntityKind::Item;
		def.color = {1, 1, 1};
		def.collision_box_min = {-0.15f, 0.0f, -0.15f};
		def.collision_box_max = { 0.15f, 0.3f,  0.15f};
		def.gravity_scale = 1.0f;
		def.default_props = {
			{PR::ItemType, std::string(BT::Dirt)}, {PR::Count, 1},
			{PR::Age, 0.0f}, {PR::DespawnTime, 300.0f},
		};
		entities.registerType(def);
	}
}

} // namespace civcraft

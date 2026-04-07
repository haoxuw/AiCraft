#pragma once

#include "server/entity_manager.h"
#include "shared/constants.h"

namespace modcraft::builtin {

inline void registerItemEntities(EntityManager& mgr) {
	// Chest pseudo-entity — static entity at each chest block, carries inventory.
	// Spawned by server::init() so Python behaviors can find chests via world["nearby"].
	{
		EntityDef def;
		def.string_id = EntityType::ChestEntity;
		def.display_name = "Chest";
		def.kind = EntityKind::Living;  // Living → gets inventory allocated
		def.category = Category::Chest;
		def.color = {0.6f, 0.4f, 0.2f};
		def.collision_box_min = {0, 0, 0};
		def.collision_box_max = {0, 0, 0};  // no collision
		def.gravity_scale = 0.0f;  // static, doesn't fall
		def.walk_speed = 0.0f;
		def.max_hp = 0;  // indestructible
		mgr.registerType(def);
	}

	EntityDef def;
	def.string_id = EntityType::ItemEntity;
	def.display_name = "Item";
	def.kind = EntityKind::Item;
	def.category = Category::Item;
	def.color = {1, 1, 1};
	def.collision_box_min = {-0.15f, 0.0f, -0.15f};
	def.collision_box_max = { 0.15f, 0.3f,  0.15f};
	def.gravity_scale = 1.0f;  // falls to ground, then floats above surface
	def.default_props = {
		{Prop::ItemType, std::string(BlockType::Dirt)}, {Prop::Count, 1},
		{Prop::Age, 0.0f}, {Prop::DespawnTime, 300.0f},
	};
	mgr.registerType(def);
}

} // namespace modcraft::builtin

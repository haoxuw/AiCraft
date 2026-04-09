#pragma once

#include "server/entity_manager.h"
#include "shared/constants.h"

namespace modcraft::builtin {

inline void registerItemEntities(EntityManager& mgr) {
	// Chests are blocks, not entities. Their inventories are managed by
	// GameServer::m_blockInventories, keyed by block position.

	EntityDef def;
	def.string_id = EntityType::ItemEntity;
	def.display_name = "Item";
	def.kind = EntityKind::Item;
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

#pragma once

#include "common/entity_manager.h"
#include "common/constants.h"
#include "game/types.h"

namespace aicraft::builtin {

inline void registerPlayerEntity(EntityManager& mgr) {
	EntityDef def;
	def.string_id = EntityType::Player;
	def.display_name = "Player";
	def.category = Category::Player;
	def.model = Asset::PlayerModel;
	def.texture = Asset::PlayerTexture;
	def.color = {1, 1, 1};
	def.collision_box_min = {-0.375f, 0.0f, -0.375f};
	def.collision_box_max = { 0.375f, 2.5f,  0.375f};
	def.gravity_scale = 1.0f;
	def.walk_speed = 8.0f;    // base movement speed
	def.run_speed = 20.0f;    // sprint speed
	def.max_hp = 20;
	def.eye_height = 1.9f;    // camera eye position above feet
	def.has_inventory = true;
	def.default_props = {
		{Prop::HP, 20},
		{Prop::Hunger, 20.0f},
		{Prop::SelectedSlot, 0},
		{Prop::Age, 0.0f},
		{Prop::WalkDistance, 0.0f},
	};
	mgr.registerType(def);
}

} // namespace aicraft::builtin

#pragma once

#include "common/entity_manager.h"
#include "common/constants.h"

namespace aicraft::builtin {

inline void registerAnimalEntities(EntityManager& mgr) {
	{
		EntityDef def;
		def.string_id = EntityType::Pig;
		def.display_name = "Pig";
		def.category = Category::Animal;
		def.model = Asset::PigModel;
		def.texture = Asset::PigTexture;
		def.color = {0.9f, 0.7f, 0.7f};
		def.collision_box_min = {-0.4f, 0.0f, -0.4f};
		def.collision_box_max = { 0.4f, 0.9f,  0.4f};
		def.gravity_scale = 1.0f;
		def.walk_speed = 2.0f;
		def.run_speed = 5.0f;
		def.max_hp = 10;
		def.default_props = {
			{Prop::HP, 10}, {Prop::Hunger, 0.5f}, {Prop::Age, 0.0f},
			{Prop::WanderTimer, 0.0f}, {Prop::WanderYaw, 0.0f}, {Prop::WalkDistance, 0.0f},
		};
		mgr.registerType(def);
	}

	{
		EntityDef def;
		def.string_id = EntityType::Chicken;
		def.display_name = "Chicken";
		def.category = Category::Animal;
		def.model = Asset::ChickenModel;
		def.texture = Asset::ChickenTexture;
		def.color = {0.95f, 0.95f, 0.90f};
		def.collision_box_min = {-0.2f, 0.0f, -0.2f};
		def.collision_box_max = { 0.2f, 0.6f,  0.2f};
		def.gravity_scale = 1.0f;
		def.walk_speed = 2.5f;
		def.run_speed = 6.0f;
		def.max_hp = 5;
		def.default_props = {
			{Prop::HP, 5}, {Prop::Hunger, 0.5f}, {Prop::Age, 0.0f},
			{Prop::WanderTimer, 0.0f}, {Prop::WanderYaw, 0.0f}, {Prop::WalkDistance, 0.0f},
		};
		mgr.registerType(def);
	}
}

} // namespace aicraft::builtin

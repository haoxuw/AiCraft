#pragma once

#include "server/entity_manager.h"
#include "shared/constants.h"

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

	// Dog — loyal companion
	{
		EntityDef def;
		def.string_id = EntityType::Dog;
		def.display_name = "Dog";
		def.category = Category::Animal;
		def.color = {0.75f, 0.55f, 0.35f}; // brown
		def.collision_box_min = {-0.3f, 0.0f, -0.3f};
		def.collision_box_max = { 0.3f, 0.7f,  0.3f};
		def.gravity_scale = 1.0f;
		def.walk_speed = 4.0f;  // faster than pig, can keep up with player
		def.run_speed = 8.0f;
		def.max_hp = 15;
		def.default_props = {
			{Prop::HP, 15}, {Prop::Age, 0.0f},
			{Prop::WanderTimer, 0.0f}, {Prop::WalkDistance, 0.0f},
			{Prop::BehaviorId, std::string("dog")},
		};
		mgr.registerType(def);
	}

	// Villager — industrious NPC
	{
		EntityDef def;
		def.string_id = EntityType::Villager;
		def.display_name = "Villager";
		def.category = Category::Animal; // uses same AI dispatch as animals
		def.color = {0.85f, 0.75f, 0.60f}; // tan skin
		def.collision_box_min = {-0.3f, 0.0f, -0.3f};
		def.collision_box_max = { 0.3f, 1.8f,  0.3f};
		def.gravity_scale = 1.0f;
		def.walk_speed = 2.5f;
		def.run_speed = 5.0f;
		def.max_hp = 20;
		def.eye_height = 1.5f;
		def.default_props = {
			{Prop::HP, 20}, {Prop::Age, 0.0f},
			{Prop::WanderTimer, 0.0f}, {Prop::WalkDistance, 0.0f},
			{Prop::BehaviorId, std::string("villager")},
		};
		mgr.registerType(def);
	}
}

} // namespace aicraft::builtin

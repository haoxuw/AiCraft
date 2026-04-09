#pragma once

#include "server/entity_manager.h"
#include "shared/constants.h"

namespace modcraft::builtin {

inline void registerAnimalEntities(EntityManager& mgr) {
	{
		EntityDef def;
		def.string_id = LivingName::Pig;
		def.display_name = "Pig";
		def.kind = EntityKind::Living;
	
		def.model = "pig";
		def.texture = "pig.png";
		def.color = {0.9f, 0.7f, 0.7f};
		def.sound_group = "creature_pig";
		def.sound_volume = 0.15f;
		def.collision_box_min = {-0.4f, 0.0f, -0.4f};
		def.collision_box_max = { 0.4f, 0.9f,  0.4f};
		def.gravity_scale = 1.0f;
		def.walk_speed = 2.0f;
		def.run_speed = 5.0f;
		def.max_hp = 10;
		def.default_props = {
			{Prop::HP, 10}, {Prop::Hunger, 0.5f}, {Prop::Age, 0.0f},
			{Prop::WanderTimer, 0.0f}, {Prop::WanderYaw, 0.0f}, {Prop::WalkDistance, 0.0f},
			{Prop::BehaviorId, std::string("wander")},
		};
		mgr.registerType(def);
	}

	{
		EntityDef def;
		def.string_id = LivingName::Chicken;
		def.display_name = "Chicken";
		def.kind = EntityKind::Living;
	
		def.model = "chicken";
		def.texture = "chicken.png";
		def.color = {0.95f, 0.95f, 0.90f};
		def.sound_group = "creature_chicken";
		def.sound_volume = 0.12f;
		def.collision_box_min = {-0.2f, 0.0f, -0.2f};
		def.collision_box_max = { 0.2f, 0.6f,  0.2f};
		def.gravity_scale = 1.0f;
		def.walk_speed = 2.5f;
		def.run_speed = 6.0f;
		def.max_hp = 5;
		def.default_props = {
			{Prop::HP, 5}, {Prop::Hunger, 0.5f}, {Prop::Age, 0.0f},
			{Prop::WanderTimer, 0.0f}, {Prop::WanderYaw, 0.0f}, {Prop::WalkDistance, 0.0f},
			{Prop::BehaviorId, std::string("peck")},
		};
		mgr.registerType(def);
	}

	// Brave Chicken — fearless hen, follows player, fights cats
	{
		EntityDef def;
		def.string_id = LivingName::BraveChicken;
		def.display_name = "Brave Chicken";
		def.kind = EntityKind::Living;
	
		def.model = "chicken";
		def.texture = "chicken.png";
		def.color = {1.0f, 0.85f, 0.30f}; // golden yellow
		def.sound_group = "creature_chicken";
		def.sound_volume = 0.15f;
		def.collision_box_min = {-0.2f, 0.0f, -0.2f};
		def.collision_box_max = { 0.2f, 0.6f,  0.2f};
		def.gravity_scale = 1.0f;
		def.walk_speed = 3.0f;
		def.run_speed = 7.0f;
		def.max_hp = 8;
		def.default_props = {
			{Prop::HP, 8}, {Prop::Age, 0.0f},
			{Prop::WanderTimer, 0.0f}, {Prop::WalkDistance, 0.0f},
			{Prop::BehaviorId, std::string("brave_chicken")},
		};
		mgr.registerType(def);
	}

	// Cat — independent, chases chickens
	{
		EntityDef def;
		def.string_id = LivingName::Cat;
		def.display_name = "Cat";
		def.kind = EntityKind::Living;
	
		def.model = "cat";
		def.color = {0.90f, 0.55f, 0.20f}; // orange tabby
		def.sound_group = "creature_cat";
		def.sound_volume = 0.10f;
		def.collision_box_min = {-0.2f, 0.0f, -0.2f};
		def.collision_box_max = { 0.2f, 0.5f,  0.2f};
		def.gravity_scale = 1.0f;
		def.walk_speed = 3.5f;
		def.run_speed = 7.0f;
		def.max_hp = 8;
		def.default_props = {
			{Prop::HP, 8}, {Prop::Age, 0.0f},
			{Prop::WanderTimer, 0.0f}, {Prop::WalkDistance, 0.0f},
			{Prop::BehaviorId, std::string("prowl")},
		};
		mgr.registerType(def);
	}

	// Dog — loyal companion
	{
		EntityDef def;
		def.string_id = LivingName::Dog;
		def.display_name = "Dog";
		def.kind = EntityKind::Living;
	
		def.model = "dog";
		def.color = {0.75f, 0.55f, 0.35f}; // brown
		def.sound_group = "creature_dog";
		def.sound_volume = 0.15f;
		def.collision_box_min = {-0.3f, 0.0f, -0.3f};
		def.collision_box_max = { 0.3f, 0.7f,  0.3f};
		def.gravity_scale = 1.0f;
		def.walk_speed = 4.0f;
		def.run_speed = 8.0f;
		def.max_hp = 15;
		def.default_props = {
			{Prop::HP, 15}, {Prop::Age, 0.0f},
			{Prop::WanderTimer, 0.0f}, {Prop::WalkDistance, 0.0f},
			{Prop::BehaviorId, std::string("follow")},
		};
		mgr.registerType(def);
	}

	// Villager — industrious NPC
	{
		EntityDef def;
		def.string_id = LivingName::Villager;
		def.display_name = "Villager";
		def.kind = EntityKind::Living;
		// category removed — use EntityKind + EntityType // uses same AI dispatch as animals
		def.model = "villager";
		def.color = {0.85f, 0.75f, 0.60f}; // tan skin
		def.sound_group = "creature_villager";
		def.sound_volume = 0.12f;
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
			{Prop::BehaviorId, std::string("woodcutter")},
		};
		mgr.registerType(def);
	}
}

} // namespace modcraft::builtin

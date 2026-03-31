#pragma once

#include "common/entity_manager.h"
#include "common/constants.h"

namespace aicraft::builtin {

inline void registerAnimalEntities(EntityManager& mgr) {
	mgr.registerType({EntityType::Pig, "Pig", Category::Animal,
		Asset::PigModel, Asset::PigTexture, {0.9f, 0.7f, 0.7f},
		{-0.4f, 0.0f, -0.4f}, {0.4f, 0.9f, 0.4f},
		1.0f, 2.0f, 5.0f, 10,
		{{Prop::HP, 10}, {Prop::Hunger, 0.5f}, {Prop::Age, 0.0f},
		 {Prop::WanderTimer, 0.0f}, {Prop::WanderYaw, 0.0f}}
	});

	mgr.registerType({EntityType::Chicken, "Chicken", Category::Animal,
		Asset::ChickenModel, Asset::ChickenTexture, {0.95f, 0.95f, 0.90f},
		{-0.2f, 0.0f, -0.2f}, {0.2f, 0.6f, 0.2f},
		1.0f, 2.5f, 6.0f, 5,
		{{Prop::HP, 5}, {Prop::Hunger, 0.5f}, {Prop::Age, 0.0f},
		 {Prop::WanderTimer, 0.0f}, {Prop::WanderYaw, 0.0f}}
	});
}

} // namespace aicraft::builtin

#pragma once

#include "common/entity_manager.h"
#include "common/constants.h"

namespace aicraft::builtin {

inline void registerItemEntities(EntityManager& mgr) {
	mgr.registerType({EntityType::ItemEntity, "Item", Category::Item,
		"", "", {1, 1, 1},
		{-0.15f, 0.0f, -0.15f}, {0.15f, 0.3f, 0.15f},
		1.0f, 0.0f, 0.0f, 0,
		{{Prop::ItemType, std::string(BlockType::Dirt)}, {Prop::Count, 1},
		 {Prop::Age, 0.0f}, {Prop::DespawnTime, 300.0f}}
	});
}

} // namespace aicraft::builtin

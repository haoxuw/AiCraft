#pragma once

#include "common/entity_manager.h"
#include "common/constants.h"

namespace aicraft::builtin {

inline void registerPlayerEntity(EntityManager& mgr) {
	mgr.registerType({EntityType::Player, "Player", Category::Player,
		Asset::PlayerModel, Asset::PlayerTexture, {1,1,1},
		{-0.3f, 0.0f, -0.3f}, {0.3f, 1.7f, 0.3f},
		1.0f, 4.0f, 6.0f, 20,
		{{Prop::HP, 20}, {Prop::Hunger, 20.0f}, {Prop::SelectedSlot, 0}}
	});
}

} // namespace aicraft::builtin

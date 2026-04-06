#include "content/builtin.h"
#include "content/blocks_terrain.h"
#include "content/blocks_plants.h"
#include "content/blocks_active.h"
#include "content/blocks_furniture.h"
#include "content/entities_player.h"
#include "content/entities_animals.h"
#include "content/entities_items.h"

namespace modcraft {

void registerAllBuiltins(BlockRegistry& blocks, EntityManager& entities) {
	// Blocks (order matters: Air must be ID 0)
	builtin::registerTerrainBlocks(blocks);
	builtin::registerPlantBlocks(blocks);
	builtin::registerActiveBlocks(blocks);
	builtin::registerFurnitureBlocks(blocks);

	// Entities
	builtin::registerPlayerEntity(entities);
	builtin::registerAnimalEntities(entities);
	builtin::registerItemEntities(entities);
}

} // namespace modcraft

#include "builtin/builtin.h"
#include "builtin/blocks_terrain.h"
#include "builtin/blocks_plants.h"
#include "builtin/blocks_active.h"
#include "builtin/entities_player.h"
#include "builtin/entities_animals.h"
#include "builtin/entities_items.h"

namespace agentworld {

void registerAllBuiltins(BlockRegistry& blocks, EntityManager& entities) {
	// Blocks (order matters: Air must be ID 0)
	builtin::registerTerrainBlocks(blocks);
	builtin::registerPlantBlocks(blocks);
	builtin::registerActiveBlocks(blocks);

	// Entities
	builtin::registerPlayerEntity(entities);
	builtin::registerAnimalEntities(entities);
	builtin::registerItemEntities(entities);
}

} // namespace agentworld

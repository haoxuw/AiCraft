#pragma once

/**
 * Built-in content registration.
 *
 * All base game content (blocks, entities, models) is defined here.
 * The engine registries (BlockRegistry, EntityManager) are generic --
 * they don't know WHAT exists, only HOW to store it.
 *
 * To add a new built-in block/entity/model:
 *   1. Add its ID to constants.h
 *   2. Add its definition to the appropriate file below
 *   3. It gets registered at startup automatically
 *
 * Player-created content follows the same pattern but is loaded
 * at runtime via the ArtifactRegistry (Python hot-load).
 */

namespace agentworld {

class BlockRegistry;
class EntityManager;

// Register all built-in content. Called once at startup.
void registerAllBuiltins(BlockRegistry& blocks, EntityManager& entities);

} // namespace agentworld

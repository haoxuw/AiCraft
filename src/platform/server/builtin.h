#pragma once

// Base-game content registration. Registries are generic (how to store);
// this file defines what exists. Modded content loads via ArtifactRegistry.

namespace civcraft {

class BlockRegistry;
class EntityManager;

void registerAllBuiltins(BlockRegistry& blocks, EntityManager& entities);

} // namespace civcraft

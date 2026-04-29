#pragma once

// Base-game content registration. Registries are generic (how to store);
// this file defines what exists. Modded content loads via ArtifactRegistry.

namespace solarium {

class BlockRegistry;
class EntityManager;

// Split entry points — block-only registration is needed by clients that
// own a ChunkSource (LocalWorldManager) without the EntityManager sibling.
void registerBlockBuiltins(BlockRegistry& blocks);
void registerEntityBuiltins(EntityManager& entities);

// Convenience for callers that own both managers (server, plaza pre-init).
void registerAllBuiltins(BlockRegistry& blocks, EntityManager& entities);

} // namespace solarium

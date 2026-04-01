#pragma once

/**
 * ClientWorld — read-only local cache of server state.
 *
 * The network client never touches the real World. Instead it receives
 * state updates from the server (entity positions, chunk data, block
 * changes) and stores them here. The renderer, mesher, raycast, and
 * HUD all read from ClientWorld.
 *
 * This is the client's "view" of the world — always a snapshot of
 * what the server last sent. The server is the sole authority.
 */

#include "shared/types.h"
#include "shared/chunk.h"
#include "shared/block_registry.h"
#include "shared/chunk_source.h"
#include "shared/entity.h"
#include "shared/inventory.h"
#include <unordered_map>
#include <memory>
#include <string>

namespace agentworld {

// Lightweight entity data for client rendering (no behavior, no physics)
struct ClientEntity {
	EntityId id = ENTITY_NONE;
	std::string typeId;
	std::string category;
	glm::vec3 position = {0, 0, 0};
	glm::vec3 velocity = {0, 0, 0};
	float yaw = 0;
	bool onGround = false;
	std::string goalText;
	int hp = 0;
	int maxHp = 0;
	float walkDistance = 0; // accumulated for animation

	// Properties (subset received from server)
	std::unordered_map<std::string, PropValue> props;

	template<typename T>
	T getProp(const std::string& key, T fallback = {}) const {
		auto it = props.find(key);
		if (it == props.end()) return fallback;
		if (auto* v = std::get_if<T>(&it->second)) return *v;
		return fallback;
	}
};

class ClientWorld : public ChunkSource {
public:
	BlockRegistry blocks; // block definitions (loaded at startup, same as server)

	const BlockRegistry& blockRegistry() const override { return blocks; }

	// --- Chunk cache ---

	void applyChunkRaw(ChunkPos pos, const std::vector<BlockId>& blockData) {
		auto& chunk = m_chunks[pos];
		if (!chunk) chunk = std::make_unique<Chunk>();
		for (int i = 0; i < CHUNK_VOLUME && i < (int)blockData.size(); i++)
			chunk->setRaw(i, blockData[i]);
		// Keep dirty=true so the renderer meshes this chunk
	}

	Chunk* getChunk(ChunkPos pos) override {
		auto it = m_chunks.find(pos);
		return it != m_chunks.end() ? it->second.get() : nullptr;
	}

	Chunk* getChunkIfLoaded(ChunkPos pos) override { return getChunk(pos); }

	BlockId getBlock(int x, int y, int z) override {
		ChunkPos cp = {
			(x < 0 ? (x + 1) / CHUNK_SIZE - 1 : x / CHUNK_SIZE),
			(y < 0 ? (y + 1) / CHUNK_SIZE - 1 : y / CHUNK_SIZE),
			(z < 0 ? (z + 1) / CHUNK_SIZE - 1 : z / CHUNK_SIZE)
		};
		auto* chunk = getChunk(cp);
		if (!chunk) return BLOCK_AIR;
		return chunk->get(
			((x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE,
			((y % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE,
			((z % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE
		);
	}

	bool isSolid(int x, int y, int z) {
		return blocks.get(getBlock(x, y, z)).solid;
	}

	// --- Block changes ---

	void setBlock(int x, int y, int z, BlockId bid) {
		ChunkPos cp = {
			(x < 0 ? (x + 1) / CHUNK_SIZE - 1 : x / CHUNK_SIZE),
			(y < 0 ? (y + 1) / CHUNK_SIZE - 1 : y / CHUNK_SIZE),
			(z < 0 ? (z + 1) / CHUNK_SIZE - 1 : z / CHUNK_SIZE)
		};
		auto* chunk = getChunk(cp);
		if (!chunk) return;
		chunk->set(
			((x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE,
			((y % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE,
			((z % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE,
			bid
		);
	}

	// --- Entity cache ---

	void applyEntityUpdate(EntityId id, const std::string& typeId,
	                        glm::vec3 pos, glm::vec3 vel, float yaw,
	                        bool onGround, const std::string& goal,
	                        int hp, int maxHp) {
		auto& ce = m_entities[id];
		// Track walk distance for animation
		if (glm::length(glm::vec2(vel.x, vel.z)) > 0.1f)
			ce.walkDistance += glm::length(glm::vec2(vel.x, vel.z)) * 0.05f;

		ce.id = id;
		ce.typeId = typeId;
		ce.position = pos;
		ce.velocity = vel;
		ce.yaw = yaw;
		ce.onGround = onGround;
		ce.goalText = goal;
		ce.hp = hp;
		ce.maxHp = maxHp;
	}

	void removeEntity(EntityId id) {
		m_entities.erase(id);
	}

	const ClientEntity* getEntity(EntityId id) const {
		auto it = m_entities.find(id);
		return it != m_entities.end() ? &it->second : nullptr;
	}

	const std::unordered_map<EntityId, ClientEntity>& entities() const {
		return m_entities;
	}

	// --- Player state ---

	EntityId localPlayerId = ENTITY_NONE;

	const ClientEntity* localPlayer() const {
		return getEntity(localPlayerId);
	}

	// --- Inventory ---

	void setInventory(EntityId id, Inventory inv) {
		m_inventories[id] = std::move(inv);
	}

	const Inventory* getInventory(EntityId id) const {
		auto it = m_inventories.find(id);
		return it != m_inventories.end() ? &it->second : nullptr;
	}

	// --- World time ---
	float worldTime = 0.30f;

	// --- Stats ---
	size_t entityCount() const { return m_entities.size(); }
	size_t chunkCount() const { return m_chunks.size(); }

private:
	std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, ChunkPosHash> m_chunks;
	std::unordered_map<EntityId, ClientEntity> m_entities;
	std::unordered_map<EntityId, Inventory> m_inventories;
};

} // namespace agentworld

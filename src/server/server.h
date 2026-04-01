#pragma once

/**
 * GameServer — authoritative world simulation.
 *
 * Owns the World, runs the 3-phase game loop, validates all actions.
 * Clients connect and send ActionProposals; server validates, executes,
 * and broadcasts state updates back.
 *
 * Can run:
 *   - In-process (singleplayer): Game creates server + connects locally
 *   - Standalone (dedicated): main_server.cpp runs headless
 */

#include "server/world.h"
#include "server/entity_manager.h"
#include "shared/action.h"
#include "shared/constants.h"
#include "server/world_template.h"
#include "shared/block_registry.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
#include <functional>
#include <cstdio>

namespace agentworld {

using ClientId = uint32_t;

// Server-side effect callbacks (client provides these for visual feedback)
struct ServerCallbacks {
	std::function<void(ChunkPos cp)> onChunkDirty;
	std::function<void(glm::vec3 pos, glm::vec3 color, int count)> onBlockBreak;
	std::function<void(glm::vec3 pos, glm::vec3 color)> onItemPickup;
};

struct ServerConfig {
	int seed = 42;
	int templateIndex = 1;  // VillageWorld (has trees)
	bool creative = false;  // survival by default
	int port = 7777;
};

class GameServer {
public:
	// Initialize server with world
	void init(const ServerConfig& config,
	          const std::vector<std::shared_ptr<WorldTemplate>>& templates) {
		auto tmpl = (config.templateIndex < (int)templates.size())
			? templates[config.templateIndex] : templates[0];
		m_world = std::make_unique<World>(config.seed, tmpl, config.templateIndex);
		m_creative = config.creative;
		m_worldTime = 0.30f;

		// Find spawn position
		float sx = 30, sz = 30;
		for (int t = 0; t < 50; t++) {
			float h = m_world->surfaceHeight(sx, sz);
			if (h > 2 && h < 15) break;
			sx += 7; sz += 3;
		}
		m_spawnPos = {sx, m_world->surfaceHeight(sx, sz) + 1, sz};

		// Spawn mobs
		for (int m = 0; m < 4; m++) {
			float emx = sx + (m % 2 == 0 ? 8.0f : -6.0f) + m * 3;
			float emz = sz + (m % 2 == 0 ? 5.0f : -8.0f) + m * 2;
			float emh = m_world->surfaceHeight(emx, emz) + 1;
			m_world->entities.spawn(EntityType::Pig, {emx, emh, emz});
		}
		for (int m = 0; m < 3; m++) {
			float emx = sx + 4.0f + m * 5;
			float emz = sz - 3.0f + m * 4;
			float emh = m_world->surfaceHeight(emx, emz) + 1;
			m_world->entities.spawn(EntityType::Chicken, {emx, emh, emz});
		}

		// Dog (near spawn, follows player)
		{
			float dx = sx + 3, dz = sz + 2;
			float dh = m_world->surfaceHeight(dx, dz) + 1;
			m_world->entities.spawn(EntityType::Dog, {dx, dh, dz});
		}

		// Cats (roam near chickens)
		for (int m = 0; m < 2; m++) {
			float cx = sx + 5.0f + m * 7;
			float cz = sz - 4.0f + m * 6;
			float ch = m_world->surfaceHeight(cx, cz) + 1;
			m_world->entities.spawn(EntityType::Cat, {cx, ch, cz});
		}

		// Place a chest at the village center
		{
			int chestX = (int)sx, chestZ = (int)sz;
			int chestY = (int)m_world->surfaceHeight((float)chestX, (float)chestZ) + 1;
			BlockId chestId = m_world->blocks.getId(BlockType::Chest);
			if (chestId != BLOCK_AIR) {
				ChunkPos cp = worldToChunk(chestX, chestY, chestZ);
				Chunk* c = m_world->getChunk(cp);
				if (c) {
					c->set(((chestX%16)+16)%16, ((chestY%16)+16)%16, ((chestZ%16)+16)%16, chestId);
				}
			}
			m_chestPos = {(float)chestX + 0.5f, (float)chestY, (float)chestZ + 0.5f};
		}

		// Villagers — spawn in forest area where trees grow
		for (int m = 0; m < 2; m++) {
			float vx = sx + 25.0f + m * 10;
			float vz = sz + 25.0f + m * 8;
			float vh = m_world->surfaceHeight(vx, vz) + 1;
			m_world->entities.spawn(EntityType::Villager, {vx, vh, vz});
		}

		printf("[Server] Initialized. Spawn: %.0f, %.0f, %.0f (chest at %.0f,%.0f,%.0f)\n",
		       m_spawnPos.x, m_spawnPos.y, m_spawnPos.z,
		       m_chestPos.x, m_chestPos.y, m_chestPos.z);
	}

	// Add a client. Returns the player's EntityId.
	EntityId addClient(ClientId clientId) {
		EntityId eid = m_world->entities.spawn(EntityType::Player, m_spawnPos);
		Entity* pe = m_world->entities.get(eid);
		if (pe && pe->inventory) {
			if (m_creative) {
				pe->inventory->add(BlockType::Stone, 999);
				pe->inventory->add(BlockType::Dirt, 999);
				pe->inventory->add(BlockType::Grass, 999);
				pe->inventory->add(BlockType::Sand, 999);
				pe->inventory->add(BlockType::Wood, 999);
				pe->inventory->add(BlockType::Leaves, 999);
				pe->inventory->add(BlockType::Snow, 999);
				pe->inventory->add(BlockType::TNT, 999);
				pe->inventory->add(BlockType::Cobblestone, 999);
			} else {
				pe->inventory->add(BlockType::Stone, 10);
				pe->inventory->add(BlockType::Wood, 10);
			}
			// Starting equipment
			pe->inventory->add("base:sword", 1);
			pe->inventory->add("base:shield", 1);
			pe->inventory->add("base:potion", 3);
		}
		m_clients[clientId] = {eid};
		printf("[Server] Client %u joined. Player entity: %u\n", clientId, eid);
		return eid;
	}

	void removeClient(ClientId clientId) {
		auto it = m_clients.find(clientId);
		if (it != m_clients.end()) {
			m_world->entities.remove(it->second.playerEntityId);
			m_clients.erase(it);
			printf("[Server] Client %u disconnected.\n", clientId);
		}
	}

	// Receive an action from a client
	void receiveAction(ClientId clientId, ActionProposal action) {
		// Validate that the actor belongs to this client
		auto it = m_clients.find(clientId);
		if (it == m_clients.end()) return;
		if (action.actorId != it->second.playerEntityId) return; // anti-cheat
		m_world->actions.propose(action);
	}

	// Set callbacks for visual effects (client provides these)
	void setCallbacks(ServerCallbacks cb) { m_callbacks = cb; }

	// Main server tick
	void tick(float dt) {
		BlockSolidFn solidFn = [&](int x, int y, int z) {
			return m_world->blocks.get(m_world->getBlock(x, y, z)).solid;
		};

		// Phase 3: AI behaviors gather decisions
		// Pass block query so behaviors can find trees, resources, etc.
		EntityManager::BlockTypeFn blockQuery = [&](int x, int y, int z) -> std::string {
			BlockId bid = m_world->getBlock(x, y, z);
			return m_world->blocks.get(bid).string_id;
		};
		m_world->entities.gatherDecisions(dt, m_world->actions, blockQuery);

		// Phase 1: Resolve all proposals
		resolveActions(dt);

		// Physics for all entities
		m_world->entities.stepPhysics(dt, solidFn);

		// Active block ticking (TNT, wheat, wire)
		m_activeBlockTimer += dt;
		if (m_activeBlockTimer >= 0.05f) {
			m_world->tickActiveBlocks(m_activeBlockTimer, [&](int bx, int by, int bz, BlockId bid) {
				if (m_callbacks.onChunkDirty)
					m_callbacks.onChunkDirty(World::worldToChunk(bx, by, bz));
				if (m_callbacks.onBlockBreak)
					m_callbacks.onBlockBreak(glm::vec3(bx, by, bz), {0.9f, 0.7f, 0.2f}, 4);
			});
			m_activeBlockTimer = 0;
		}

		// Item pickup for all player entities
		for (auto& [cid, cs] : m_clients) {
			Entity* pe = m_world->entities.get(cs.playerEntityId);
			if (!pe || !pe->inventory) continue;
			glm::vec3 center = pe->position + glm::vec3(0, 1, 0);
			auto pickups = m_world->entities.attractItemsToward(center, 3.0f, 1.2f, dt);
			for (auto* item : pickups) {
				std::string itemType = item->getProp<std::string>(Prop::ItemType);
				int count = item->getProp<int>(Prop::Count, 1);
				pe->inventory->add(itemType, count);
				if (m_callbacks.onItemPickup)
					m_callbacks.onItemPickup(item->position, {0.8f, 0.9f, 1.0f});
				item->removed = true;
			}
		}

		// Advance world time
		m_worldTime += (1.0f / 600.0f) * dt;
	}

	// Accessors
	World& world() { return *m_world; }
	const World& world() const { return *m_world; }
	float worldTime() const { return m_worldTime; }
	glm::vec3 spawnPos() const { return m_spawnPos; }
	bool isCreative() const { return m_creative; }

	EntityId getPlayerEntity(ClientId clientId) const {
		auto it = m_clients.find(clientId);
		return it != m_clients.end() ? it->second.playerEntityId : ENTITY_NONE;
	}

private:
	void resolveActions(float dt);

	std::unique_ptr<World> m_world;
	ServerCallbacks m_callbacks;
	bool m_creative = true;
	float m_worldTime = 0.30f;
	float m_activeBlockTimer = 0;
	glm::vec3 m_spawnPos = {30, 10, 30};
	glm::vec3 m_chestPos = {30, 10, 30};

	struct ClientState {
		EntityId playerEntityId;
	};
	std::unordered_map<ClientId, ClientState> m_clients;
};

} // namespace agentworld

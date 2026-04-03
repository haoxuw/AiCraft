#pragma once

/**
 * LocalServer — in-process GameServer for singleplayer.
 *
 * Creates and owns a GameServer. All calls are direct function calls
 * with zero network overhead. Used when the player hosts their own game.
 *
 * Each client runs one local server when creating a game.
 * Later, clients can join a global server instead.
 */

#include "shared/server_interface.h"
#include "server/server.h"
#include "server/world_template.h"
#include <vector>

namespace agentworld {

class LocalServer : public ServerInterface {
public:
	LocalServer(const std::vector<std::shared_ptr<WorldTemplate>>& templates)
		: m_templates(templates) {}

	void setCreatureType(const std::string& type) { m_creatureType = type; }

	bool createGame(int seed, int templateIndex,
	                const WorldGenConfig& wgc = WorldGenConfig{}) override {
		ServerConfig config;
		config.seed = seed;
		config.templateIndex = templateIndex;
		config.worldGenConfig = wgc;

		m_server = std::make_unique<GameServer>();
		m_server->init(config, m_templates);

		// Connect as the local player
		m_clientId = 1;
		m_playerId = m_server->addClient(m_clientId, m_creatureType);
		m_connected = true;
		return true;
	}

	// Create GameServer object without initializing a world (for loadWorld)
	void createServerOnly() {
		m_server = std::make_unique<GameServer>();
	}

	// After loadWorld() populates the server, connect the local player
	void finishLoad() {
		m_clientId = 1;
		m_playerId = m_server->addClient(m_clientId, m_creatureType);
		m_connected = true;
	}

	void disconnect() override {
		if (m_server && m_connected) {
			m_server->removeClient(m_clientId);
		}
		m_server.reset();
		m_connected = false;
		m_playerId = ENTITY_NONE;
	}

	bool isConnected() const override { return m_connected; }

	void tick(float dt) override {
		if (m_server) m_server->tick(dt);
	}

	void sendAction(const ActionProposal& action) override {
		if (m_server) m_server->receiveAction(m_clientId, action);
	}

	ChunkSource& chunks() override { return m_server->world(); }

	EntityId localPlayerId() const override { return m_playerId; }

	Entity* getEntity(EntityId id) override {
		return m_server ? m_server->world().entities.get(id) : nullptr;
	}

	void forEachEntity(std::function<void(Entity&)> fn) override {
		if (m_server) m_server->world().entities.forEach(fn);
	}

	size_t entityCount() const override {
		return m_server ? m_server->world().entities.count() : 0;
	}

	BehaviorInfo getBehaviorInfo(EntityId id) override {
		BehaviorInfo info;
		if (!m_server) return info;
		auto* state = m_server->world().entities.getBehaviorState(id);
		if (state && state->behavior) {
			info.name = state->behavior->name();
			info.sourceCode = state->behavior->sourceCode();
		}
		auto* entity = m_server->world().entities.get(id);
		if (entity) {
			info.goal = entity->goalText;
			info.hasError = entity->hasError;
			info.errorText = entity->errorText;
		}
		return info;
	}

	float worldTime() const override {
		return m_server ? m_server->worldTime() : 0.30f;
	}

	glm::vec3 spawnPos() const override {
		return m_server ? m_server->spawnPos() : glm::vec3(0, 10, 0);
	}
	float pickupRange() const override {
		return m_server ? m_server->worldGenConfig().pickupRange : 1.5f;
	}


	const BlockRegistry& blockRegistry() const override {
		return m_server->world().blocks;
	}

	ActionQueue& actionQueue() override {
		return m_server->world().actions;
	}

	void setEffectCallbacks(
		std::function<void(ChunkPos)> onChunkDirty,
		std::function<void(glm::vec3, glm::vec3, int)> onBlockBreak,
		std::function<void(glm::vec3, glm::vec3)> onItemPickup,
		std::function<void(glm::vec3, const std::string&)> onBlockPlace = nullptr
	) override {
		if (m_server) {
			// Update only the 4 effect fields — do NOT replace the entire struct.
			// Replacing the struct would wipe onBreakText, onPickupText, and
			// network broadcast callbacks that may have been set independently.
			auto& cb = m_server->callbacks();
			cb.onChunkDirty  = onChunkDirty;
			cb.onBlockBreak  = onBlockBreak;
			cb.onItemPickup  = onItemPickup;
			cb.onBlockPlace  = onBlockPlace;
		}
	}

	// Direct access to server (for things that haven't been abstracted yet)
	GameServer* server() { return m_server.get(); }

private:
	std::vector<std::shared_ptr<WorldTemplate>> m_templates;
	std::unique_ptr<GameServer> m_server;
	std::string m_creatureType; // selected creature type (empty = base:player)
	ClientId m_clientId = 0;
	EntityId m_playerId = ENTITY_NONE;
	bool m_connected = false;
};

} // namespace agentworld

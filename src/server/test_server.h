#pragma once

/**
 * TestServer — lightweight in-process GameServer harness for unit tests.
 *
 * Wraps GameServer directly (no TCP, no ServerInterface). Only used by
 * test code — never by the game client.
 */

#include "server/server.h"
#include "server/world_template.h"
#include <vector>
#include <functional>

namespace agentica {

class TestServer {
public:
	TestServer(const std::vector<std::shared_ptr<WorldTemplate>>& templates)
		: m_templates(templates) {}

	void setCreatureType(const std::string& type) { m_creatureType = type; }

	bool createGame(int seed, int templateIndex,
	                const WorldGenConfig& wgc = WorldGenConfig{}) {
		ServerConfig config;
		config.seed = seed;
		config.templateIndex = templateIndex;
		config.worldGenConfig = wgc;
		m_server = std::make_unique<GameServer>();
		m_server->init(config, m_templates);
		m_clientId = 1;
		m_playerId = m_server->addClient(m_clientId, m_creatureType);
		return true;
	}

	void sendAction(const ActionProposal& action) {
		if (m_server) m_server->receiveAction(m_clientId, action);
	}

	void tick(float dt) { if (m_server) m_server->tick(dt); }

	EntityId localPlayerId() const { return m_playerId; }

	Entity* getEntity(EntityId id) {
		return m_server ? m_server->world().entities.get(id) : nullptr;
	}

	void forEachEntity(std::function<void(Entity&)> fn) {
		if (m_server) m_server->world().entities.forEach(fn);
	}

	size_t entityCount() const {
		return m_server ? m_server->world().entities.count() : 0;
	}

	ChunkSource& chunks() { return m_server->world(); }
	const BlockRegistry& blockRegistry() const { return m_server->world().blocks; }
	glm::vec3 spawnPos() const { return m_server ? m_server->spawnPos() : glm::vec3(0,10,0); }

	// Direct access to underlying GameServer (for test-only operations)
	GameServer* server() { return m_server.get(); }

private:
	std::vector<std::shared_ptr<WorldTemplate>> m_templates;
	std::unique_ptr<GameServer> m_server;
	std::string m_creatureType;
	ClientId m_clientId = 1;
	EntityId m_playerId = ENTITY_NONE;
};

} // namespace agentica

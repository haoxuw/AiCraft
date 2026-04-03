#pragma once

/**
 * NetworkServer — connects to a remote GameServer via TCP.
 *
 * Implements ServerInterface so the Game class can use it
 * identically to LocalServer. Sends ActionProposals over TCP,
 * receives entity/chunk/time updates.
 *
 * Used when a dedicated server is already running.
 * The client generates a random UUID as its player name.
 */

#include "shared/server_interface.h"
#include "shared/net_socket.h"
#include "shared/net_protocol.h"
#include "shared/block_registry.h"
#include "shared/chunk.h"
#include "server/entity_manager.h"
#include "content/builtin.h"
#include <unordered_map>
#include <functional>
#include <string>
#include <random>
#include <thread>
#include <chrono>

namespace agentworld {

class NetworkServer : public ServerInterface {
public:
	NetworkServer(const std::string& host, int port)
		: m_host(host), m_port(port) {
		// Register all block and entity definitions (same as server)
		// so entities have proper collision boxes, HP, inventory, etc.
		registerAllBuiltins(m_blocks, m_entityDefs);

		// Generate random UUID for this client
		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_int_distribution<uint32_t> dist;
		char buf[40];
		snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%08x%04x",
			dist(gen), dist(gen) & 0xFFFF, dist(gen) & 0xFFFF,
			dist(gen) & 0xFFFF, dist(gen), dist(gen) & 0xFFFF);
		m_clientUUID = buf;
		printf("[Net] Client UUID: %s\n", m_clientUUID.c_str());
	}

	bool createGame(int seed, int templateIndex, bool creative) override {
		if (!m_tcp.connect(m_host.c_str(), m_port)) {
			printf("[Net] Cannot connect to %s:%d\n", m_host.c_str(), m_port);
			return false;
		}

		m_connected = true;
		m_creative = creative;

		// Wait for S_WELCOME (polling for up to 3 seconds)
		auto start = std::chrono::steady_clock::now();
		while (true) {
			m_recv.readFrom(m_tcp.fd()); // non-blocking, may return EAGAIN

			net::MsgHeader hdr;
			std::vector<uint8_t> payload;
			if (m_recv.tryExtract(hdr, payload)) {
				if (hdr.type == net::S_WELCOME) {
					net::ReadBuffer rb(payload.data(), payload.size());
					m_localPlayerId = rb.readU32();
					m_spawnPos = rb.readVec3();
					printf("[Net] Welcome! Player ID=%u, spawn=(%.1f,%.1f,%.1f)\n",
						m_localPlayerId, m_spawnPos.x, m_spawnPos.y, m_spawnPos.z);

					// Identify ourselves to the server
					net::WriteBuffer hello;
					hello.writeString(m_clientUUID);
					net::sendMessage(m_tcp.fd(), net::C_HELLO, hello);

					return true;
				}
			}

			auto elapsed = std::chrono::steady_clock::now() - start;
			if (std::chrono::duration<float>(elapsed).count() > 3.0f) {
				printf("[Net] Timeout waiting for welcome\n");
				break;
			}

			// Don't spin at 100% CPU while waiting
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		disconnect();
		return false;
	}

	void disconnect() override {
		m_tcp.disconnect();
		m_connected = false;
	}

	bool isConnected() const override { return m_connected; }

	void tick(float dt) override {
		if (!m_connected) return;

		// Read incoming messages
		if (!m_recv.readFrom(m_tcp.fd())) {
			printf("[Net] Connection lost\n");
			m_connected = false;
			return;
		}

		net::MsgHeader hdr;
		std::vector<uint8_t> payload;
		while (m_recv.tryExtract(hdr, payload)) {
			handleMessage(hdr.type, payload);
		}

		// Interpolate ALL entities toward server targets — same code for
		// player, animals, villagers, other players. No special cases.
		// Server is authoritative for positions. Client smoothly tracks.
		const float INTERP_SPEED = 18.0f; // fast tracking for responsive feel
		for (auto& [id, target] : m_interpTargets) {
			auto it = m_entities.find(id);
			if (it == m_entities.end()) continue;
			auto& e = *it->second;

			// For local player, use WASD velocity (already set by gameplay.cpp)
			// for better extrapolation. For others, use server velocity.
			glm::vec3 useVel = (id == m_localPlayerId) ? e.velocity : target.velocity;

			// Predicted position = server pos + velocity × time since update
			glm::vec3 predicted = target.position + useVel * target.age;
			glm::vec3 diff = predicted - e.position;
			float dist = glm::length(diff);

			if (dist > 8.0f) {
				e.position = predicted; // snap if too far
			} else if (dist > 0.005f) {
				float t = std::min(dt * INTERP_SPEED, 1.0f);
				e.position += diff * t;
			}

			// Smooth yaw — same for all entities (server-authoritative)
			{
				float yawDiff = target.yaw - e.yaw;
				while (yawDiff > 180.0f) yawDiff -= 360.0f;
				while (yawDiff < -180.0f) yawDiff += 360.0f;
				e.yaw += yawDiff * std::min(dt * INTERP_SPEED, 1.0f);
			}
			// Velocity: skip for local player (client-side prediction handles it)
			if (id != m_localPlayerId) {
				e.velocity = target.velocity;
			}

			target.age += dt;

			// Walk distance for animation — same for all entities
			float hSpeed = std::sqrt(e.velocity.x * e.velocity.x + e.velocity.z * e.velocity.z);
			if (hSpeed > 0.01f) {
				float wd = e.getProp<float>(Prop::WalkDistance, 0.0f);
				e.setProp(Prop::WalkDistance, wd + hSpeed * dt);
			}
		}
	}

	void sendAction(const ActionProposal& action) override {
		if (!m_connected) return;
		net::WriteBuffer buf;
		net::serializeAction(buf, action);
		net::sendMessage(m_tcp.fd(), net::C_ACTION, buf);
	}

	// --- State access ---
	ChunkSource& chunks() override { return m_chunks; }
	EntityId localPlayerId() const override { return m_localPlayerId; }

	Entity* getEntity(EntityId id) override {
		auto it = m_entities.find(id);
		return it != m_entities.end() ? it->second.get() : nullptr;
	}

	void forEachEntity(std::function<void(Entity&)> fn) override {
		for (auto& [id, e] : m_entities)
			if (!e->removed) fn(*e);
	}

	size_t entityCount() const override { return m_entities.size(); }

	BehaviorInfo getBehaviorInfo(EntityId) override { return {}; }
	float worldTime() const override { return m_worldTime; }
	glm::vec3 spawnPos() const override { return m_spawnPos; }
	bool isCreative() const override { return m_creative; }
	const BlockRegistry& blockRegistry() const override { return m_blocks; }
	ActionQueue& actionQueue() override { return m_actions; }

	void setEffectCallbacks(
		std::function<void(ChunkPos)> onChunkDirty,
		std::function<void(glm::vec3, glm::vec3, int)> onBlockBreak,
		std::function<void(glm::vec3, glm::vec3)> onItemPickup,
		std::function<void(glm::vec3, const std::string&)> onBlockPlace = nullptr) override {
		m_onChunkDirty = onChunkDirty;
		m_onBlockBreak = onBlockBreak;
		m_onItemPickup = onItemPickup;
		m_onBlockPlace = onBlockPlace;
	}

	const std::string& clientUUID() const { return m_clientUUID; }

	// Try to connect (non-blocking check for game.cpp)
	static bool canConnect(const char* host, int port) {
		net::TcpClient probe;
		bool ok = probe.connect(host, port);
		probe.disconnect();
		return ok;
	}

private:
	void handleMessage(uint32_t type, const std::vector<uint8_t>& payload) {
		net::ReadBuffer rb(payload.data(), payload.size());

		switch (type) {
		case net::S_ENTITY: {
			auto es = net::deserializeEntityState(rb);

			auto it = m_entities.find(es.id);
			if (it == m_entities.end()) {
				// New entity — look up proper EntityDef from registered builtins
				const EntityDef* def = m_entityDefs.getTypeDef(es.typeId);
				if (!def) {
					printf("[Net] WARNING: unknown entity type '%s' (id=%u), using default def\n",
					       es.typeId.c_str(), es.id);
					def = &m_defaultDef;
				}
				auto ent = std::make_unique<Entity>(es.id, es.typeId, *def);
				ent->position = es.position;
				ent->velocity = es.velocity;
				ent->yaw = es.yaw;
				ent->onGround = es.onGround;
				ent->goalText = es.goalText;
				if (def->max_hp > 0)
					ent->setProp(Prop::HP, es.hp);
				m_entities[es.id] = std::move(ent);
				// Initialize interpolation target
				m_interpTargets[es.id] = {es.position, es.velocity, es.yaw, 0};
			} else {
				auto& e = *it->second;

				// ALL entities use the same code path — server is authoritative.
				// Set interpolation target from server data. The tick() loop
				// smoothly interpolates every entity toward its target.
				// No special case for local player vs animals vs other players.
				m_interpTargets[es.id] = {es.position, es.velocity, es.yaw, 0.0f};

				// Sync non-positional state immediately
				e.onGround = es.onGround;
				e.goalText = es.goalText;
				if (e.def().max_hp > 0)
					e.setProp(Prop::HP, es.hp);
			}
			break;
		}
		case net::S_CHUNK: {
			int cx = rb.readI32(), cy = rb.readI32(), cz = rb.readI32();
			ChunkPos cp = {cx, cy, cz};
			auto chunk = std::make_unique<Chunk>();
			for (int ly = 0; ly < CHUNK_SIZE; ly++)
				for (int lz = 0; lz < CHUNK_SIZE; lz++)
					for (int lx = 0; lx < CHUNK_SIZE; lx++)
						chunk->set(lx, ly, lz, (BlockId)rb.readU32());
			m_chunkData[cp] = std::move(chunk);
			if (m_onChunkDirty) m_onChunkDirty(cp);
			break;
		}
		case net::S_REMOVE: {
			EntityId id = rb.readU32();
			m_entities.erase(id);
			m_interpTargets.erase(id);
			break;
		}
		case net::S_TIME: {
			m_worldTime = rb.readF32();
			break;
		}
		case net::S_BLOCK: {
			int bx = rb.readI32(), by = rb.readI32(), bz = rb.readI32();
			BlockId bid = (BlockId)rb.readU32();
			auto div = [](int a, int b) { return (a >= 0) ? a / b : (a - b + 1) / b; };
			ChunkPos cp = {div(bx, CHUNK_SIZE), div(by, CHUNK_SIZE), div(bz, CHUNK_SIZE)};
			auto it = m_chunkData.find(cp);
			if (it != m_chunkData.end()) {
				int lx = ((bx % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
				int ly = ((by % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
				int lz = ((bz % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
				it->second->set(lx, ly, lz, bid);
			}
			if (m_onChunkDirty) m_onChunkDirty(cp);
			break;
		}
		case net::S_INVENTORY: {
			EntityId id = rb.readU32();
			auto it = m_entities.find(id);
			if (it != m_entities.end() && it->second->inventory) {
				auto& inv = *it->second->inventory;
				inv.clear();
				uint32_t count = rb.readU32();
				for (uint32_t i = 0; i < count && rb.hasMore(); i++) {
					std::string itemId = rb.readString();
					int amount = rb.readI32();
					inv.add(itemId, amount);
				}
				inv.autoPopulateHotbar();
			}
			break;
		}
		}
	}

	// Chunk source that reads from received chunk data
	class NetChunkSource : public ChunkSource {
	public:
		NetChunkSource(NetworkServer& ns) : m_ns(ns) {}
		Chunk* getChunk(ChunkPos pos) override {
			auto it = m_ns.m_chunkData.find(pos);
			return it != m_ns.m_chunkData.end() ? it->second.get() : nullptr;
		}
		Chunk* getChunkIfLoaded(ChunkPos pos) override { return getChunk(pos); }
		BlockId getBlock(int x, int y, int z) override {
			auto div = [](int a, int b) { return (a >= 0) ? a / b : (a - b + 1) / b; };
			ChunkPos cp = {div(x, CHUNK_SIZE), div(y, CHUNK_SIZE), div(z, CHUNK_SIZE)};
			Chunk* c = getChunk(cp);
			if (!c) return BLOCK_AIR;
			int lx = ((x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			int ly = ((y % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			int lz = ((z % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			return c->get(lx, ly, lz);
		}
		const BlockRegistry& blockRegistry() const override { return m_ns.m_blocks; }
	private:
		NetworkServer& m_ns;
	};

	std::string m_host;
	int m_port;
	std::string m_clientUUID;
	bool m_connected = false;
	bool m_creative = false;

	net::TcpClient m_tcp;
	net::RecvBuffer m_recv;

	EntityId m_localPlayerId = ENTITY_NONE;
	glm::vec3 m_spawnPos = {0, 0, 0};
	float m_worldTime = 0.3f;

	std::unordered_map<EntityId, std::unique_ptr<Entity>> m_entities;
	std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, ChunkPosHash> m_chunkData;
	NetChunkSource m_chunks{*this};

	// Interpolation: smooth remote entity movement between 20Hz server updates
	struct InterpTarget {
		glm::vec3 position;
		glm::vec3 velocity;
		float yaw;
		float age; // time since last server update
	};
	std::unordered_map<EntityId, InterpTarget> m_interpTargets;

	BlockRegistry m_blocks;
	ActionQueue m_actions;
	EntityDef m_defaultDef;
	EntityManager m_entityDefs;  // holds type defs for entity creation

	std::function<void(ChunkPos)> m_onChunkDirty;
	std::function<void(glm::vec3, glm::vec3, int)> m_onBlockBreak;
	std::function<void(glm::vec3, glm::vec3)> m_onItemPickup;
	std::function<void(glm::vec3, const std::string&)> m_onBlockPlace;
};

} // namespace agentworld

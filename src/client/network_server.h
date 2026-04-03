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

	bool createGame(int seed, int templateIndex, bool creative,
	                const WorldGenConfig& /*wgc*/ = WorldGenConfig{}) override {
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
		int msgCount = 0;
		while (m_recv.tryExtract(hdr, payload)) {
			handleMessage(hdr.type, payload);
			msgCount++;
		}
		m_tickMsgCount += msgCount;
		m_totalMsgCount += msgCount;
		m_uptime += dt;

		// Early connection verbose logging (first 10 seconds)
		if (m_uptime < 10.0f && msgCount > 0) {
			printf("[Net] t=%.1fs: received %d msgs, %zu entities, %zu chunks\n",
				m_uptime, msgCount, m_entities.size(), m_chunkData.size());
			Entity* pe = getEntity(m_localPlayerId);
			if (!pe)
				printf("[Net] WARNING: local player entity %u not received yet\n", m_localPlayerId);
		}

		// Periodic diagnostics (every 5 seconds)
		m_diagTimer += dt;
		if (m_diagTimer >= 5.0f) {
			Entity* pe = m_entities.count(m_localPlayerId)
				? m_entities[m_localPlayerId].get() : nullptr;
			auto* pt = m_interpTargets.count(m_localPlayerId)
				? &m_interpTargets[m_localPlayerId] : nullptr;

			printf("[Net] Stats: %d msgs/5s (total=%d), %zu entities, %zu chunks, uptime=%.0fs\n",
				m_tickMsgCount, m_totalMsgCount, m_entities.size(), m_chunkData.size(), m_uptime);
			if (pe && pt) {
				glm::vec3 diff = pt->position - pe->position;
				printf("[Net] Player %u: pos=(%.1f,%.1f,%.1f) srv=(%.1f,%.1f,%.1f) diff=%.2f vel=(%.1f,%.1f,%.1f) ground=%d\n",
					m_localPlayerId,
					pe->position.x, pe->position.y, pe->position.z,
					pt->position.x, pt->position.y, pt->position.z,
					glm::length(diff),
					pe->velocity.x, pe->velocity.y, pe->velocity.z,
					pe->onGround);
			} else {
				printf("[Net] Player %u: entity=%s, interp=%s\n",
					m_localPlayerId, pe ? "yes" : "NO", pt ? "yes" : "NO");
			}
			m_diagTimer = 0;
			m_tickMsgCount = 0;
		}

		// Interpolate ALL entities toward server targets.
		// Server is fully authoritative for positions. Client smoothly tracks.
		// NO special cases for local player — this prevents phasing through
		// walls (client has no collision, so using client velocity for
		// extrapolation causes rubber-banding against solid blocks).
		const float INTERP_SPEED = 18.0f;
		const float LOCAL_INTERP_SPEED = 25.0f; // faster for local player responsiveness
		for (auto& [id, target] : m_interpTargets) {
			auto it = m_entities.find(id);
			if (it == m_entities.end()) continue;
			auto& e = *it->second;
			bool isLocal = (id == m_localPlayerId);

			// ALL entities use server velocity for prediction — client has
			// no physics so client velocity would phase through blocks
			glm::vec3 predicted = target.position + target.velocity * target.age;
			glm::vec3 diff = predicted - e.position;
			float dist = glm::length(diff);

			float speed = isLocal ? LOCAL_INTERP_SPEED : INTERP_SPEED;
			if (dist > 8.0f) {
				e.position = predicted;
				printf("[Net] SNAP entity %u to server pos (dist=%.1f) local=(%.1f,%.1f,%.1f) srv=(%.1f,%.1f,%.1f)\n",
					id, dist, e.position.x, e.position.y, e.position.z,
					predicted.x, predicted.y, predicted.z);
			} else if (dist > 0.005f) {
				float t = std::min(dt * speed, 1.0f);
				e.position += diff * t;
			}

			// Smooth yaw — same for all entities (server-authoritative)
			{
				float yawDiff = target.yaw - e.yaw;
				while (yawDiff > 180.0f) yawDiff -= 360.0f;
				while (yawDiff < -180.0f) yawDiff += 360.0f;
				e.yaw += yawDiff * std::min(dt * speed, 1.0f);
			}
			// Velocity: update for ALL entities (server-authoritative)
			e.velocity = target.velocity;

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
				printf("[Net] New entity: id=%u type=%s pos=(%.1f,%.1f,%.1f)%s\n",
					es.id, es.typeId.c_str(), es.position.x, es.position.y, es.position.z,
					es.id == m_localPlayerId ? " [LOCAL PLAYER]" : "");
				auto ent = std::make_unique<Entity>(es.id, es.typeId, *def);
				ent->position = es.position;
				ent->velocity = es.velocity;
				ent->yaw = es.yaw;
				ent->onGround = es.onGround;
				ent->goalText = es.goalText;
				if (es.id == m_localPlayerId)
					printf("[Net] Local player entity created: type=%s pos=(%.1f,%.1f,%.1f)\n",
						es.typeId.c_str(), es.position.x, es.position.y, es.position.z);
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

	// Diagnostics
	float m_diagTimer = 0;
	int m_tickMsgCount = 0;
	int m_totalMsgCount = 0;
	float m_uptime = 0;

	std::function<void(ChunkPos)> m_onChunkDirty;
	std::function<void(glm::vec3, glm::vec3, int)> m_onBlockBreak;
	std::function<void(glm::vec3, glm::vec3)> m_onItemPickup;
	std::function<void(glm::vec3, const std::string&)> m_onBlockPlace;
};

} // namespace agentworld

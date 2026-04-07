#pragma once

/**
 * AgentClient — headless AI client that controls one or more entities.
 *
 * Connects to a GameServer via TCP (same protocol as GUI client).
 * Receives entity/chunk state updates, runs Python behavior decide()
 * at 4Hz, and sends ActionProposals back to the server.
 *
 * From the server's perspective, an agent client is indistinguishable
 * from a human player — it just sends C_ACTION messages.
 */

#include "shared/types.h"
#include "shared/entity.h"
#include "shared/chunk.h"
#include "shared/block_registry.h"
#include "shared/net_socket.h"
#include "shared/net_protocol.h"
#include <zstd.h>
#include "shared/constants.h"
#include "server/behavior.h"
#include "server/behavior_store.h"
#include "server/python_bridge.h"
#include "agent/behavior_executor.h"
#include "server/entity_manager.h"
#include "content/builtin.h"
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <ctime>

namespace modcraft {

class AgentClient {
public:
	AgentClient() {
		// Register entity type definitions (same as server/client)
		registerAllBuiltins(m_blocks, m_entityDefs);
	}

	// Set the entity ID this agent wants to control (call before connect).
	void setTargetEntity(EntityId id) { m_targetEntityId = id; }

	// Connect to server. Returns true on success.
	bool connect(const std::string& host, int port, const std::string& name = "agent") {
		m_host = host;
		m_port = port;
		m_name = name;

		if (!m_tcp.connect(host.c_str(), port)) {
			printf("[Agent:%s] Cannot connect to %s:%d\n", name.c_str(), host.c_str(), port);
			return false;
		}
		m_connected = true;

		// Wait for S_WELCOME
		auto start = std::chrono::steady_clock::now();
		while (true) {
			m_recv.readFrom(m_tcp.fd());
			net::MsgHeader hdr;
			std::vector<uint8_t> payload;
			if (m_recv.tryExtract(hdr, payload)) {
				if (hdr.type == net::S_WELCOME) {
					net::ReadBuffer rb(payload.data(), payload.size());
					uint32_t tempId = rb.readU32();
					rb.readVec3(); // spawn pos (unused by agent)
					printf("[Agent:%s] Connected (temp entity %u).\n",
						m_name.c_str(), tempId);

					// Identify as agent client with target entity
					net::WriteBuffer hello;
					hello.writeString(m_name);
					hello.writeU32(m_targetEntityId);
					net::sendMessage(m_tcp.fd(), net::C_AGENT_HELLO, hello);
					return true;
				}
			}

			auto elapsed = std::chrono::steady_clock::now() - start;
			if (std::chrono::duration<float>(elapsed).count() > 3.0f) {
				printf("[Agent:%s] Timeout waiting for welcome\n", m_name.c_str());
				disconnect();
				return false;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}

	void disconnect() {
		m_tcp.disconnect();
		m_connected = false;
	}

	bool isConnected() const { return m_connected; }
	bool hasControlledEntities() const { return !m_controlled.empty(); }

	// Assign an entity for this agent to control.
	void assignEntity(EntityId id, const std::string& behaviorId) {
		if (m_controlled.count(id)) return;
		m_controlled.insert(id);
		printf("[Agent:%s] Now controlling entity %u (behavior: %s)\n",
			m_name.c_str(), id, behaviorId.c_str());
	}

	// Revoke control of an entity.
	void revokeEntity(EntityId id) {
		m_controlled.erase(id);
		m_behaviorStates.erase(id);
		m_blockCaches.erase(id);
		printf("[Agent:%s] Revoked entity %u\n", m_name.c_str(), id);
	}

	// Main tick: receive state, run behaviors, send actions.
	void tick(float dt) {
		if (!m_connected) return;

		// 1. Receive all pending messages from server
		if (!receiveMessages()) {
			printf("[Agent:%s] Disconnected from server\n", m_name.c_str());
			m_connected = false;
			return;
		}

		// 2. Run behavior decisions for controlled entities
		for (EntityId eid : m_controlled) {
			auto it = m_entities.find(eid);
			if (it == m_entities.end() || !it->second) continue;
			Entity& e = *it->second;
			if (e.removed) continue;

			auto& state = m_behaviorStates[eid];

			// Load behavior on first use
			if (!state.behavior) {
				std::string behaviorId = e.getProp<std::string>(Prop::BehaviorId, "");
				state.behavior = loadBehavior(behaviorId);
			}

			// Decide at 4 Hz
			state.decideTimer -= dt;
			if (state.decideTimer <= 0) {
				state.decideTimer = 0.25f;

				auto nearby = gatherNearby(e, m_entities, 64.0f);

				BlockTypeFn blockQuery = [&](int x, int y, int z) -> std::string {
					BlockId bid = getBlock(x, y, z);
					return m_blocks.get(bid).string_id;
				};
				// Use entity's work_radius prop as block scan radius (capped at 100).
				// Defaults to 80 which covers most tree distances from a village.
				int blockScanRadius = std::min(100,
					(int)e.getProp<float>("work_radius", 80.0f));
				auto blocks = getKnownBlocks(e, blockScanRadius, blockQuery, m_blockCaches[eid]);

				BehaviorWorldView view{e, nearby, blocks, dt, m_worldTime};
				state.currentAction = state.behavior->decide(view);

				// Log only when goal differs from the last LOGGED goal for this entity
				if (!e.goalText.empty() && e.goalText != m_lastLoggedGoal[eid]) {
					m_lastLoggedGoal[eid] = e.goalText;
					time_t now = time(nullptr);
					struct tm* ti = localtime(&now);
					char ts[10];
					strftime(ts, sizeof(ts), "%H:%M:%S", ti);
					// "base:chicken" -> "Chicken"
					std::string typeName = e.typeId();
					auto col = typeName.find(':');
					if (col != std::string::npos) typeName = typeName.substr(col + 1);
					if (!typeName.empty()) typeName[0] = (char)toupper((unsigned char)typeName[0]);
					printf("[%s][Agent:%s] %s #%u: %s\n",
						ts, m_name.c_str(), typeName.c_str(), eid, e.goalText.c_str());
				}

				// Extract one-shot actions (DropItem, BreakBlock) — sent exactly once
				extractOneShots(e, state.currentAction, state.pendingOneShots);
			}

			// Send pending one-shot actions (queued by decide, drained here)
			for (auto& p : state.pendingOneShots) {
				net::WriteBuffer wb;
				net::serializeAction(wb, p);
				net::sendMessage(m_tcp.fd(), net::C_ACTION, wb);
			}
			state.pendingOneShots.clear();

			// Send continuous Move action every tick (smooth movement at 50Hz)
			// Attach current goal text so the server can broadcast it to clients.
			std::vector<ActionProposal> moveProposals;
			behaviorToActionProposals(e, state, state.currentAction, dt, moveProposals);
			for (auto& p : moveProposals) {
				p.goalText = e.goalText;
				net::WriteBuffer wb;
				net::serializeAction(wb, p);
				net::sendMessage(m_tcp.fd(), net::C_ACTION, wb);
			}
		}

		// Periodic heartbeat: show all controlled entities and their current goal
		m_statusTimer += dt;
		if (m_statusTimer >= 10.0f) {
			m_statusTimer = 0.0f;
			printf("[Agent:%s] Heartbeat —", m_name.c_str());
			for (EntityId eid : m_controlled) {
				auto it = m_entities.find(eid);
				if (it == m_entities.end() || !it->second) continue;
				printf(" [%u: %s]", eid,
					it->second->goalText.empty() ? "waiting" : it->second->goalText.c_str());
			}
			printf("\n");
		}
	}

private:
	// Receive and process all pending network messages.
	bool receiveMessages() {
		if (!m_recv.readFrom(m_tcp.fd()))
			return false;

		net::MsgHeader hdr;
		std::vector<uint8_t> payload;
		while (m_recv.tryExtract(hdr, payload)) {
			handleMessage(hdr.type, payload);
		}
		return true;
	}

	void handleMessage(uint32_t type, const std::vector<uint8_t>& payload) {
		net::ReadBuffer rb(payload.data(), payload.size());

		switch (type) {
		case net::S_ENTITY: {
			auto es = net::deserializeEntityState(rb);
			auto it = m_entities.find(es.id);
			if (it == m_entities.end()) {
				// New entity
				const EntityDef* def = m_entityDefs.getTypeDef(es.typeId);
				if (!def) def = &m_defaultDef;
				auto ent = std::make_unique<Entity>(es.id, es.typeId, *def);
				ent->position = es.position;
				ent->velocity = es.velocity;
				ent->yaw = es.yaw;
				ent->onGround = es.onGround;
				ent->goalText = es.goalText;
				if (def->isLiving())
					ent->setProp(Prop::HP, es.hp);
				for (auto& [k, v] : es.stringProps)
					ent->setProp(k, v);
				m_entities[es.id] = std::move(ent);

				// Auto-assign if this entity has a BehaviorId and we're supposed to control it
				// (For now, agent controls entities assigned via command line or S_ASSIGN_ENTITY)
			} else {
				// Update existing entity
				auto& e = *it->second;
				e.position = es.position;
				e.velocity = es.velocity;
				e.yaw = es.yaw;
				e.onGround = es.onGround;
				e.goalText = es.goalText;
				if (e.def().isLiving())
					e.setProp(Prop::HP, es.hp);
				for (auto& [k, v] : es.stringProps)
					e.setProp(k, v);
			}
			break;
		}
		case net::S_CHUNK: {
			int cx = rb.readI32(), cy = rb.readI32(), cz = rb.readI32();
			ChunkPos cp = {cx, cy, cz};
			auto chunk = std::make_unique<Chunk>();
			for (int ly = 0; ly < CHUNK_SIZE; ly++)
				for (int lz = 0; lz < CHUNK_SIZE; lz++)
					for (int lx = 0; lx < CHUNK_SIZE; lx++) {
						uint32_t v = rb.readU32();
						chunk->set(lx, ly, lz, (BlockId)(v & 0xFFFF), (uint8_t)((v >> 16) & 0xFF));
					}
			m_chunks[cp] = std::move(chunk);
			break;
		}
		case net::S_CHUNK_Z: {
			size_t compSize = rb.remaining();
			const uint8_t* compData = rb.remainingData();
			size_t decompBound = ZSTD_getFrameContentSize(compData, compSize);
			if (decompBound == ZSTD_CONTENTSIZE_ERROR || decompBound == ZSTD_CONTENTSIZE_UNKNOWN) {
				fprintf(stderr, "[Agent] S_CHUNK_Z: bad zstd frame\n");
				break;
			}
			std::vector<uint8_t> decomp(decompBound);
			size_t actual = ZSTD_decompress(decomp.data(), decompBound, compData, compSize);
			if (ZSTD_isError(actual)) {
				fprintf(stderr, "[Agent] S_CHUNK_Z: decompression error: %s\n",
				        ZSTD_getErrorName(actual));
				break;
			}
			net::ReadBuffer zrb(decomp.data(), actual);
			ChunkPos cp = {zrb.readI32(), zrb.readI32(), zrb.readI32()};
			auto chunk = std::make_unique<Chunk>();
			for (int ly = 0; ly < CHUNK_SIZE; ly++)
				for (int lz = 0; lz < CHUNK_SIZE; lz++)
					for (int lx = 0; lx < CHUNK_SIZE; lx++) {
						uint32_t v = zrb.readU32();
						chunk->set(lx, ly, lz, (BlockId)(v & 0xFFFF), (uint8_t)((v >> 16) & 0xFF));
					}
			m_chunks[cp] = std::move(chunk);
			break;
		}
		case net::S_CHUNK_EVICT: {
			ChunkPos cp = {rb.readI32(), rb.readI32(), rb.readI32()};
			m_chunks.erase(cp);
			break;
		}
		case net::S_REMOVE: {
			EntityId id = rb.readU32();
			m_entities.erase(id);
			revokeEntity(id);
			break;
		}
		case net::S_INVENTORY: {
			// Server broadcasts full inventory for an entity after any change.
			// Agent updates local entity so decide() sees current item counts.
			EntityId eid = rb.readU32();
			uint32_t n = rb.readU32();
			std::vector<std::pair<std::string, int>> items;
			items.reserve(n);
			for (uint32_t i = 0; i < n; i++) {
				std::string itemId = rb.readString();
				int count = rb.readI32();
				items.push_back({itemId, count});
			}
			// Skip hotbar strings (10 slots) + equipment strings (WEAR_SLOT_COUNT=5)
			for (int i = 0; i < Inventory::HOTBAR_SLOTS + WEAR_SLOT_COUNT; i++)
				if (rb.hasMore()) rb.readString();

			auto it = m_entities.find(eid);
			if (it != m_entities.end() && it->second->inventory) {
				it->second->inventory->clear();
				for (auto& [itemId, count] : items)
					if (count > 0) it->second->inventory->add(itemId, count);
			}
			break;
		}
		case net::S_TIME: {
			m_worldTime = rb.readF32();
			break;
		}
		case net::S_BLOCK: {
			int bx = rb.readI32(), by = rb.readI32(), bz = rb.readI32();
			BlockId bid = (BlockId)rb.readU32();
			uint8_t p2 = rb.hasMore() ? rb.readU8() : 0;
			auto div = [](int a, int b) { return (a >= 0) ? a / b : (a - b + 1) / b; };
			ChunkPos cp = {div(bx, CHUNK_SIZE), div(by, CHUNK_SIZE), div(bz, CHUNK_SIZE)};
			auto it = m_chunks.find(cp);
			if (it != m_chunks.end()) {
				int lx = ((bx % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
				int ly = ((by % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
				int lz = ((bz % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
				it->second->set(lx, ly, lz, bid, p2);
			}
			break;
		}
		case net::S_ASSIGN_ENTITY: {
			EntityId eid = rb.readU32();
			std::string behaviorId = rb.readString();
			printf("[Agent:%s] Server assigned entity %u (behavior: %s)\n",
				m_name.c_str(), eid, behaviorId.c_str());
			assignEntity(eid, behaviorId);
			break;
		}
		case net::S_REVOKE_ENTITY: {
			EntityId eid = rb.readU32();
			printf("[Agent:%s] Server revoked entity %u\n", m_name.c_str(), eid);
			revokeEntity(eid);
			break;
		}
		case net::S_RELOAD_BEHAVIOR: {
			EntityId eid = rb.readU32();
			std::string newSource = rb.readString();
			printf("[Agent:%s] Reloading behavior for entity %u\n", m_name.c_str(), eid);
			reloadBehavior(eid, newSource);
			break;
		}
		default:
			// Unknown/unhandled server message — log so we notice missing handlers.
			fprintf(stderr, "[Agent:%s] WARNING: unhandled server message type 0x%04X (%zu bytes) — check agent_client.h\n",
				m_name.c_str(), type, rb.remaining());
			break;
		}
	}

	// Look up a block from local chunk cache
	BlockId getBlock(int x, int y, int z) const {
		auto div = [](int a, int b) { return (a >= 0) ? a / b : (a - b + 1) / b; };
		ChunkPos cp = {div(x, CHUNK_SIZE), div(y, CHUNK_SIZE), div(z, CHUNK_SIZE)};
		auto it = m_chunks.find(cp);
		if (it == m_chunks.end() || !it->second) return BLOCK_AIR;
		int lx = ((x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
		int ly = ((y % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
		int lz = ((z % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
		return it->second->get(lx, ly, lz);
	}

	// Load a Python behavior by name
	std::unique_ptr<Behavior> loadBehavior(const std::string& behaviorId) {
		if (behaviorId.empty())
			return std::make_unique<IdleFallbackBehavior>();

		if (!m_behaviorStore.isInitialized())
			m_behaviorStore.init();

		std::string source = m_behaviorStore.load(behaviorId);
		if (source.empty()) {
			printf("[Agent:%s] No .py file for behavior '%s'\n",
				m_name.c_str(), behaviorId.c_str());
			return std::make_unique<IdleFallbackBehavior>();
		}

		auto& bridge = pythonBridge();
		if (!bridge.isInitialized())
			return std::make_unique<IdleFallbackBehavior>();

		std::string error;
		auto handle = bridge.loadBehavior(source, error);
		if (handle < 0) {
			printf("[Agent:%s] Failed to load behavior '%s': %s\n",
				m_name.c_str(), behaviorId.c_str(), error.c_str());
			return std::make_unique<IdleFallbackBehavior>();
		}

		printf("[Agent:%s] Loaded behavior '%s'\n", m_name.c_str(), behaviorId.c_str());
		return std::make_unique<PythonBehavior>(handle, source);
	}

	// Reload behavior for an entity with new source code.
	void reloadBehavior(EntityId eid, const std::string& newSource) {
		auto& bridge = pythonBridge();
		if (!bridge.isInitialized()) return;

		std::string error;
		auto handle = bridge.loadBehavior(newSource, error);
		if (handle >= 0) {
			m_behaviorStates[eid].behavior = std::make_unique<PythonBehavior>(handle, newSource);
			m_behaviorStates[eid].decideTimer = 0; // decide immediately
			printf("[Agent:%s] Behavior reloaded for entity %u\n", m_name.c_str(), eid);
		} else {
			printf("[Agent:%s] Behavior reload failed for entity %u: %s\n",
				m_name.c_str(), eid, error.c_str());
		}
	}

	// --- Connection state ---
	std::string m_host;
	int m_port = 0;
	std::string m_name;
	bool m_connected = false;
	net::TcpClient m_tcp;
	net::RecvBuffer m_recv;

	EntityId m_targetEntityId = ENTITY_NONE; // entity this agent wants to control
	float m_worldTime = 0.3f;
	float m_statusTimer = 0.0f;  // periodic heartbeat

	// --- Entity state cache (received from server) ---
	std::unordered_map<EntityId, std::unique_ptr<Entity>> m_entities;
	std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, ChunkPosHash> m_chunks;

	// --- Entity type definitions ---
	BlockRegistry m_blocks;
	EntityManager m_entityDefs;  // type registry only
	EntityDef m_defaultDef;

	// --- Controlled entities ---
	std::unordered_set<EntityId> m_controlled;
	std::unordered_map<EntityId, AgentBehaviorState> m_behaviorStates;
	std::unordered_map<EntityId, BlockCache> m_blockCaches;
	std::unordered_map<EntityId, std::string> m_lastLoggedGoal;
	BehaviorStore m_behaviorStore;
};

} // namespace modcraft

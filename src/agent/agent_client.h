#pragma once

/**
 * AgentClient — headless AI client that controls one or more entities.
 *
 * Connects to a GameServer via TCP (same protocol as GUI client).
 * Receives entity/chunk state updates, runs Python behavior decide()
 * on a per-entity schedule (DecisionQueue), and sends ActionProposals
 * back to the server.
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
#include "server/chunk_info.h"
#include <zstd.h>
#include "shared/constants.h"
#include "server/behavior.h"
#include "server/behavior_store.h"
#include "server/python_bridge.h"
#include "agent/behavior_executor.h"
#include "agent/block_search.h"
#include "agent/decision_queue.h"
#include "server/entity_manager.h"
#include "server/server_tuning.h"
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
			// printf("[Agent:%s] Cannot connect to %s:%d\n", name.c_str(), host.c_str(), port);
			return false;
		}
		m_connected = true;

		// Identify immediately — server defers all setup until it knows
		// whether this is a GUI client (C_HELLO) or agent (C_AGENT_HELLO).
		{
			net::WriteBuffer hello;
			hello.writeString(m_name);
			hello.writeU32(m_targetEntityId);
			net::sendMessage(m_tcp.fd(), net::C_AGENT_HELLO, hello);
		}

		// Wait for S_ASSIGN_ENTITY (server assigns us the target entity)
		auto start = std::chrono::steady_clock::now();
		while (true) {
			m_recv.readFrom(m_tcp.fd());
			net::MsgHeader hdr;
			std::vector<uint8_t> payload;
			while (m_recv.tryExtract(hdr, payload)) {
				if (hdr.type == net::S_ASSIGN_ENTITY) {
					net::ReadBuffer rb(payload.data(), payload.size());
					EntityId eid = rb.readU32();
					std::string behaviorId = rb.readString();
					assignEntity(eid, behaviorId);
					return true;
				}
				// Handle other messages that arrive before assignment (S_ENTITY, etc.)
				handleMessage(hdr.type, payload);
			}

			auto elapsed = std::chrono::steady_clock::now() - start;
			if (std::chrono::duration<float>(elapsed).count() > 5.0f) {
				// printf("[Agent:%s] Timeout waiting for entity assignment\n", m_name.c_str());
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
		m_decideQueue.scheduleNow(id);
	}

	// Revoke control of an entity. Silent if the agent didn't control it.
	void revokeEntity(EntityId id) {
		if (m_controlled.erase(id) > 0) {
			m_behaviorStates.erase(id);
			m_decideQueue.remove(id);
			// printf("[Agent:%s] Revoked entity %u\n", m_name.c_str(), id);
		}
	}

	// Main tick: receive state, drain decision queue, send actions.
	void tick(float dt) {
		if (!m_connected) return;
		if (!receiveMessages()) {
			m_connected = false;
			return;
		}

		// ── Phase 1: Drain decision queue — run decide() for due entities ────
		auto dueEntities = m_decideQueue.drain(ServerTuning::maxDecidesPerTick);
		std::unordered_set<EntityId> decidedThisTick(dueEntities.begin(), dueEntities.end());

		for (EntityId eid : dueEntities) {
			auto entIt = m_entities.find(eid);
			if (entIt == m_entities.end() || entIt->second->removed) continue;
			if (!m_controlled.count(eid)) continue;
			Entity& e = *entIt->second;
			auto& state = m_behaviorStates[eid];

			// Lazy-load behavior
			if (!state.behavior) {
				std::string bid = e.getProp<std::string>(Prop::BehaviorId, "");
				state.behavior = loadBehavior(bid);
			}

			// Build NearbyEntities from m_entities cache
			auto nearby = gatherNearby(e, m_entities, 64.0f);

			// Block query + scan callbacks for Python
			auto blockQueryFn = [this](int x, int y, int z) -> std::string {
				BlockId bid = getBlock(x, y, z);
				return m_blocks.get(bid).string_id;
			};
			auto scanBlocksFn = [this](const std::string& typeId, glm::vec3 nearPos,
			                           float maxDist, int maxResults) {
				return scanBlocks(nearPos, typeId, maxDist, maxResults);
			};

			// Build world view — chunkBlocks is empty, behaviors use scan_blocks() directly
			std::vector<BlockSample> chunkBlocks;
			BehaviorWorldView view{e, nearby, chunkBlocks, dt, m_worldTime, blockQueryFn, scanBlocksFn};

			auto t0 = std::chrono::steady_clock::now();
			state.currentAction = state.behavior->decide(view);
			float ms = std::chrono::duration<float, std::milli>(
			               std::chrono::steady_clock::now() - t0).count();
			state.lastDecideMs   = ms;
			state.totalDecideMs += ms;
			state.decideCount++;
			state.justDecided = true;

			// Schedule next decide based on duration from behavior
			float duration = state.currentAction.decideDuration;
			auto nextTime = SteadyClock::now() +
				std::chrono::duration_cast<SteadyClock::duration>(
					std::chrono::duration<float>(duration));
			m_decideQueue.schedule(eid, nextTime);

			// Log goal changes
			auto& lastGoal = m_lastLoggedGoal[eid];
			if (e.goalText != lastGoal) {
				lastGoal = e.goalText;
			}

			// Send proposals
			std::vector<ActionProposal> proposals;
			behaviorToActionProposals(e, state, state.currentAction, dt, proposals);
			for (auto& p : proposals) {
				net::WriteBuffer wb;
				net::serializeAction(wb, p);
				net::sendMessage(m_tcp.fd(), net::C_ACTION, wb);
				walkDbgTrackMove(p, state, e);
			}
			state.justDecided = false;
		}

		// ── Phase 2: Re-send Move for entities NOT decided this tick ─────────
		// Keeps physics smooth between decide() calls.
		for (EntityId eid : m_controlled) {
			if (decidedThisTick.count(eid)) continue;
			auto entIt = m_entities.find(eid);
			if (entIt == m_entities.end() || entIt->second->removed) continue;
			Entity& e = *entIt->second;
			auto& state = m_behaviorStates[eid];
			if (state.currentAction.type == BehaviorAction::Move) {
				std::vector<ActionProposal> proposals;
				behaviorToActionProposals(e, state, state.currentAction, dt, proposals);
				for (auto& p : proposals) {
					net::WriteBuffer wb;
					net::serializeAction(wb, p);
					net::sendMessage(m_tcp.fd(), net::C_ACTION, wb);
					walkDbgTrackMove(p, state, e);
				}
			}
		}

		// ── Walk debug: dump per-entity Move stats every 1 second ───────────
		for (auto& [eid, state] : m_behaviorStates) {
			state.walkDbg_windowTimer += dt;
			if (state.walkDbg_windowTimer >= 1.0f) {
				if (state.walkDbg_movesSent > 0) {
					fprintf(stderr,
						"[walkdbg-agent eid=%u] moves=%d distinct=%d to_self=%d (in %.1fs)\n",
						eid, state.walkDbg_movesSent, state.walkDbg_movesNewTgt,
						state.walkDbg_movesToSelf, state.walkDbg_windowTimer);
				}
				state.walkDbg_movesSent   = 0;
				state.walkDbg_movesNewTgt = 0;
				state.walkDbg_movesToSelf = 0;
				state.walkDbg_windowTimer = 0;
			}
		}

		// ── Phase 3: Periodic sweep — ensure no entity is orphaned ───────────
		m_sweepTimer += dt;
		if (m_sweepTimer >= ServerTuning::decisionSweepInterval) {
			m_sweepTimer = 0.0f;
			for (EntityId eid : m_controlled) {
				if (!m_decideQueue.hasPending(eid)) {
					m_decideQueue.scheduleNow(eid);
				}
			}
		}

		// Periodic heartbeat
		m_statusTimer += dt;
		if (m_statusTimer >= 10.0f) {
			m_statusTimer = 0.0f;
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
				for (auto& [k, v] : es.props)
					ent->setProp(k, v);
				m_entities[es.id] = std::move(ent);
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
				for (auto& [k, v] : es.props)
					e.setProp(k, v);
			}
			// Active trigger: if a controlled entity just lost HP → re-decide
			if (m_controlled.count(es.id)) {
				auto& state = m_behaviorStates[es.id];
				if (state.lastKnownHp >= 0 && es.hp < state.lastKnownHp) {
					m_decideQueue.scheduleNow(es.id);
				}
				state.lastKnownHp = es.hp;
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
			m_chunkInfoCache.erase(cp);
			break;
		}
		case net::S_CHUNK_INFO:
		case net::S_CHUNK_INFO_DELTA: {
			auto payload = net::readChunkInfoPayload(rb);
			AgentChunkInfo& ci = m_chunkInfoCache[payload.pos];
			ci.pos = payload.pos;
			ci.hasAir = payload.hasAir;
			if (type == net::S_CHUNK_INFO)
				ci.entries.clear();
			for (auto& we : payload.entries) {
				if (we.count <= 0)
					ci.entries.erase(we.typeId);
				else
					ci.entries[we.typeId] = {we.count};
			}
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
			// Skip equipment: [u8 equipCount][{str slot, str id}...]
			if (rb.hasMore()) {
				uint8_t equipCount = rb.readU8();
				for (uint8_t i = 0; i < equipCount; i++) {
					if (rb.hasMore()) rb.readString(); // slot
					if (rb.hasMore()) rb.readString(); // item
				}
			}

			auto it = m_entities.find(eid);
			if (it != m_entities.end() && it->second->inventory) {
				it->second->inventory->clear();
				for (auto& [itemId, count] : items)
					if (count > 0) it->second->inventory->add(itemId, count);
			}
			break;
		}
		case net::S_TIME: {
			float newTime = rb.readF32();
			// Active trigger: time-of-day threshold crossing → all entities re-decide
			if (m_prevWorldTime >= 0.0f && isTimeOfDayEvent(m_prevWorldTime, newTime)) {
				for (EntityId eid : m_controlled)
					m_decideQueue.scheduleNow(eid);
			}
			m_prevWorldTime = m_worldTime;
			m_worldTime = newTime;
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
			// printf("[Agent:%s] Server assigned entity %u (behavior: %s)\n",
			//	m_name.c_str(), eid, behaviorId.c_str());
			assignEntity(eid, behaviorId);
			break;
		}
		case net::S_REVOKE_ENTITY: {
			EntityId eid = rb.readU32();
			// printf("[Agent:%s] Server revoked entity %u\n", m_name.c_str(), eid);
			revokeEntity(eid);
			break;
		}
		case net::S_RELOAD_BEHAVIOR: {
			EntityId eid = rb.readU32();
			std::string newSource = rb.readString();
			// printf("[Agent:%s] Reloading behavior for entity %u\n", m_name.c_str(), eid);
			reloadBehavior(eid, newSource);
			break;
		}
		case net::S_PROXIMITY: {
			uint32_t count = rb.readU32();
			for (uint32_t i = 0; i < count; i++) {
				EntityId eid = rb.readU32();
				if (m_controlled.count(eid))
					m_decideQueue.scheduleNow(eid);
			}
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
			// printf("[Agent:%s] No .py file for behavior '%s'\n",
			//	m_name.c_str(), behaviorId.c_str());
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

		// printf("[Agent:%s] Loaded behavior '%s'\n", m_name.c_str(), behaviorId.c_str());
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
			m_decideQueue.scheduleNow(eid); // decide immediately with new behavior
			// printf("[Agent:%s] Behavior reloaded for entity %u\n", m_name.c_str(), eid);
		} else {
			printf("[Agent:%s] Behavior reload failed for entity %u: %s\n",
				m_name.c_str(), eid, error.c_str());
		}
	}

	// ── Walk debug: track Move proposal stats ──────────────────────────────
	void walkDbgTrackMove(const ActionProposal& p, AgentBehaviorState& state, const Entity& e) {
		if (p.type != ActionProposal::Move) return;
		state.walkDbg_movesSent++;
		glm::vec3 tgt = state.currentAction.targetPos;
		float distFromPrev = glm::length(glm::vec2(
			tgt.x - state.walkDbg_lastTarget.x,
			tgt.z - state.walkDbg_lastTarget.z));
		float distToSelf = glm::length(glm::vec2(
			tgt.x - e.position.x, tgt.z - e.position.z));
		if (distFromPrev > 0.1f) state.walkDbg_movesNewTgt++;
		if (distToSelf < 0.5f) state.walkDbg_movesToSelf++;
		state.walkDbg_lastTarget = tgt;
	}

	// ── Time-of-day thresholds that trigger active re-decide for all entities ──
	// Behaviors care about dawn (0.25) and dusk (0.75); midnight (0.0 wrap) is
	// also caught. Returns true if prevTime→newTime crosses any threshold.
	static bool isTimeOfDayEvent(float prevTime, float newTime) {
		// Forward crossing of dawn / noon / dusk thresholds
		static const float kThresholds[] = {0.25f, 0.50f, 0.75f};
		for (float t : kThresholds)
			if (prevTime < t && newTime >= t) return true;
		// Midnight wrap-around: time jumps from ~1.0 back to ~0.0
		if (prevTime > 0.90f && newTime < 0.10f) return true;
		return false;
	}

	// Targeted block scan: thin wrapper around block_search::run which owns
	// the actual logic (see src/agent/block_search.h). Kept here so the lambda
	// in gatherNearby() can bind to m_chunkInfoCache / m_chunks directly.
	std::vector<BlockSample> scanBlocks(glm::vec3 origin, const std::string& typeId,
	                                     float maxDist, int maxResults) {
		block_search::Options opt{typeId, origin, maxDist, maxResults};
		return block_search::run(opt, m_chunkInfoCache, m_chunks, m_blocks,
			[this](ChunkPos cp) { requestAndWaitForChunk(cp); });
	}

	// Request a specific chunk from the server and wait for it to arrive.
	// Blocks the agent process (not server or GUI) until the chunk is received.
	void requestAndWaitForChunk(ChunkPos pos) {
		net::WriteBuffer wb;
		wb.writeI32(pos.x); wb.writeI32(pos.y); wb.writeI32(pos.z);
		net::sendMessage(m_tcp.fd(), net::C_RESYNC_CHUNK, wb);

		// Poll for up to 500ms waiting for this chunk
		for (int i = 0; i < 100; i++) {
			if (m_chunks.find(pos) != m_chunks.end()) return; // arrived
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			receiveMessages();
		}
		// Timed out — chunk didn't arrive, scan will skip it
	}

	// --- Connection state ---
	std::string m_host;
	int m_port = 0;
	std::string m_name;
	bool m_connected = false;
	net::TcpClient m_tcp;
	net::RecvBuffer m_recv;

	EntityId m_targetEntityId = ENTITY_NONE;
	float m_worldTime     = 0.3f;

	float m_prevWorldTime = -1.0f;  // used to detect time-of-day threshold crossings
	float m_statusTimer   = 0.0f;   // periodic heartbeat

	// --- Entity state cache (received from server) ---
	std::unordered_map<EntityId, std::unique_ptr<Entity>> m_entities;
	std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, ChunkPosHash> m_chunks;

	// --- Entity type definitions ---
	BlockRegistry m_blocks;
	EntityManager m_entityDefs;  // type registry only
	EntityDef m_defaultDef;

	// --- ChunkInfo cache (counts only, no samples) ---
	struct AgentChunkInfo {
		ChunkPos pos;
		bool hasAir = false;
		struct Entry { int count = 0; };
		std::unordered_map<std::string, Entry> entries;
	};
	std::unordered_map<ChunkPos, AgentChunkInfo, ChunkPosHash> m_chunkInfoCache;

	// --- Controlled entities ---
	std::unordered_set<EntityId> m_controlled;
	std::unordered_map<EntityId, AgentBehaviorState> m_behaviorStates;
	std::unordered_map<EntityId, std::string> m_lastLoggedGoal;
	BehaviorStore m_behaviorStore;

	// --- Decision queue (replaces fixed 4 Hz timer) ---
	DecisionQueue m_decideQueue;
	float m_sweepTimer = 0.0f;
};

} // namespace modcraft

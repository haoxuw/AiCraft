#pragma once

// ServerInterface over TCP. Client generates random UUID as player name.

#include "net/server_interface.h"
#include "logic/physics.h"
#include "net/net_socket.h"
#include "net/net_protocol.h"
#include "client/entity_reconciler.h"
#include "client/local_world.h"
#include <array>
#ifndef __EMSCRIPTEN__
#include <zstd.h>
#endif
#include "logic/annotation.h"
#include <unordered_map>
#include <functional>
#include <string>
#include <random>
#include <thread>
#include <chrono>
#include <algorithm>
#include <vector>
#include <utility>

namespace civcraft {

class NetworkServer : public ServerInterface {
public:
	NetworkServer(const std::string& host, int port, LocalWorld& world)
		: m_host(host), m_port(port), m_world(world) {
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

	bool createGame(int seed, int templateIndex,
	                const WorldGenConfig& /*wgc*/ = WorldGenConfig{}) override {
		if (!m_tcp.connect(m_host.c_str(), m_port)) {
			printf("[Net] Cannot connect to %s:%d\n", m_host.c_str(), m_port);
			return false;
		}

		m_connected = true;

		// Server defers entity creation until C_HELLO, then replies S_WELCOME.
		{
			std::string name = m_displayName.empty()
				? ("Player-" + m_clientUUID.substr(0, 4))
				: m_displayName;
			net::WriteBuffer hello;
#ifdef __EMSCRIPTEN__
			hello.writeU32(1);
#else
			hello.writeU32(net::PROTOCOL_VERSION);
#endif
			hello.writeString(m_clientUUID);
			hello.writeString(name);
			hello.writeString(m_creatureType);
			net::sendMessage(m_tcp.fd(), net::C_HELLO, hello);
		}

		// Generous 60s timeout — server prep phase streams S_PREPARING/S_CHUNK* first.
		auto start = std::chrono::steady_clock::now();
		while (true) {
			m_recv.readFrom(m_tcp.fd());

			if (recvWelcomeFromBuffer()) return true;

			auto elapsed = std::chrono::steady_clock::now() - start;
			if (std::chrono::duration<float>(elapsed).count() > 60.0f) {
				printf("[Net] Timeout waiting for welcome (60s)\n");
				break;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		disconnect();
		return false;
	}

	// Async connect for web (WebSocket needs event-loop frames to complete).
	bool beginConnect() {
		printf("[Net] Connecting to %s:%d ...\n", m_host.c_str(), m_port);
		if (!m_tcp.connect(m_host.c_str(), m_port)) {
			printf("[Net] beginConnect failed for %s:%d\n", m_host.c_str(), m_port);
			return false;
		}
		m_connected = true;
		{
			std::string name = m_displayName.empty()
				? ("Player-" + m_clientUUID.substr(0, 4))
				: m_displayName;
			net::WriteBuffer hello;
#ifdef __EMSCRIPTEN__
			hello.writeU32(1);
#else
			hello.writeU32(net::PROTOCOL_VERSION);
#endif
			hello.writeString(m_clientUUID);
			hello.writeString(name);
			hello.writeString(m_creatureType);
			net::sendMessage(m_tcp.fd(), net::C_HELLO, hello);
		}
		return true;
	}

	// Call every frame — returns true when S_WELCOME arrives.
	bool pollWelcome() {
		if (!m_connected) return false;
		if (!m_recv.readFrom(m_tcp.fd())) {
			printf("[Net] Connection lost while waiting for welcome\n");
			disconnect();
			return false;
		}
		return recvWelcomeFromBuffer();
	}

	void disconnect() override {
		// C_QUIT lets server run cleanup immediately instead of heartbeat timeout.
		if (m_connected && m_tcp.connected()) {
			net::WriteBuffer wb;
			net::sendMessage(m_tcp.fd(), net::C_QUIT, wb);
		}
		m_tcp.disconnect();
		m_connected = false;
		m_serverReady = false;
		m_controlledEid = ENTITY_NONE;
		m_heartbeatTimer = 0.0f;
	}

	bool isConnected() const override { return m_connected; }

	void tick(float dt) override {
		if (!m_connected) return;

		// Liveness ping (any outbound traffic counts server-side).
		m_heartbeatTimer += dt;
		if (m_heartbeatTimer >= kHeartbeatSendInterval) {
			m_heartbeatTimer = 0.0f;
			net::WriteBuffer wb;
			net::sendMessage(m_tcp.fd(), net::C_HEARTBEAT, wb);
		}

		if (!m_recv.readFrom(m_tcp.fd())) {
			printf("[Net] Connection lost\n");
			disconnect();
			return;
		}

		net::MsgHeader hdr;
		std::vector<uint8_t> payload;
		int msgCount = 0;
		while (m_recv.tryExtract(hdr, payload)) {
			handleMessage(hdr.type, payload);
			msgCount++;
		}
		m_totalMsgCount += msgCount;
		m_uptime += dt;

		// Silent-server: warn @2s, disconnect @5s. Reconciler flags stale entities red at 2s too.
		if (msgCount > 0) {
			if (m_silentWarned) {
				std::printf("[NetDiag] Server back (silent %.1fs)\n", m_silentTime);
				std::fflush(stdout);
			}
			m_silentTime = 0;
			m_silentWarned = false;
		} else {
			m_silentTime += dt;
			float limit = m_serverReady ? kHeartbeatDisconnectSec
			                            : kHeartbeatDisconnectPrepSec;
			if (!m_silentWarned && m_silentTime > 2.0f) {
				std::printf("[NetDiag] Server silent for %.1fs "
				            "(phase=%s, limit=%.0fs, chunks=%zu, prep=%.0f%%, entities=%zu)\n",
				            m_silentTime, m_serverReady ? "playing" : "prep",
				            limit, m_world.chunkCount(),
				            m_preparingPct < 0 ? 0.0f : m_preparingPct * 100.0f,
				            m_entities.size());
				std::fflush(stdout);
				m_silentWarned = true;
			}
			if (m_silentTime > limit) {
				std::printf("[NetDiag] Server heartbeat lost >%.0fs — disconnecting to menu "
				            "(phase=%s, chunks=%zu, prep=%.0f%%, entities=%zu, uptime=%.1fs)\n",
				            limit, m_serverReady ? "playing" : "prep",
				            m_world.chunkCount(),
				            m_preparingPct < 0 ? 0.0f : m_preparingPct * 100.0f,
				            m_entities.size(), m_uptime);
				std::fflush(stdout);
				disconnect();
				return;
			}
		}

		if (m_uptime < 5.0f && !getEntity(m_localPlayerId) && msgCount > 0) {
			printf("[Net] t=%.1fs: waiting for player entity (received %zu entities, %zu chunks)\n",
				m_uptime, m_entities.size(), m_world.chunkCount());
		}

		m_diagTimer += dt;
		if (m_diagTimer >= 30.0f) {
			Entity* pe = m_entities.count(m_localPlayerId)
				? m_entities[m_localPlayerId].get() : nullptr;
			printf("[Net] %zu entities, %zu chunks, %.0fs uptime\n",
				m_entities.size(), m_world.chunkCount(), m_uptime);
			if (pe)
				printf("[Net] Player pos=(%.1f,%.1f,%.1f) vel=(%.1f,%.1f,%.1f) ground=%d\n",
					pe->position.x, pe->position.y, pe->position.z,
					pe->velocity.x, pe->velocity.y, pe->velocity.z, pe->onGround);
			m_diagTimer = 0;
		}

		BlockSolidFn solidFn = m_world.solidFn();
		bool serverSilent = m_silentTime > 2.0f;
		m_reconciler.tick(dt, m_localPlayerId, m_entities, solidFn, serverSilent);
	}

	void sendAction(const ActionProposal& action) override {
		if (!m_connected) return;
		net::WriteBuffer buf;
		net::serializeAction(buf, action);
		bool ok = net::sendMessage(m_tcp.fd(), net::C_ACTION, buf);
		if (!ok) {
			std::printf("[Net] sendAction FAILED (fd closed?) actor=%u type=%d\n",
				action.actorId, (int)action.type);
			std::fflush(stdout);
			m_connected = false;
			return;
		}
	}

	void sendGetInventory(EntityId eid) override {
		if (!m_connected) return;
		net::WriteBuffer wb;
		wb.writeU32(eid);
		net::sendMessage(m_tcp.fd(), net::C_GET_INVENTORY, wb);
	}

	void sendSetGoal(EntityId eid, glm::vec3 pos) override {
		if (!m_connected) return;
		net::WriteBuffer wb;
		wb.writeU32(eid);
		wb.writeF32(pos.x); wb.writeF32(pos.y); wb.writeF32(pos.z);
		net::sendMessage(m_tcp.fd(), net::C_SET_GOAL, wb);
	}

	void sendSetGoalGroup(glm::vec3 pos, const std::vector<EntityId>& eids) override {
		if (!m_connected || eids.empty()) return;
		net::WriteBuffer wb;
		wb.writeF32(pos.x); wb.writeF32(pos.y); wb.writeF32(pos.z);
		wb.writeU32((uint32_t)eids.size());
		for (auto eid : eids) wb.writeU32(eid);
		net::sendMessage(m_tcp.fd(), net::C_SET_GOAL_GROUP, wb);
	}

	void sendCancelGoal(EntityId eid) override {
		if (!m_connected) return;
		net::WriteBuffer wb;
		wb.writeU32(eid);
		net::sendMessage(m_tcp.fd(), net::C_CANCEL_GOAL, wb);
	}

	void sendProximity(const std::vector<EntityId>& eids) override {
		(void)eids; // C_PROXIMITY removed — agents run inside PlayerClient now
	}

	ChunkSource& chunks() override { return m_world; }
	EntityId localPlayerId() const override { return m_localPlayerId; }
	EntityId controlledEntityId() const override {
		return m_controlledEid != ENTITY_NONE ? m_controlledEid : m_localPlayerId;
	}
	void setControlledEntityId(EntityId eid) override { m_controlledEid = eid; }
	bool isServerReady() const override { return m_serverReady; }
	float preparingProgress() const override { return m_preparingPct; }
	const std::string& lastError() const override { return m_lastError; }

	// Latest server-auth position; falls back to last applied entity position.
	glm::vec3 getServerPosition(EntityId id) override {
		Entity* e = getEntity(id);
		return m_reconciler.getServerPosition(id, e ? e->position : glm::vec3(0));
	}

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
	uint32_t dayCount() const override { return m_dayCount; }
	glm::vec3 spawnPos() const override { return m_spawnPos; }
	float pickupRange() const override { return 1.5f; } // TODO: sync from server

	const std::string& weatherKind() const override { return m_weatherKind; }
	float    weatherIntensity() const override { return m_weatherIntensity; }
	glm::vec2 weatherWind()      const override { return m_weatherWind; }
	uint32_t weatherSeq()         const override { return m_weatherSeq; }
	const BlockRegistry& blockRegistry() const override { return m_world.blockRegistry(); }
	ActionProposalQueue& proposalQueue() override { return m_proposals; }

	void setEffectCallbacks(
		std::function<void(ChunkPos)> onChunkDirty,
		std::function<void(glm::vec3, const std::string&)> onBlockBreakText = nullptr,
		std::function<void(glm::vec3, const std::string&)> onBlockPlace = nullptr) override {
		m_onChunkDirty      = onChunkDirty;
		m_onBlockBreakText  = onBlockBreakText;
		m_onBlockPlace      = onBlockPlace;
	}

	void setInventoryCallback(std::function<void(EntityId)> cb) override {
		m_onInventoryUpdate = std::move(cb);
	}

	// Wired to AgentClient::onInterrupt / onWorldEvent by game.cpp.
	void setInterruptHandlers(
		std::function<void(EntityId, const std::string&)> onNpcInterrupt,
		std::function<void(const std::string&, const std::string&)> onWorldEvent)
	{
		m_onNpcInterrupt = std::move(onNpcInterrupt);
		m_onWorldEvent   = std::move(onWorldEvent);
	}

	const std::string& clientUUID() const { return m_clientUUID; }
	void setDisplayName(const std::string& name) { m_displayName = name; }
	void setCreatureType(const std::string& type) override { m_creatureType = type; }

	static bool canConnect(const char* host, int port) {
		net::TcpClient probe;
		bool ok = probe.connect(host, port);
		probe.disconnect();
		return ok;
	}

	// Returns null if the chunk has no cached annotations.
	const std::vector<std::pair<glm::ivec3, Annotation>>*
	annotationsForChunk(ChunkPos cp) const override {
		return m_world.annotationsForChunk(cp);
	}

private:
	// Single path for S_ENTITY (full) and S_ENTITY_DELTA (merged into cached
	// baseline first). Creates the Entity on first sight, else refreshes the
	// reconciler + fields the client copies through verbatim.
	void applyEntityState(const net::EntityState& es) {
		auto it = m_entities.find(es.id);
		if (it == m_entities.end()) {
			const EntityDef* def = m_world.entityDefs().getTypeDef(es.typeId);
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
			// First S_ENTITY predates any decide(); empty goalText renders "…".
			ent->goalText = es.goalText;
			// Owned entities keep locally-predicted lookYaw (see update path below).
			bool ownedByUs = (es.owner == (int)m_localPlayerId);
			if (!ownedByUs) {
				ent->lookYaw   = es.lookYaw;
				ent->lookPitch = es.lookPitch;
			} else {
				ent->lookYaw   = es.yaw;
				ent->lookPitch = 0.0f;
			}
			if (!es.characterSkin.empty())
				ent->setProp("character_skin", es.characterSkin);
			// WalkDistance is client-local (see update path below).
			for (auto& [k, v] : es.props) {
				if (k == Prop::WalkDistance) continue;
				ent->setProp(k, v);
			}
			if (es.id == m_localPlayerId)
				printf("[Net] Local player entity created: type=%s pos=(%.1f,%.1f,%.1f)\n",
					es.typeId.c_str(), es.position.x, es.position.y, es.position.z);
			if (def->isLiving())
				ent->setProp(Prop::HP, es.hp);
			if (es.owner != 0)
				ent->setProp(Prop::Owner, es.owner);
			// Must move ent into m_entities before firing callback so getEntity works.
			auto pit = m_pendingInventory.find(es.id);
			bool hadPendingInv = false;
			if (pit != m_pendingInventory.end() && ent->inventory) {
				auto& inv = *ent->inventory;
				inv.clear();
				for (auto& [iid, amt] : pit->second.items)
					inv.add(iid, amt);
				m_pendingInventory.erase(pit);
				hadPendingInv = true;
			}
			m_entities[es.id] = std::move(ent);
			if (hadPendingInv && m_onInventoryUpdate) m_onInventoryUpdate(es.id);
			m_reconciler.onEntityCreate(es.id, es.position, es.velocity, es.yaw,
			                            es.moveTarget, es.moveSpeed);
		} else {
			auto& e = *it->second;

			m_reconciler.onEntityUpdate(es.id, es.position, es.velocity, es.yaw,
			                            es.moveTarget, es.moveSpeed);

			// Local player's onGround stays client-owned (20Hz server value retriggers jumps).
			if (es.id != m_localPlayerId)
				e.onGround = es.onGround;
			// Owned lookYaw/Pitch are locally predicted; server value causes sideways-body bug.
			{
				int owner = e.getProp<int>(Prop::Owner, 0);
				bool ownedByUs = (owner == (int)m_localPlayerId);
				if (!ownedByUs) {
					e.lookYaw   = es.lookYaw;
					e.lookPitch = es.lookPitch;
				}
			}
			if (!es.goalText.empty()) e.goalText = es.goalText;
			if (e.def().isLiving())
				e.setProp(Prop::HP, es.hp);
			if (es.owner != 0)
				e.setProp(Prop::Owner, es.owner);
			// WalkDistance is client-local — 20Hz server value would stairstep walk cycle.
			for (auto& [k, v] : es.props) {
				if (k == Prop::WalkDistance) continue;
				e.setProp(k, v);
			}
		}
	}

	// S_CHUNK / S_CHUNK_Z annotation tail: [u32 n][{i32 dx, dy, dz, str typeId, u8 slot}×n].
	void readChunkAnnotations(net::ReadBuffer& rb, ChunkPos cp) {
		if (!rb.hasMore()) {
			m_world.setAnnotations(cp, {});
			return;
		}
		uint32_t n = rb.readU32();
		std::vector<std::pair<glm::ivec3, Annotation>> out;
		out.reserve(n);
		for (uint32_t i = 0; i < n; i++) {
			int dx = rb.readI32();
			int dy = rb.readI32();
			int dz = rb.readI32();
			std::string typeId = rb.readString();
			uint8_t slot = rb.readU8();
			Annotation a;
			a.typeId = std::move(typeId);
			a.slot = (AnnotationSlot)slot;
			glm::ivec3 wpos(cp.x * CHUNK_SIZE + dx,
			                cp.y * CHUNK_SIZE + dy,
			                cp.z * CHUNK_SIZE + dz);
			out.push_back({wpos, a});
		}
		m_world.setAnnotations(cp, std::move(out));
	}

	// Must process pre-welcome S_PREPARING/S_CHUNK* or chunks get dropped.
	bool recvWelcomeFromBuffer() {
		net::MsgHeader hdr;
		std::vector<uint8_t> payload;
		while (m_recv.tryExtract(hdr, payload)) {
			if (hdr.type == net::S_WELCOME) {
				net::ReadBuffer rb(payload.data(), payload.size());
				m_localPlayerId = rb.readU32();
				m_spawnPos = rb.readVec3();
				printf("[Net] Welcome! Player ID=%u, spawn=(%.1f,%.1f,%.1f)\n",
					m_localPlayerId, m_spawnPos.x, m_spawnPos.y, m_spawnPos.z);
				return true;
			}
			if (hdr.type == net::S_PREPARING) {
				net::ReadBuffer rb(payload.data(), payload.size());
				float pct = rb.readF32();
				m_preparingPct = pct;
				static float s_lastLoggedPct = -1.0f;
				if (pct - s_lastLoggedPct >= 0.1f || pct >= 1.0f) {
					printf("[Net] Preparing world: %.0f%%\n", pct * 100.0f);
					s_lastLoggedPct = pct;
				}
				continue;
			}
			if (hdr.type == net::S_ERROR) {
				net::ReadBuffer rb(payload.data(), payload.size());
				(void)rb.readU32();
				m_lastError = rb.readString();
				printf("[Net] Server error (pre-welcome): %s\n", m_lastError.c_str());
				disconnect();
				return false;
			}
			// Prep-phase chunks land before welcome — dispatch to populate cache.
			handleMessage(hdr.type, payload);
		}
		return false;
	}

	void handleMessage(uint32_t type, const std::vector<uint8_t>& payload) {
		net::ReadBuffer rb(payload.data(), payload.size());

		switch (type) {
		case net::S_WELCOME: {
			// Race: S_WELCOME may land between pollWelcome() and tick().
			if (m_localPlayerId == ENTITY_NONE) {
				m_localPlayerId = rb.readU32();
				m_spawnPos = rb.readVec3();
				printf("[Net] Welcome! Player ID=%u, spawn=(%.1f,%.1f,%.1f) [via tick]\n",
					m_localPlayerId, m_spawnPos.x, m_spawnPos.y, m_spawnPos.z);
			}
			break;
		}
		case net::S_READY: {
			// Loading screen waits on this before handoff.
			m_serverReady = true;
			printf("[Net] Server ready.\n");
			break;
		}
		case net::S_ERROR: {
			(void)rb.readU32();
			m_lastError = rb.readString();
			printf("[Net] Server error: %s\n", m_lastError.c_str());
			disconnect();
			break;
		}
		case net::S_ENTITY: {
			auto es = net::deserializeEntityState(rb);
			applyEntityState(es);
			break;
		}
		case net::S_ENTITY_DELTA: {
			// Look up baseline by id first, then merge only the flagged fields.
			// A never-seen id starts from a zero-initialized EntityState; the
			// server always sets FLD_TYPE_ID on the first broadcast per entity.
			auto hdr = net::readEntityDeltaHeader(rb);
			net::EntityState& es = m_lastEntityState[hdr.eid];
			es.id = hdr.eid;
			net::mergeEntityDeltaFields(rb, hdr.mask, es);
			applyEntityState(es);
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
			m_world.setChunk(cp, std::move(chunk));
			readChunkAnnotations(rb, cp);
			if (m_onChunkDirty) m_onChunkDirty(cp);
			break;
		}
#ifndef __EMSCRIPTEN__
		case net::S_CHUNK_Z: {
			size_t compSize = rb.remaining();
			const uint8_t* compData = rb.remainingData();
			size_t decompBound = ZSTD_getFrameContentSize(compData, compSize);
			if (decompBound == ZSTD_CONTENTSIZE_ERROR || decompBound == ZSTD_CONTENTSIZE_UNKNOWN) {
				fprintf(stderr, "[Client] S_CHUNK_Z: bad zstd frame\n");
				break;
			}
			std::vector<uint8_t> decomp(decompBound);
			size_t actual = ZSTD_decompress(decomp.data(), decompBound, compData, compSize);
			if (ZSTD_isError(actual)) {
				fprintf(stderr, "[Client] S_CHUNK_Z: decompression error: %s\n",
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
			m_world.setChunk(cp, std::move(chunk));
			readChunkAnnotations(zrb, cp);
			if (m_onChunkDirty) m_onChunkDirty(cp);
			break;
		}
#endif
		case net::S_CHUNK_EVICT: {
			ChunkPos cp = {rb.readI32(), rb.readI32(), rb.readI32()};
			m_world.removeChunk(cp);
			if (m_onChunkDirty) m_onChunkDirty(cp);
			break;
		}
		case net::S_ANNOTATION_SET: {
			int x = rb.readI32(), y = rb.readI32(), z = rb.readI32();
			std::string typeId = rb.readString();
			uint8_t slot = rb.readU8();
			auto div = [](int a, int b) { return (a >= 0) ? a / b : (a - b + 1) / b; };
			ChunkPos cp = {div(x, CHUNK_SIZE), div(y, CHUNK_SIZE), div(z, CHUNK_SIZE)};
			glm::ivec3 wpos(x, y, z);
			m_world.removeAnnotation(cp, wpos);
			if (!typeId.empty()) {
				Annotation a;
				a.typeId = typeId;
				a.slot = (AnnotationSlot)slot;
				m_world.addAnnotation(cp, wpos, a);
			}
			if (m_onChunkDirty) m_onChunkDirty(cp);
			break;
		}
		case net::S_REMOVE: {
			EntityId id = rb.readU32();
			m_entities.erase(id);
			m_lastEntityState.erase(id);
			m_reconciler.onEntityRemove(id);
			break;
		}
		case net::S_TIME: {
			m_worldTime = rb.readF32();
			// v3+ payload appends day counter; old servers omit it.
			if (rb.hasMore()) m_dayCount = rb.readU32();
			break;
		}
		case net::S_WEATHER: {
			m_weatherKind      = rb.readString();
			m_weatherIntensity = rb.readF32();
			float wx           = rb.readF32();
			float wz           = rb.readF32();
			m_weatherWind      = {wx, wz};
			m_weatherSeq       = rb.readU32();
			break;
		}
		case net::S_NPC_INTERRUPT: {
			EntityId eid = (EntityId)rb.readU32();
			std::string reason = rb.readString();
			if (m_onNpcInterrupt) m_onNpcInterrupt(eid, reason);
			break;
		}
		case net::S_WORLD_EVENT: {
			std::string kind    = rb.readString();
			std::string payload = rb.readString();
			if (m_onWorldEvent) m_onWorldEvent(kind, payload);
			break;
		}
		case net::S_BLOCK: {
			int bx = rb.readI32(), by = rb.readI32(), bz = rb.readI32();
			BlockId bid = (BlockId)rb.readU32();
			uint8_t p2 = rb.hasMore() ? rb.readU8() : 0;
			auto div = [](int a, int b) { return (a >= 0) ? a / b : (a - b + 1) / b; };
			ChunkPos cp = {div(bx, CHUNK_SIZE), div(by, CHUNK_SIZE), div(bz, CHUNK_SIZE)};
			// Old block lets fx distinguish dig/place/swap (e.g. door↔door_open).
			BlockId oldBid = m_world.getBlock(bx, by, bz);
			if (bid == BLOCK_AIR && oldBid != BLOCK_AIR && m_onBlockBreakText) {
				const BlockDef& bdef = m_world.blockRegistry().get(oldBid);
				if (bdef.string_id != "air" && !bdef.string_id.empty())
					m_onBlockBreakText(glm::vec3(bx, by, bz),
						bdef.display_name.empty() ? bdef.string_id : bdef.display_name);
			} else if (bid != BLOCK_AIR && oldBid != bid && m_onBlockPlace) {
				const BlockDef& bdef = m_world.blockRegistry().get(bid);
				if (!bdef.string_id.empty())
					m_onBlockPlace(glm::vec3(bx, by, bz), bdef.string_id);
			}
			m_world.setBlock(bx, by, bz, bid);
			if (p2 != 0) {
				Chunk* c = m_world.getChunk(cp);
				if (c) {
					int lx = ((bx % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
					int ly = ((by % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
					int lz = ((bz % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
					c->set(lx, ly, lz, bid, p2);
				}
			}
			if (m_onChunkDirty) m_onChunkDirty(cp);
			break;
		}
		case net::S_INVENTORY: {
			EntityId id = rb.readU32();
			uint32_t count = rb.readU32();
			std::vector<std::pair<std::string,int>> items;
			for (uint32_t i = 0; i < count && rb.hasMore(); i++) {
				std::string itemId = rb.readString();
				int amount = rb.readI32();
				items.push_back({itemId, amount});
			}
			// Equipment tail: [u8 count][{str slot, str id}...]
			std::vector<std::pair<std::string,std::string>> equipment;
			if (rb.hasMore()) {
				uint8_t equipCount = rb.readU8();
				for (uint8_t i = 0; i < equipCount && rb.hasMore(); i++) {
					std::string slot = rb.readString();
					std::string eqId = rb.readString();
					equipment.push_back({slot, eqId});
				}
			}

			auto applyInv = [&](Inventory& inv) {
				inv.clear();
				for (auto& [iid, amt] : items) inv.add(iid, amt);
				for (auto& [slot, eqId] : equipment) {
					WearSlot ws;
					if (wearSlotFromString(slot, ws)) {
						inv.add(eqId, 1); // counter needs it for equip()
						inv.equip(ws, eqId);
					}
				}
			};

			auto it = m_entities.find(id);
			if (it != m_entities.end() && it->second->inventory) {
				applyInv(*it->second->inventory);
				if (m_onInventoryUpdate) m_onInventoryUpdate(id);
			} else {
				// Buffer until S_ENTITY for this id arrives.
				m_pendingInventory[id] = {std::move(items)};
			}
			break;
		}
		}
	}

	std::string m_host;
	int m_port;
	std::string m_clientUUID;
	std::string m_displayName;
	std::string m_creatureType;
	bool m_connected = false;

	net::TcpClient m_tcp;
	net::RecvBuffer m_recv;

	EntityId m_localPlayerId = ENTITY_NONE;
	EntityId m_controlledEid = ENTITY_NONE; // ENTITY_NONE = drive local player
	glm::vec3 m_spawnPos = {0, 0, 0};
	float m_worldTime = 0.25f;
	uint32_t m_dayCount = 0;

	// S_WEATHER drives sky/fog/particles. Defaults keep visuals sane pre-first-broadcast.
	std::string m_weatherKind      = "clear";
	float       m_weatherIntensity = 0.0f;
	glm::vec2   m_weatherWind      = {0.0f, 0.0f};
	uint32_t    m_weatherSeq       = 0;

	bool m_serverReady = false;
	float m_preparingPct = -1.0f;  // -1 = no S_PREPARING seen; else [0..1]
	std::string m_lastError;

	std::unordered_map<EntityId, std::unique_ptr<Entity>> m_entities;
	LocalWorld& m_world;

	// Per-entity smoothing + drift correction between 20Hz broadcasts.
	EntityReconciler m_reconciler;

	// Inventory that arrived before its entity — applied on first S_ENTITY.
	struct PendingInv {
		std::vector<std::pair<std::string,int>> items;
	};
	std::unordered_map<EntityId, PendingInv> m_pendingInventory;

	// Baseline for S_ENTITY_DELTA merge (v4+). Holds the last fully-assembled
	// EntityState per entity id; each delta updates only the fields whose
	// bit is set, then we feed the merged struct through applyEntityState.
	// Erased alongside m_entities on S_REMOVE.
	std::unordered_map<EntityId, net::EntityState> m_lastEntityState;

	ActionProposalQueue m_proposals;
	EntityDef m_defaultDef;

	float m_diagTimer = 0;
	int m_totalMsgCount = 0;
	float m_uptime = 0;

	float m_silentTime = 0;
	bool  m_silentWarned = false;
	// Post-ready: 5s fine. Pre-ready: server may gen chunks synchronously on cold cache,
	// 10-30s gaps between S_PREPARING on first connect.
	static constexpr float kHeartbeatDisconnectSec    = 5.0f;
	static constexpr float kHeartbeatDisconnectPrepSec = 60.0f;

	// Outbound heartbeat resets server's per-client idle timer (enables cleanup on silent clients).
	float m_heartbeatTimer = 0.0f;
	static constexpr float kHeartbeatSendInterval = 2.0f;

	std::function<void(ChunkPos)> m_onChunkDirty;
	std::function<void(glm::vec3, const std::string&)> m_onBlockBreakText;
	std::function<void(glm::vec3, const std::string&)> m_onBlockPlace;
	std::function<void(EntityId)> m_onInventoryUpdate;
	std::function<void(EntityId, const std::string&)> m_onNpcInterrupt;
	std::function<void(const std::string&, const std::string&)> m_onWorldEvent;
};

} // namespace civcraft

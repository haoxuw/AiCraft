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
#include "shared/physics.h"
#include "shared/net_socket.h"
#include "shared/net_protocol.h"
#include "client/entity_reconciler.h"
#include <array>
#ifndef __EMSCRIPTEN__
#include <zstd.h>
#endif
#include "shared/annotation.h"
#include "shared/block_registry.h"
#include "shared/chunk.h"
#include "server/entity_manager.h"
#include "content/builtin.h"
#include "shared/artifact_registry.h"
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
	NetworkServer(const std::string& host, int port)
		: m_host(host), m_port(port) {
		// Register all block and entity definitions (same as server)
		// so entities have proper collision boxes, HP, inventory, etc.
		registerAllBuiltins(m_blocks, m_entityDefs);

		// Merge Python-declared feature tags into EntityDefs
		{
			ArtifactRegistry artifacts;
			artifacts.loadAll("artifacts");
			m_entityDefs.mergeArtifactTags(artifacts.livingTags());
		}

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

	bool createGame(int seed, int templateIndex,
	                const WorldGenConfig& /*wgc*/ = WorldGenConfig{}) override {
		if (!m_tcp.connect(m_host.c_str(), m_port)) {
			printf("[Net] Cannot connect to %s:%d\n", m_host.c_str(), m_port);
			return false;
		}

		m_connected = true;

		// Send C_HELLO immediately — server defers entity creation until it
		// receives this, then responds with S_WELCOME.
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

		// Wait for S_WELCOME. The server may spend many seconds in its
		// async Preparing phase (generating chunks) before sending welcome,
		// so use a generous timeout and process any prep-phase traffic
		// (S_PREPARING, S_CHUNK, S_CHUNK_Z) that arrives in the meantime.
		auto start = std::chrono::steady_clock::now();
		while (true) {
			m_recv.readFrom(m_tcp.fd()); // non-blocking, may return EAGAIN

			if (recvWelcomeFromBuffer()) return true;

			auto elapsed = std::chrono::steady_clock::now() - start;
			if (std::chrono::duration<float>(elapsed).count() > 60.0f) {
				printf("[Net] Timeout waiting for welcome (60s)\n");
				break;
			}

			// Don't spin at 100% CPU while waiting
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		disconnect();
		return false;
	}

	// ── Async connect for web (WebSocket needs event-loop frames to complete) ──
	// Step 1: initiate connection (non-blocking, returns immediately)
	bool beginConnect() {
		printf("[Net] Connecting to %s:%d ...\n", m_host.c_str(), m_port);
		if (!m_tcp.connect(m_host.c_str(), m_port)) {
			printf("[Net] beginConnect failed for %s:%d\n", m_host.c_str(), m_port);
			return false;
		}
		m_connected = true;
		// Send C_HELLO immediately (same as sync path)
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

	// Step 2: call every frame — returns true when S_WELCOME has been received
	bool pollWelcome() {
		if (!m_connected) return false;
		if (!m_recv.readFrom(m_tcp.fd())) {
			printf("[Net] Connection lost while waiting for welcome\n");
			disconnect(); // close fd properly
			return false;
		}
		return recvWelcomeFromBuffer();
	}

	void disconnect() override {
		// Polite quit: tell the server we're leaving so it can run cleanup
		// immediately (snapshot owned NPCs, save inventory, despawn) instead
		// of waiting for the heartbeat timeout. Best-effort — if the socket
		// is already dead the send just fails silently.
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

		// Emit liveness pings on a fixed cadence. Any outbound traffic counts
		// server-side, so this loop only fires when the player is otherwise
		// quiet — no action, no goal, no inventory request.
		m_heartbeatTimer += dt;
		if (m_heartbeatTimer >= kHeartbeatSendInterval) {
			m_heartbeatTimer = 0.0f;
			net::WriteBuffer wb;
			net::sendMessage(m_tcp.fd(), net::C_HEARTBEAT, wb);
		}

		// Read incoming messages
		if (!m_recv.readFrom(m_tcp.fd())) {
			printf("[Net] Connection lost\n");
			disconnect(); // close fd properly, not just flag
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

		// Silent-server detection + escalating response:
		//   @2s → warn once (lets a brief hiccup self-recover)
		//   @5s → disconnect() to force the reconnect / return-to-menu flow
		// The reconciler will also flag individual entities red after 2s of
		// their own staleness, so the player sees trouble before the full
		// disconnect fires.
		if (msgCount > 0) {
			if (m_silentWarned) {
				std::printf("[NetDiag] Server back (silent %.1fs)\n", m_silentTime);
				std::fflush(stdout);
			}
			m_silentTime = 0;
			m_silentWarned = false;
		} else {
			m_silentTime += dt;
			if (!m_silentWarned && m_silentTime > 2.0f) {
				std::printf("[NetDiag] Server silent for %.1fs — no messages arriving\n", m_silentTime);
				std::fflush(stdout);
				m_silentWarned = true;
			}
			if (m_silentTime > kHeartbeatDisconnectSec) {
				std::printf("[NetDiag] Server heartbeat lost >%.0fs — disconnecting to menu\n",
				            kHeartbeatDisconnectSec);
				std::fflush(stdout);
				disconnect();
				return;
			}
		}

		// Early connection logging — only warn if player entity is missing
		if (m_uptime < 5.0f && !getEntity(m_localPlayerId) && msgCount > 0) {
			printf("[Net] t=%.1fs: waiting for player entity (received %zu entities, %zu chunks)\n",
				m_uptime, m_entities.size(), m_chunkData.size());
		}

		// Periodic diagnostics (every 30 seconds — quiet by default)
		m_diagTimer += dt;
		if (m_diagTimer >= 30.0f) {
			Entity* pe = m_entities.count(m_localPlayerId)
				? m_entities[m_localPlayerId].get() : nullptr;
			printf("[Net] %zu entities, %zu chunks, %.0fs uptime\n",
				m_entities.size(), m_chunkData.size(), m_uptime);
			if (pe)
				printf("[Net] Player pos=(%.1f,%.1f,%.1f) vel=(%.1f,%.1f,%.1f) ground=%d\n",
					pe->position.x, pe->position.y, pe->position.z,
					pe->velocity.x, pe->velocity.y, pe->velocity.z, pe->onGround);
			m_diagTimer = 0;
		}

		BlockSolidFn solidFn = [&](int x, int y, int z) -> float {
			const auto& bd = m_blocks.get(m_chunks.getBlock(x, y, z));
			return bd.solid ? bd.collision_height : 0.0f;
		};
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
		// C_PROXIMITY removed — agents run inside PlayerClient now, no server relay needed
		(void)eids;
	}

	// --- State access ---
	ChunkSource& chunks() override { return m_chunks; }
	EntityId localPlayerId() const override { return m_localPlayerId; }
	EntityId controlledEntityId() const override {
		return m_controlledEid != ENTITY_NONE ? m_controlledEid : m_localPlayerId;
	}
	void setControlledEntityId(EntityId eid) override { m_controlledEid = eid; }
	bool isServerReady() const override { return m_serverReady; }
	float preparingProgress() const override { return m_preparingPct; }

	// Latest server-authoritative position from the broadcast stream.
	// Falls back to the last applied entity position if no interp target.
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
	glm::vec3 spawnPos() const override { return m_spawnPos; }
	float pickupRange() const override { return 1.5f; } // TODO: sync from server
	const BlockRegistry& blockRegistry() const override { return m_blocks; }
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

	// Wired to AgentClient::onInterrupt / onWorldEvent by game.cpp after
	// the agent client is constructed. See Step 7 of the decide-loop plan.
	void setInterruptHandlers(
		std::function<void(EntityId, const std::string&)> onNpcInterrupt,
		std::function<void(const std::string&, const std::string&)> onWorldEvent)
	{
		m_onNpcInterrupt = std::move(onNpcInterrupt);
		m_onWorldEvent   = std::move(onWorldEvent);
	}

	const std::string& clientUUID() const { return m_clientUUID; }
	void setDisplayName(const std::string& name) { m_displayName = name; }
	void setCreatureType(const std::string& type) { m_creatureType = type; }

	// Try to connect (non-blocking check for game.cpp)
	static bool canConnect(const char* host, int port) {
		net::TcpClient probe;
		bool ok = probe.connect(host, port);
		probe.disconnect();
		return ok;
	}

	// Annotation accessor for the renderer. Returns null if the chunk has no
	// cached annotations (or hasn't been received yet). Overrides
	// ServerInterface so the renderer never touches NetworkServer directly.
	const std::vector<std::pair<glm::ivec3, Annotation>>*
	annotationsForChunk(ChunkPos cp) const override {
		auto it = m_annotations.find(cp);
		return it != m_annotations.end() ? &it->second : nullptr;
	}

private:
	// Shared annotation tail-parsing for S_CHUNK and S_CHUNK_Z payloads.
	// Reads [u32 count][{i32 dx, i32 dy, i32 dz, str typeId, u8 slot}×count]
	// and stores annotations in world-space under chunk cp.
	void readChunkAnnotations(net::ReadBuffer& rb, ChunkPos cp) {
		if (!rb.hasMore()) {
			m_annotations.erase(cp);
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
		if (out.empty()) m_annotations.erase(cp);
		else m_annotations[cp] = std::move(out);
	}

	// Original private section below.

	// Drain the receive buffer while waiting for S_WELCOME.
	//
	// The async Preparing phase streams S_PREPARING + chunk messages BEFORE
	// welcome, so this loop must process them (otherwise chunks would be
	// silently dropped and we'd just wait forever on the wire queue).
	// Returns true the instant S_WELCOME is parsed.
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
				m_preparingPct = pct;  // surfaced to loading UI via preparingProgress()
				static float s_lastLoggedPct = -1.0f;
				if (pct - s_lastLoggedPct >= 0.1f || pct >= 1.0f) {
					printf("[Net] Preparing world: %.0f%%\n", pct * 100.0f);
					s_lastLoggedPct = pct;
				}
				continue;
			}
			// Other messages (S_CHUNK, S_CHUNK_Z, …) arrive before welcome
			// during prep — dispatch so the chunk cache is populated.
			handleMessage(hdr.type, payload);
		}
		return false;
	}

	void handleMessage(uint32_t type, const std::vector<uint8_t>& payload) {
		net::ReadBuffer rb(payload.data(), payload.size());

		switch (type) {
		case net::S_READY: {
			// Server finished per-client setup (mobs spawned, welcome done).
			// The loading screen waits on this before handing off to gameplay.
			m_serverReady = true;
			printf("[Net] Server ready.\n");
			break;
		}
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
				// Placeholder until first decide() lands — the render path asserts
			// goalText is non-empty, but S_ENTITY on first spawn arrives before
			// any Python decide() has populated a goal.
			ent->goalText = es.goalText.empty() ? "Spawning..." : es.goalText;
				// Entities owned by our in-process agent client keep lookYaw
				// locally predicted (same reason as the S_ENTITY update path).
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
				// Apply string properties (ItemType, BehaviorId, etc.)
				// WalkDistance is client-local (see tick() sync path).
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
				// Apply any inventory that arrived before this entity
				auto pit = m_pendingInventory.find(es.id);
				if (pit != m_pendingInventory.end() && ent->inventory) {
					auto& inv = *ent->inventory;
					inv.clear();
					for (auto& [iid, amt] : pit->second.items)
						inv.add(iid, amt);
					m_pendingInventory.erase(pit);
					if (m_onInventoryUpdate) m_onInventoryUpdate(es.id);
				}
				m_entities[es.id] = std::move(ent);
				m_reconciler.onEntityCreate(es.id, es.position, es.velocity, es.yaw,
				                            es.moveTarget, es.moveSpeed);
			} else {
				auto& e = *it->second;

				m_reconciler.onEntityUpdate(es.id, es.position, es.velocity, es.yaw,
				                            es.moveTarget, es.moveSpeed);

				// Sync non-positional state immediately.
				// Skip onGround for local player — client physics owns it.
				// Server's onGround is stale (20Hz) and would re-trigger jumps.
				if (es.id != m_localPlayerId)
					e.onGround = es.onGround;
				// lookYaw/lookPitch are locally predicted for entities owned by
				// our in-process agent (mirror of the WalkDistance filter below).
				// Server broadcasts would stomp the agent's fast yaw updates and
				// reintroduce the sideways-body bug.
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
				// Sync entity properties from server (server already filters
				// out client-private props like SelectedSlot at broadcast time).
				// WalkDistance is client-local for smooth animation (same as
				// player's m_playerWalkDist) — the server's 20Hz broadcasts
				// would stairstep the walk cycle if we applied them.
				for (auto& [k, v] : es.props) {
					if (k == Prop::WalkDistance) continue;
					e.setProp(k, v);
				}
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
			m_chunkData[cp] = std::move(chunk);
			readChunkAnnotations(rb, cp);
			if (m_onChunkDirty) m_onChunkDirty(cp);
			break;
		}
#ifndef __EMSCRIPTEN__
		case net::S_CHUNK_Z: {
			// Decompress zstd frame, then parse identical to S_CHUNK
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
			m_chunkData[cp] = std::move(chunk);
			readChunkAnnotations(zrb, cp);
			if (m_onChunkDirty) m_onChunkDirty(cp);
			break;
		}
#endif
		case net::S_CHUNK_EVICT: {
			ChunkPos cp = {rb.readI32(), rb.readI32(), rb.readI32()};
			m_chunkData.erase(cp);
			m_annotations.erase(cp);
			if (m_onChunkDirty) m_onChunkDirty(cp); // triggers mesh rebuild for that position
			break;
		}
		case net::S_ANNOTATION_SET: {
			int x = rb.readI32(), y = rb.readI32(), z = rb.readI32();
			std::string typeId = rb.readString();
			uint8_t slot = rb.readU8();
			auto div = [](int a, int b) { return (a >= 0) ? a / b : (a - b + 1) / b; };
			ChunkPos cp = {div(x, CHUNK_SIZE), div(y, CHUNK_SIZE), div(z, CHUNK_SIZE)};
			auto& vec = m_annotations[cp];
			// Remove any existing annotation at this position.
			vec.erase(std::remove_if(vec.begin(), vec.end(),
				[&](const auto& p){ return p.first.x == x && p.first.y == y && p.first.z == z; }),
				vec.end());
			if (!typeId.empty()) {
				Annotation a;
				a.typeId = typeId;
				a.slot = (AnnotationSlot)slot;
				vec.push_back({glm::ivec3(x, y, z), a});
			}
			if (m_onChunkDirty) m_onChunkDirty(cp);
			break;
		}
		case net::S_REMOVE: {
			EntityId id = rb.readU32();
			m_entities.erase(id);
			m_reconciler.onEntityRemove(id);
			break;
		}
		case net::S_TIME: {
			m_worldTime = rb.readF32();
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
			auto it = m_chunkData.find(cp);
			if (it != m_chunkData.end()) {
				int lx = ((bx % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
				int ly = ((by % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
				int lz = ((bz % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
				// Detect block break: non-air → air transition
				if (bid == BLOCK_AIR && m_onBlockBreakText) {
					BlockId oldBid = it->second->get(lx, ly, lz);
					if (oldBid != BLOCK_AIR) {
						const BlockDef& bdef = m_blocks.get(oldBid);
						if (bdef.string_id != "air" && !bdef.string_id.empty())
							m_onBlockBreakText(glm::vec3(bx, by, bz), bdef.display_name.empty() ? bdef.string_id : bdef.display_name);
					}
				}
				it->second->set(lx, ly, lz, bid, p2);
			}
			if (m_onChunkDirty) m_onChunkDirty(cp);
			break;
		}
		case net::S_INVENTORY: {
			EntityId id = rb.readU32();
			// Read items
			uint32_t count = rb.readU32();
			std::vector<std::pair<std::string,int>> items;
			for (uint32_t i = 0; i < count && rb.hasMore(); i++) {
				std::string itemId = rb.readString();
				int amount = rb.readI32();
				items.push_back({itemId, amount});
			}
			// Read equipment: [u8 equipCount][{str slot, str id}...]
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
						inv.add(eqId, 1); // need it in counter for equip()
						inv.equip(ws, eqId);
					}
				}
			};

			auto it = m_entities.find(id);
			if (it != m_entities.end() && it->second->inventory) {
				applyInv(*it->second->inventory);
				if (m_onInventoryUpdate) m_onInventoryUpdate(id);
			} else {
				// Entity not yet known — buffer for when S_ENTITY arrives
				m_pendingInventory[id] = {std::move(items)};
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
	std::string m_displayName;   // player name
	std::string m_creatureType;  // requested creature type
	bool m_connected = false;

	net::TcpClient m_tcp;
	net::RecvBuffer m_recv;

	EntityId m_localPlayerId = ENTITY_NONE;
	EntityId m_controlledEid = ENTITY_NONE; // Control mode; ENTITY_NONE = drive local player
	glm::vec3 m_spawnPos = {0, 0, 0};
	float m_worldTime = 0.25f;
	bool m_serverReady = false;
	float m_preparingPct = -1.0f;  // -1 = no S_PREPARING seen; else [0..1]

	std::unordered_map<EntityId, std::unique_ptr<Entity>> m_entities;
	std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, ChunkPosHash> m_chunkData;
	// Block decorations (flowers, moss, …) keyed by chunk for cheap eviction.
	// Each entry: world-space host-block pos + Annotation. Populated from
	// S_CHUNK / S_CHUNK_Z tails and single-cell S_ANNOTATION_SET updates.
	std::unordered_map<ChunkPos, std::vector<std::pair<glm::ivec3, Annotation>>,
	                   ChunkPosHash> m_annotations;
	NetChunkSource m_chunks{*this};

	// Client-side smoothing + reconciliation for remote entities.
	// Owns all per-entity physics + drift-correction between 20Hz broadcasts.
	EntityReconciler m_reconciler;

	// Pending inventory: arrived before entity — applied on first S_ENTITY
	struct PendingInv {
		std::vector<std::pair<std::string,int>> items;
	};
	std::unordered_map<EntityId, PendingInv> m_pendingInventory;

	BlockRegistry m_blocks;
	ActionProposalQueue m_proposals;
	EntityDef m_defaultDef;
	EntityManager m_entityDefs;  // holds type defs for entity creation

	// Diagnostics
	float m_diagTimer = 0;
	int m_totalMsgCount = 0;
	float m_uptime = 0;

	float m_silentTime = 0;
	bool  m_silentWarned = false;
	static constexpr float kHeartbeatDisconnectSec = 5.0f;

	// Outbound heartbeat. Keeps the server's per-client idle timer reset so
	// the server can drop a silent client (crashed GUI, network drop) and
	// run its normal cleanup — snapshot owned NPCs, save inventory, despawn.
	float m_heartbeatTimer = 0.0f;
	static constexpr float kHeartbeatSendInterval = 2.0f; // seconds

	std::function<void(ChunkPos)> m_onChunkDirty;
	std::function<void(glm::vec3, const std::string&)> m_onBlockBreakText;
	std::function<void(glm::vec3, const std::string&)> m_onBlockPlace;
	std::function<void(EntityId)> m_onInventoryUpdate;
	std::function<void(EntityId, const std::string&)> m_onNpcInterrupt;
	std::function<void(const std::string&, const std::string&)> m_onWorldEvent;
};

} // namespace civcraft

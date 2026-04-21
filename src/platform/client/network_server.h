#pragma once

// ServerInterface over TCP. Client generates random UUID as player name.

#include "net/server_interface.h"
#include "logic/physics.h"
#include "net/net_socket.h"
#include "net/net_protocol.h"
#include "client/entity_reconciler.h"
#include "client/local_world.h"
#include "debug/perf_registry.h"
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
#include <atomic>
#include <mutex>
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

	~NetworkServer() {
		// Belt & suspenders: stop the worker in case the caller skipped disconnect().
		stopNetThread();
	}

	bool createGame(int seed, int templateIndex,
	                const WorldGenConfig& /*wgc*/ = WorldGenConfig{}) override {
		if (!m_tcp.connect(m_host.c_str(), m_port)) {
			printf("[Net] Cannot connect to %s:%d\n", m_host.c_str(), m_port);
			return false;
		}

		m_connected = true;
		startNetThread();

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
		while (m_connected && !m_welcomeReceived) {
			// Worker feeds queue asynchronously; we drain on this thread so
			// handleMessage's mutation of m_entities / m_world stays single-threaded.
			if (drainQueue() < 0) {
				printf("[Net] Connection lost waiting for welcome\n");
				break;
			}
			if (m_welcomeReceived) return true;

			auto elapsed = std::chrono::steady_clock::now() - start;
			if (std::chrono::duration<float>(elapsed).count() > 60.0f) {
				printf("[Net] Timeout waiting for welcome (60s)\n");
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		if (m_welcomeReceived) return true;
		disconnect();
		return false;
	}

	// Async connect: the native UI uses this during CharacterSelect so the
	// window keeps pumping events while S_WELCOME is in flight (blocking in
	// the click handler triggers "App not responding"). Web still needs it
	// because WebSocket handshakes require event-loop frames.
	bool beginConnect(int /*seed*/ = 42, int /*templateIndex*/ = 1) override {
		printf("[Net] Connecting to %s:%d ...\n", m_host.c_str(), m_port);
		if (!m_tcp.connect(m_host.c_str(), m_port)) {
			printf("[Net] beginConnect failed for %s:%d\n", m_host.c_str(), m_port);
			return false;
		}
		m_connected = true;
		startNetThread();
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

	// Pure query: tick() is the sole socket consumer and flips
	// m_welcomeReceived when S_WELCOME is dispatched. Must be read after
	// tick() each frame (see Game::runOneFrame order).
	bool pollWelcome() override { return m_welcomeReceived; }

	void disconnect() override {
		// C_QUIT lets server run cleanup immediately instead of heartbeat timeout.
		if (m_connected && m_tcp.connected()) {
			net::WriteBuffer wb;
			net::sendMessage(m_tcp.fd(), net::C_QUIT, wb);
		}
		// Stop the worker BEFORE closing the socket — the worker reads from the
		// fd, and closing it while the worker is in readFrom() gives a clean
		// EBADF/0-return; stop flag ensures the worker exits its loop instead
		// of racing on a new fd if we reconnect soon after.
		stopNetThread();
		m_tcp.disconnect();
		m_connected = false;
		m_serverReady = false;
		m_welcomeReceived = false;
		m_preparingPct = -1.0f;
		m_controlledEid = ENTITY_NONE;
		m_heartbeatTimer = 0.0f;
		// Drop any unprocessed messages from the last session.
		{
			std::lock_guard<std::mutex> lk(m_queueMu);
			m_queue.clear();
		}
		m_pending.clear();
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

		// 2 Hz RTT probe. Token is the steady_clock now in microseconds —
		// we echo it back, S_PONG handler diffs against a fresh now to get
		// round-trip millis. PING costs 8 bytes + header; trivial vs the
		// chunk/entity stream.
		m_pingTimer += dt;
		if (m_pingTimer >= kPingSendInterval) {
			m_pingTimer = 0.0f;
			uint64_t nowUs = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			net::WriteBuffer wb;
			wb.writeU64(nowUs);
			net::sendMessage(m_tcp.fd(), net::C_PING, wb);
		}

		int msgCount = drainQueue();
		if (msgCount < 0) {
			printf("[Net] Connection lost\n");
			disconnect();
			return;
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

		// Pre-join waiting tick. Throttle to 1Hz; fires while we're still
		// before the "player entity arrived" milestone. Stops printing once
		// the player entity exists — the arrival line is emitted elsewhere.
		if (!getEntity(m_localPlayerId) && msgCount > 0) {
			m_waitLogTimer += dt;
			if (m_waitLogTimer >= 1.0f) {
				m_waitLogTimer = 0;
				printf("[Net] waiting for player entity — t=%.1fs, %zu entities, %zu chunks\n",
					m_uptime, m_entities.size(), m_world.chunkCount());
			}
		}

		m_diagTimer += dt;
		if (m_diagTimer >= 30.0f) {
			Entity* pe = m_entities.count(m_localPlayerId)
				? m_entities[m_localPlayerId].get() : nullptr;
			printf("[Net] stats: %zu entities (+%d), %zu chunks, %.0fs uptime\n",
				m_entities.size(), m_entitiesArrivedSinceLastDiag,
				m_world.chunkCount(), m_uptime);
			if (pe)
				printf("[Net] Player pos=(%.1f,%.1f,%.1f) vel=(%.1f,%.1f,%.1f) ground=%d\n",
					pe->position.x, pe->position.y, pe->position.z,
					pe->velocity.x, pe->velocity.y, pe->velocity.z, pe->onGround);
			m_entitiesArrivedSinceLastDiag = 0;
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
	float lastRttMs() const { return m_lastRttMs; }
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

	// Repoint at a different civcraft-server before reconnecting. Only safe
	// while disconnected — the Multiplayer menu calls this after the user
	// picks a LAN-discovered server, then CharacterSelect drives the handshake.
	void setTarget(const std::string& host, int port) {
		if (m_connected) disconnect();
		m_host = host;
		m_port = port;
		printf("[Net] Target set to %s:%d\n", host.c_str(), port);
	}
	const std::string& host() const { return m_host; }
	int targetPort() const { return m_port; }

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
			// Per-entity arrival is tallied; the periodic diag line reports
			// counts. Only the local-player arrival is milestone-worthy.
			m_entitiesArrivedSinceLastDiag++;
			if (es.id == m_localPlayerId) {
				printf("[Net] Player entity arrived: id=%u type=%s pos=(%.1f,%.1f,%.1f) t=%.1fs\n",
					es.id, es.typeId.c_str(),
					es.position.x, es.position.y, es.position.z, m_uptime);
			}
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

	// ── Network worker ────────────────────────────────────────────────
	// Moves socket-read and zstd decompress off the render thread. The worker
	// reads frames, decompresses S_CHUNK_Z payloads (pushed as S_CHUNK — the
	// decompressed layout matches an uncompressed chunk frame byte-for-byte),
	// and enqueues QueuedMsg's. The main thread calls drainQueue() once per
	// tick; handleMessage still runs on the main thread, so all mutation of
	// m_entities / m_world / m_reconciler stays single-threaded. TCP sends
	// stay on the main thread — kernel allows concurrent send/recv on one fd.
	struct QueuedMsg {
		uint32_t type;
		std::vector<uint8_t> payload;
	};

	void startNetThread() {
		if (m_netThread.joinable()) return;
		m_netStop.store(false, std::memory_order_relaxed);
		m_netError.store(false, std::memory_order_relaxed);
		m_netThread = std::thread(&NetworkServer::netThreadLoop, this);
	}

	void stopNetThread() {
		if (!m_netThread.joinable()) return;
		m_netStop.store(true, std::memory_order_relaxed);
		// The fd is closed by the caller (disconnect) which wakes the worker
		// out of readFrom() with EBADF. If we're called from the destructor
		// without a prior disconnect, the socket is still open — the stop
		// flag + the 1ms idle sleep bounds the shutdown wait.
		m_netThread.join();
	}

	void netThreadLoop() {
#ifndef __EMSCRIPTEN__
		while (!m_netStop.load(std::memory_order_relaxed)) {
			if (!m_recv.readFrom(m_tcp.fd())) {
				m_netError.store(true, std::memory_order_relaxed);
				return;
			}
			net::MsgHeader hdr;
			std::vector<uint8_t> payload;
			bool extracted = false;
			while (m_recv.tryExtract(hdr, payload)) {
				extracted = true;
				uint32_t type = hdr.type;
				std::vector<uint8_t> out;
				if (type == net::S_CHUNK_Z) {
					// Decompress here so the main thread sees a regular
					// S_CHUNK payload — same x/y/z/block/appearance/annotation
					// layout, no zstd cost on the render thread.
					size_t compSize = payload.size();
					const uint8_t* compData = payload.data();
					size_t decompBound = ZSTD_getFrameContentSize(compData, compSize);
					if (decompBound == ZSTD_CONTENTSIZE_ERROR || decompBound == ZSTD_CONTENTSIZE_UNKNOWN) {
						fprintf(stderr, "[Net] bad zstd frame\n");
						continue;
					}
					out.resize(decompBound);
					size_t actual = ZSTD_decompress(out.data(), out.size(), compData, compSize);
					if (ZSTD_isError(actual)) {
						fprintf(stderr, "[Net] zstd error: %s\n", ZSTD_getErrorName(actual));
						continue;
					}
					out.resize(actual);
					type = net::S_CHUNK;
				} else {
					out = std::move(payload);
				}
				{
					std::lock_guard<std::mutex> lk(m_queueMu);
					m_queue.push_back({type, std::move(out)});
				}
			}
			if (!extracted) {
				// readFrom on a non-blocking socket returns immediately when
				// no data is ready. 1ms idle keeps us out of a busy spin while
				// still responding to bursts within one frame.
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}
#endif
	}

	// Main-thread drain with a per-frame time budget. Pull fresh messages
	// from the worker into m_pending (main-thread-only, FIFO preserved),
	// then dispatch up to kBudgetMs; leftovers carry to the next frame.
	// Prevents a burst of S_CHUNK after S_READY from blowing the frame
	// budget — we trade a few frames of catch-up for smoother FPS.
	int drainQueue() {
		if (!m_connected) return 0;
		if (m_netError.load(std::memory_order_relaxed)) return -1;
		{
			std::lock_guard<std::mutex> lk(m_queueMu);
			if (!m_queue.empty()) {
				m_pending.reserve(m_pending.size() + m_queue.size());
				for (auto& m : m_queue) m_pending.push_back(std::move(m));
				m_queue.clear();
			}
		}
		if (m_pending.empty()) return 0;

		// 3ms cap per frame. Checked after *every* message — not every 4 —
		// because one unlucky S_CHUNK already blows the budget by itself.
		// A single over-budget message still completes (we can't split it),
		// so worst-case frame overshoot = cost of one handleMessage.
		// 3ms cap per frame. Checked after every message — a single over-
		// budget handleMessage still completes (we can't split it), so worst
		// case per frame = cost of one message. With the agent-scan lock fix
		// that's ≤1ms; without it, S_CHUNK / S_CHUNK_EVICT could block on the
		// agent's shared_lock for 70-100ms.
		constexpr double kBudgetMs = 3.0;
		auto t0 = std::chrono::steady_clock::now();
		size_t i = 0;
		for (; i < m_pending.size(); i++) {
			handleMessage(m_pending[i].type, m_pending[i].payload);
			auto elapsed = std::chrono::duration<double,std::milli>(
				std::chrono::steady_clock::now() - t0).count();
			if (elapsed >= kBudgetMs) { i++; break; }
		}
		int processed = (int)i;
		m_pending.erase(m_pending.begin(), m_pending.begin() + i);
		return processed;
	}

	// Sole socket consumer. readFrom() + extract-all → handleMessage().
	// Returns messages drained, or -1 if the socket closed.
	int drainSocket() {
		if (!m_connected) return 0;
		if (!m_recv.readFrom(m_tcp.fd())) return -1;
		net::MsgHeader hdr;
		std::vector<uint8_t> payload;
		int n = 0;
		while (m_recv.tryExtract(hdr, payload)) {
			handleMessage(hdr.type, payload);
			n++;
		}
		return n;
	}

	void handleMessage(uint32_t type, const std::vector<uint8_t>& payload) {
		net::ReadBuffer rb(payload.data(), payload.size());

		switch (type) {
		case net::S_WELCOME: {
			if (m_welcomeReceived) break; // defensive — server shouldn't resend
			m_localPlayerId = rb.readU32();
			m_spawnPos = rb.readVec3();
			m_welcomeReceived = true;
			printf("[Net] Welcome! Player ID=%u, spawn=(%.1f,%.1f,%.1f)\n",
				m_localPlayerId, m_spawnPos.x, m_spawnPos.y, m_spawnPos.z);
			break;
		}
		case net::S_PREPARING: {
			float pct = rb.readF32();
			m_preparingPct = pct;
			static float s_lastLoggedPct = -1.0f;
			if (pct - s_lastLoggedPct >= 0.1f || pct >= 1.0f) {
				printf("[Net] Preparing world: %.0f%%\n", pct * 100.0f);
				s_lastLoggedPct = pct;
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
			// v5+: [u8×CHUNK_VOLUME] appearance array, before annotations.
			if (rb.remaining() >= (size_t)CHUNK_VOLUME) {
				for (int ly = 0; ly < CHUNK_SIZE; ly++)
					for (int lz = 0; lz < CHUNK_SIZE; lz++)
						for (int lx = 0; lx < CHUNK_SIZE; lx++)
							chunk->setAppearance(lx, ly, lz, rb.readU8());
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
			if (zrb.remaining() >= (size_t)CHUNK_VOLUME) {
				for (int ly = 0; ly < CHUNK_SIZE; ly++)
					for (int lz = 0; lz < CHUNK_SIZE; lz++)
						for (int lx = 0; lx < CHUNK_SIZE; lx++)
							chunk->setAppearance(lx, ly, lz, zrb.readU8());
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
		case net::S_PONG: {
			uint64_t sentUs = rb.readU64();
			uint64_t nowUs  = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			double rttMs = (nowUs - sentUs) / 1000.0;
			m_lastRttMs = (float)rttMs;
			// Only record post-handshake. Pre-READY pings would sit alongside
			// chunk streams and skew the distribution toward setup cost.
			if (m_serverReady) {
				PERF_RECORD_MS("client.net.rtt_ms", rttMs);
				PERF_COUNT("client.net.pings");
			}
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
			uint8_t appearance = rb.hasMore() ? rb.readU8() : 0;  // v5+
			auto div = [](int a, int b) { return (a >= 0) ? a / b : (a - b + 1) / b; };
			ChunkPos cp = {div(bx, CHUNK_SIZE), div(by, CHUNK_SIZE), div(bz, CHUNK_SIZE)};
			// Only fire dig/place fx if this chunk was already loaded — otherwise
			// getBlock() returns AIR for the "old" block and every routine server
			// tick (grass growth, water flow, fire, doors) in still-streaming
			// chunks sounds like a placed block, producing an overlapping chorus
			// of place_stone/wood/soft during login.
			bool chunkLoaded = (m_world.getChunk(cp) != nullptr);
			BlockId oldBid = chunkLoaded ? m_world.getBlock(bx, by, bz) : bid;
			if (chunkLoaded && bid == BLOCK_AIR && oldBid != BLOCK_AIR && m_onBlockBreakText) {
				const BlockDef& bdef = m_world.blockRegistry().get(oldBid);
				if (bdef.string_id != "air" && !bdef.string_id.empty())
					m_onBlockBreakText(glm::vec3(bx, by, bz),
						bdef.display_name.empty() ? bdef.string_id : bdef.display_name);
			} else if (chunkLoaded && bid != BLOCK_AIR && oldBid != bid && m_onBlockPlace) {
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
			m_world.setAppearance(bx, by, bz, appearance);
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
	bool m_welcomeReceived = false; // set by handleMessage; read by pollWelcome
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
	float m_waitLogTimer = 0;
	int m_totalMsgCount = 0;
	float m_uptime = 0;
	// Per-entity new-entity spam is replaced with a periodic stats line; we
	// still track arrivals since the last diag so the line says something.
	int m_entitiesArrivedSinceLastDiag = 0;

	float m_silentTime = 0;
	bool  m_silentWarned = false;
	// Post-ready: 5s fine. Pre-ready: server may gen chunks synchronously on cold cache,
	// 10-30s gaps between S_PREPARING on first connect.
	static constexpr float kHeartbeatDisconnectSec    = 5.0f;
	static constexpr float kHeartbeatDisconnectPrepSec = 60.0f;

	// Outbound heartbeat resets server's per-client idle timer (enables cleanup on silent clients).
	float m_heartbeatTimer = 0.0f;
	static constexpr float kHeartbeatSendInterval = 2.0f;
	float m_pingTimer = 0.0f;
	static constexpr float kPingSendInterval = 0.5f;   // 2 Hz
	float m_lastRttMs = 0.0f;

	std::function<void(ChunkPos)> m_onChunkDirty;
	std::function<void(glm::vec3, const std::string&)> m_onBlockBreakText;
	std::function<void(glm::vec3, const std::string&)> m_onBlockPlace;
	std::function<void(EntityId)> m_onInventoryUpdate;
	std::function<void(EntityId, const std::string&)> m_onNpcInterrupt;
	std::function<void(const std::string&, const std::string&)> m_onWorldEvent;

	// Network worker. m_recv is touched only on the worker once it starts;
	// tick()/drainQueue() see messages through m_queue exclusively.
	std::thread m_netThread;
	std::atomic<bool> m_netStop{false};
	std::atomic<bool> m_netError{false};
	std::mutex m_queueMu;
	std::vector<QueuedMsg> m_queue;
	// Main-thread leftover from previous drain (budget not reached to here).
	// Never touched by worker; no lock needed.
	std::vector<QueuedMsg> m_pending;
};

} // namespace civcraft

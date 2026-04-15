#pragma once

/**
 * ClientManager — manages TCP client connections for the dedicated server.
 *
 * Extracted from main_server.cpp to keep the main loop clean.
 * Handles: accept, receive, broadcast, chunk streaming, disconnect.
 */

#include "server/server.h"
#include "server/chunk_info.h"
#include "server/chunk_gen_service.h"
#include "shared/net_socket.h"
#include "shared/net_protocol.h"
#include "shared/constants.h"
#include <zstd.h>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <string_view>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <cmath>

namespace civcraft {

// Async-handshake phase. Clients start in Preparing (HELLO received, chunks
// being generated in the background). They transition to Ready once all
// required chunks are delivered and the player entity has been spawned.
// Preparing clients never receive S_ENTITY broadcasts — they have no
// playerId yet, and ownerId==0 would falsely match every unowned entity.
enum class ClientPhase : uint8_t {
	Preparing,
	Ready,
};

// ─── TransportClient ────────────────────────────────────────────────
// Owns the raw TCP pipe to one GUI client: file descriptor, peer address,
// inbound RecvBuffer, and the outbound queue of pre-serialised chunk messages.
// No game state, no session identity — those live on ClientSession. Split
// out so the network plumbing can be tested or swapped (e.g. a WebSocket
// transport for the WASM build) without disturbing game-level code.
struct TransportClient {
	int         fd   = -1;
	std::string ip;
	int         port = 0;

	net::RecvBuffer recvBuf;

	// Outbound chunk messages, already prepended with their 8-byte header.
	// Drained by flushPendingChunks() under a per-tick cap so large prep
	// bursts don't starve the rest of the loop.
	std::vector<std::pair<ChunkPos, std::vector<uint8_t>>> pendingChunks;

	// Count of consecutive send() failures on pendingChunks. Reset on any
	// successful send; escalates to disconnect at the 5th failure.
	int chunkSendErrors = 0;

	bool isOpen() const { return fd >= 0; }
	void close() { if (fd >= 0) { ::close(fd); fd = -1; } }

	// Format the peer address as "ip:port" for log messages.
	std::string addr() const { return ip + ":" + std::to_string(port); }
};

struct ClientSession {
	// TCP transport (fd, recvBuf, pending queue, errors, peer addr).
	TransportClient transport;

	ClientId  id       = 0;
	EntityId  playerId = ENTITY_NONE;

	// Sticky chunk-streaming bookkeeping: the last chunk the player stood in
	// (used to decide when to re-evaluate the streaming region) and the set
	// of chunks already sent (dedup so we never re-send and never evict a
	// chunk that was never delivered).
	ChunkPos                                  lastChunkPos = {0, 0, 0};
	std::unordered_set<ChunkPos, ChunkPosHash> sentChunks;

	std::string name;              // display label assembled from HELLO fields
	float       pendingAge = 0;    // seconds in pending pool before hello received

	// ─── Liveness tracking ──────────────────────────────────────────
	// Seconds since this client last sent ANY message (C_ACTION, C_HEARTBEAT,
	// C_QUIT, …). A well-behaved client keeps this near zero by sending
	// C_HEARTBEAT every few seconds. When it exceeds ServerTuning::
	// clientIdleTimeoutSec, ClientManager drops the client and runs the
	// standard cleanup (snapshot owned NPCs + save inventory + despawn).
	// disconnectQueued is set once the session has been queued for removal
	// so concurrent detectors (TCP close + C_QUIT in the same tick) don't
	// enqueue it twice.
	float idleSec          = 0.0f;
	bool  disconnectQueued = false;

	// Mark "we just got a real message from this client" — resets idle timer.
	void noteActivity() { idleSec = 0.0f; }
	// Advance the idle counter by one frame's dt.
	void accumulateIdle(float dt) { idleSec += dt; }
	bool isStale(float thresholdSec) const { return idleSec > thresholdSec; }

	// Protocol negotiation (set from C_HELLO version field)
	uint32_t protocolVersion = 1;
	bool     supportsZstd    = false; // true when protocolVersion >= 2

	// Async handshake state. Preparing until the background chunk prep
	// finishes; then flipped to Ready by ClientManager::advancePreparing().
	ClientPhase phase = ClientPhase::Ready;
	std::string pendingDisplayName;   // HELLO fields stashed until Ready
	std::string pendingCreatureType;
	size_t requiredChunkCount     = 0;
	size_t chunksCompletedForPrep = 0;
	float  lastProgressSent       = -1.0f; // last pct emitted via S_PREPARING
	// Watchdog: if chunksCompletedForPrep hasn't advanced for kPrepStallSec,
	// tear the session down — otherwise a wedged worker would wait forever
	// for the client's 5s heartbeat to notice the silent stall.
	std::chrono::steady_clock::time_point lastPrepAdvanceAt
		= std::chrono::steady_clock::now();
	size_t lastPrepAdvanceValue = 0;

	// Edge-trigger state for S_NPC_INTERRUPT. True if a player was within
	// proximity radius of this NPC on the previous broadcast tick.
	std::unordered_map<EntityId, bool> prevProximity;

	// Chunk streaming radii.
	// STREAM_R must exceed fogEnd/CHUNK_SIZE so the player never sees void at the fog boundary.
	// fogEnd = 160 blocks → 10 chunks; use 11 for a 1-chunk safety margin.
	static constexpr int STREAM_R     = 11;  // priority streaming radius (must exceed fog end)
	static constexpr int STREAM_FAR_R = 20;  // opportunistic far streaming (distant mountains)
	static constexpr int EVICT_R      = 22;  // evict sentChunks beyond this (STREAM_FAR_R + 2)
	static constexpr int EVICT_DY     =  4;  // vertical eviction radius

	std::string label() const {
		const std::string& ip = transport.ip;
		int port              = transport.port;
		if (!name.empty())
			return "Client " + std::to_string(id) + " (" + name + "@" + ip + ":" + std::to_string(port) + ")";
		return "Client " + std::to_string(id) + " (" + ip + ":" + std::to_string(port) + ")";
	}
};

// Perception predicate — decides whether to broadcast an entity to a given
// client. Lifted to a pure function so test_e2e can assert the invariant
// "every owned entity is broadcast, regardless of distance" without standing
// up a full TCP loopback pair. Match the check in broadcastState() exactly.
inline bool shouldBroadcastEntityToClient(const Entity& e,
                                          EntityId clientPlayerId,
                                          const std::vector<glm::vec3>& viewPoints,
                                          float perceptionR2 = 64.0f * 64.0f) {
	int ownerId = e.getProp<int>(Prop::Owner, 0);
	if (ownerId == (int)clientPlayerId) return true;  // owner-override
	for (auto& vp : viewPoints)
		if (glm::dot(e.position - vp, e.position - vp) <= perceptionR2)
			return true;
	return false;
}

class ClientManager {
public:
	explicit ClientManager(GameServer& server)
		: m_server(server),
		  m_chunkGen(std::make_unique<ChunkGenService>(m_server.world())) {}

	~ClientManager() = default;

	// Set the directory containing civcraft-agent binary.
	// Set the server port (for LAN discovery announcements).
	void setPort(int port) { m_port = port; }

	// Accept new TCP connections and register as clients.
	// Entity creation is deferred to C_HELLO — agents don't need a temp entity.
	void acceptConnections(net::TcpServer& listener) {
		auto accepted = listener.acceptClient();
		if (accepted.fd < 0) return;

		ClientId cid = m_nextClientId++;

		// Hold in pending pool until C_HELLO identifies this client.
		// No entity is created yet — pending clients are invisible to
		// the broadcast + chunk pipeline.
		ClientSession cc;
		cc.transport.fd   = accepted.fd;
		cc.transport.ip   = accepted.ip;
		cc.transport.port = accepted.port;
		cc.id = cid; cc.playerId = ENTITY_NONE;

		auto sp = m_server.spawnPos();
		auto cp = worldToChunk((int)sp.x, (int)sp.y, (int)sp.z);
		cc.lastChunkPos = cp;

		m_pendingClients[cid] = std::move(cc);
		printf("[Server] %s connected. Waiting for hello...\n",
		       m_pendingClients[cid].label().c_str());
	}

	// Drain results from the ChunkGenService worker pool, route each into
	// its client's pendingChunks/sentChunks, emit S_PREPARING progress, and
	// finalize the handshake (spawn player + send S_WELCOME/S_INVENTORY/
	// S_READY) when all required chunks for a Preparing client have arrived.
	void advancePreparing() {
		auto results = m_chunkGen->drain();
		for (auto& r : results) {
			auto it = m_clients.find(r.cid);
			if (it == m_clients.end()) continue; // client disconnected
			auto& c = it->second;
			if (c.sentChunks.count(r.pos)) continue; // dedup (shouldn't happen in prep)
			c.transport.pendingChunks.push_back({r.pos, std::move(r.message)});
			c.sentChunks.insert(r.pos);
			// chunksCompletedForPrep is bumped in sendPendingChunks after the
			// message is actually on the wire — so S_READY (which gates on
			// chunksCompletedForPrep) can't fire while chunks are still queued.
		}

		// Pump progress + finalize. Collect cids first — finalizePreparing
		// may mutate m_clients (it doesn't today but keep this safe).
		std::vector<ClientId> toFinalize;
		std::vector<ClientId> toStall;
		auto now = std::chrono::steady_clock::now();
		constexpr float kPrepStallSec = 30.0f;
		for (auto& [cid, c] : m_clients) {
			if (c.phase != ClientPhase::Preparing) continue;
			float pct = c.requiredChunkCount == 0 ? 1.0f
				: (float)c.chunksCompletedForPrep / (float)c.requiredChunkCount;
			if (pct >= 1.0f) pct = 1.0f;
			// Emit at ~2% granularity to avoid flooding tiny packets.
			if (pct - c.lastProgressSent >= 0.02f || (pct >= 1.0f && c.lastProgressSent < 1.0f)) {
				c.lastProgressSent = pct;
				net::WriteBuffer wb;
				wb.writeF32(pct);
				net::sendMessage(c.transport.fd, net::S_PREPARING, wb);
			}
			if (c.chunksCompletedForPrep >= c.requiredChunkCount) {
				toFinalize.push_back(cid);
				continue;
			}
			// Watchdog — any forward motion resets the clock.
			if (c.chunksCompletedForPrep != c.lastPrepAdvanceValue) {
				c.lastPrepAdvanceValue = c.chunksCompletedForPrep;
				c.lastPrepAdvanceAt = now;
			} else {
				float stalled = std::chrono::duration<float>(now - c.lastPrepAdvanceAt).count();
				if (stalled > kPrepStallSec) toStall.push_back(cid);
			}
		}
		for (auto cid : toStall) {
			auto it = m_clients.find(cid);
			if (it == m_clients.end()) continue;
			printf("[Server] %s prep stalled >%.0fs at %zu/%zu chunks — disconnecting\n",
				it->second.label().c_str(), kPrepStallSec,
				it->second.chunksCompletedForPrep, it->second.requiredChunkCount);
			net::WriteBuffer err;
			err.writeU32(0);
			err.writeString("prep stalled on server");
			net::sendMessage(it->second.transport.fd, net::S_ERROR, err);
			markDisconnect(cid, "prep stalled");
		}
		for (auto cid : toFinalize) {
			auto it = m_clients.find(cid);
			if (it != m_clients.end()) finalizePreparing(it->second);
		}
	}

	// Send queued chunks to clients (non-blocking, batched).
	void sendPendingChunks() {
		for (auto& [cid, client] : m_clients) {
			int sent = 0;
			while (!client.transport.pendingChunks.empty() && sent < 10) {
				auto& msg = client.transport.pendingChunks.front().second;
				ssize_t n = send(client.transport.fd, msg.data(), msg.size(), MSG_NOSIGNAL);
				if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
				if (n <= 0) {
					client.transport.chunkSendErrors++;
					if (client.transport.chunkSendErrors <= 3)
						printf("[Server] sendChunk: n=%zd errno=%d (%s) for %s, pending=%zu\n",
							n, errno, strerror(errno), client.label().c_str(),
							client.transport.pendingChunks.size());
					if (client.transport.chunkSendErrors >= 5) {
						// Persistent send failure — treat as disconnect to avoid zombie client
						markDisconnect(cid, "send errors");
					}
					break;
				}
				if (n != (ssize_t)msg.size()) {
					// Partial send — trim and retry next tick
					if (++client.transport.chunkSendErrors <= 3)
						printf("[Server] sendChunk: partial send n=%zd/%zu for %s\n",
							n, msg.size(), client.label().c_str());
					msg.erase(msg.begin(), msg.begin() + n);
					break;
				}
				client.transport.chunkSendErrors = 0; // reset on success
				client.transport.pendingChunks.erase(client.transport.pendingChunks.begin());
				if (client.phase == ClientPhase::Preparing)
					client.chunksCompletedForPrep++;
				sent++;
			}
		}
	}

	// Receive and dispatch all pending messages from clients.
	// Pending (unidentified) clients are only promoted to active on C_HELLO.
	void receiveMessages(float dt) {
		// --- Pending pool: only accept C_HELLO ---
		struct Hello { ClientId cid; uint32_t type; std::vector<uint8_t> payload; };
		std::vector<Hello> hellos;

		for (auto& [cid, client] : m_pendingClients) {
			if (client.disconnectQueued) continue;
			client.pendingAge += dt;
			if (client.pendingAge > 10.0f) {
				markDisconnect(cid, "no hello within 10s");
				continue;
			}
			if (!client.transport.recvBuf.readFrom(client.transport.fd)) {
				markDisconnect(cid, "tcp closed (pre-hello)");
				continue;
			}
			net::MsgHeader hdr;
			std::vector<uint8_t> payload;
			while (client.transport.recvBuf.tryExtract(hdr, payload)) {
				if (hdr.type == net::C_HELLO) {
					hellos.push_back({cid, hdr.type, std::move(payload)});
					break; // one hello per client per tick
				}
				// drop unrecognised pre-hello messages
			}
		}

		// Move identified clients from pending → active, then dispatch their hello
		for (auto& h : hellos) {
			auto it = m_pendingClients.find(h.cid);
			if (it == m_pendingClients.end()) continue;
			m_clients[h.cid] = std::move(it->second);
			m_pendingClients.erase(it);
			net::ReadBuffer rb(h.payload.data(), h.payload.size());
#ifdef CIVCRAFT_PERF
			auto t0 = std::chrono::steady_clock::now();
#endif
			handleMessage(h.cid, m_clients[h.cid], h.type, rb);
#ifdef CIVCRAFT_PERF
			double ms = std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - t0).count();
			if (ms > 5.0)
				fprintf(stderr, "[Perf] handleMessage HELLO type=%u took %.1fms (cid=%u)\n",
					h.type, ms, h.cid);
#endif
		}

		// --- Active pool: handle all messages ---
		for (auto& [cid, client] : m_clients) {
			if (client.disconnectQueued) continue;
			if (!client.transport.recvBuf.readFrom(client.transport.fd)) {
				markDisconnect(cid, "tcp closed");
				continue;
			}
			net::MsgHeader hdr;
			std::vector<uint8_t> payload;
			while (client.transport.recvBuf.tryExtract(hdr, payload)) {
				// Any well-formed message from the client counts as liveness
				// — reset the idle timer before dispatch so C_HEARTBEAT is
				// purely a keepalive (no handler logic needed).
				client.noteActivity();
				net::ReadBuffer rb(payload.data(), payload.size());
#ifdef CIVCRAFT_PERF
				auto t0 = std::chrono::steady_clock::now();
#endif
				handleMessage(cid, client, hdr.type, rb);
#ifdef CIVCRAFT_PERF
				double ms = std::chrono::duration<double, std::milli>(
					std::chrono::steady_clock::now() - t0).count();
				if (ms > 5.0)
					fprintf(stderr, "[Perf] handleMessage type=%u took %.1fms (cid=%u)\n",
						hdr.type, ms, cid);
#endif
				if (client.disconnectQueued) break; // handler triggered quit — stop draining
			}
		}
	}

	// Sweep active + pending clients for idle timeouts. Any client whose
	// last incoming message is older than ServerTuning::clientIdleTimeoutSec
	// is marked for disconnect, which converges on the same cleanup as a
	// graceful C_QUIT or TCP close (snapshot NPCs + save inventory + despawn).
	// Preparing clients are exempt — they send no application traffic while
	// waiting for chunks; they have their own hello-timeout path.
	void checkIdleTimeouts(float dt) {
		for (auto& [cid, client] : m_clients) {
			if (client.disconnectQueued) continue;
			if (client.phase == ClientPhase::Preparing) continue;
			client.accumulateIdle(dt);
			if (client.isStale(ServerTuning::clientIdleTimeoutSec))
				markDisconnect(cid, "heartbeat timeout");
		}
	}

	// Remove clients that disconnected since last call.
	void pruneDisconnected() {
		for (auto cid : m_disconnected) {
			// Always cancel outstanding chunk-gen jobs so workers don't
			// burn CPU generating chunks for a client that's already gone.
			if (m_chunkGen) m_chunkGen->cancelClient(cid);

			auto pit = m_pendingClients.find(cid);
			if (pit != m_pendingClients.end()) {
				printf("[Server] %s disconnected (no hello).\n", pit->second.label().c_str());
				pit->second.transport.close();
				m_server.removeClient(cid);
				m_pendingClients.erase(pit);
				continue;
			}
			auto it = m_clients.find(cid);
			if (it != m_clients.end()) {
				printf("[Server] %s disconnected.\n", it->second.label().c_str());
				it->second.transport.close();
				m_server.removeClient(cid);
				m_clients.erase(it);
			}
		}
		m_disconnected.clear();
	}

	// Forward pending behavior reloads to controlling agent clients.
	// NOTE: Behavior reload forwarding removed. AgentClient runs in-process
	// with PlayerClient and can reload behaviors directly.

	// Serialize one entity's full state and ship it as S_ENTITY to this client.
	//
	// Shared by two call sites:
	//  1. broadcastState() — the 20 Hz perception-scoped loop for every entity
	//     visible to the client.
	//  2. finalizePreparingImpl() — one eager push of the player's own entity
	//     before S_READY, so the client never enters PLAYING without finding
	//     itself (avoids the 10s "waiting for player entity" timeout).
	//
	// Keeping the serialization in one place guarantees the eager push and the
	// steady broadcast produce byte-identical wire payloads — otherwise the
	// client could observe the player entity with missing props on the first
	// frame and then have them snap in a tick later.
	void sendEntityState(ClientSession& client, Entity& e) {
		net::EntityState es;
		es.id = e.id(); es.typeId = e.typeId();
		es.position = e.position; es.velocity = e.velocity;
		es.yaw = e.yaw; es.onGround = e.onGround;
		es.goalText = e.goalText;
		es.characterSkin = e.getProp<std::string>("character_skin", "");
		es.hp = e.hp(); es.maxHp = e.def().max_hp;
		es.owner = e.getProp<int>(Prop::Owner, 0);
		es.moveTarget = e.moveTarget;
		es.moveSpeed = e.moveSpeed;
		// Player-controlled entities broadcast camera look (for remote head
		// tracking). AI creatures don't set lookYaw/Pitch (defaults to 0 =
		// world +X), so fall back to body yaw → head faces forward.
		bool hasOwner = es.owner != 0;
		es.lookYaw   = hasOwner ? e.lookYaw   : e.yaw;
		es.lookPitch = hasOwner ? e.lookPitch : 0.0f;

		// Skip props that are (a) already encoded in EntityState fields,
		// (b) internal AI bookkeeping, or (c) private (Hunger → owner only).
		bool isOwnEntity = (e.id() == client.playerId);
		auto isPrivateProp = [&](const std::string& k) {
			if (k == Prop::HP) return true;          // already in es.hp
			if (k == Prop::Owner) return true;       // already in es.owner
			if (k == Prop::Goal) return true;        // already in es.goalText
			if (k == "character_skin") return true;  // already in es.characterSkin
			if (k == Prop::WanderTimer) return true; // internal AI state
			if (k == Prop::WanderYaw) return true;   // internal AI state
			if (k == Prop::Age) return true;         // internal bookkeeping
			if (k == Prop::Hunger) return !isOwnEntity; // HUD: owner only
			return false;
		};
		for (auto& [key, val] : e.props()) {
			if (isPrivateProp(key)) continue;
			std::string sv;
			if (auto* s = std::get_if<std::string>(&val)) sv = *s;
			else if (auto* f = std::get_if<float>(&val)) sv = std::to_string(*f);
			else if (auto* i = std::get_if<int>(&val)) sv = std::to_string(*i);
			else if (auto* b = std::get_if<bool>(&val)) sv = *b ? "true" : "false";
			else if (auto* v = std::get_if<glm::vec3>(&val))
				sv = std::to_string(v->x) + "," + std::to_string(v->y) + "," + std::to_string(v->z);
			if (!sv.empty()) es.props.push_back({key, sv});
		}

		net::WriteBuffer wb;
		net::serializeEntityState(wb, es);
		if (!net::sendMessage(client.transport.fd, net::S_ENTITY, wb)) {
			fprintf(stderr, "[ClientMgr] sendEntityState FAILED: client=%s eid=%u type=%s (peer likely gone)\n",
				client.label().c_str(), (unsigned)e.id(), e.typeId().c_str());
			return;
		}
		m_broadcastStats.sEntity++;
	}

	// Broadcast entity state with perception scoping + stream chunks.
	void broadcastState(float dt) {
		m_broadcastTimer += dt;
		if (m_broadcastTimer < ServerTuning::broadcastInterval || m_clients.empty()) return;
		m_broadcastTimer = 0;

		const float PERCEPTION_R2 = 64.0f * 64.0f;

		// Day/night edges (world-wide). Day = [0.25, 0.75), night = the rest.
		float curWT = m_server.worldTime();
		std::string worldEventPhase;
		if (m_prevWorldTime >= 0.0f) {
			auto isDay = [](float t) {
				float f = t - std::floor(t);
				return f >= 0.25f && f < 0.75f;
			};
			bool prevDay = isDay(m_prevWorldTime);
			bool curDay  = isDay(curWT);
			if (prevDay != curDay) worldEventPhase = curDay ? "day" : "night";
		}
		m_prevWorldTime = curWT;

		for (auto& [cid, client] : m_clients) {
			// Preparing clients have no player entity yet. Skip entity/time/
			// chunk-stream broadcasts — they'd either no-op or (worse) match
			// every unowned entity via ownerId==0==ENTITY_NONE==playerId.
			// advancePreparing() owns the prep-phase wire traffic instead.
			if (client.phase == ClientPhase::Preparing) continue;

			// Gather viewpoints for perception scoping (player position only)
			std::vector<glm::vec3> viewPoints;
			Entity* pe = m_server.world().entities.get(client.playerId);
			if (pe) viewPoints.push_back(pe->position);

			// Entity + time broadcast — always fires regardless of pending chunks.
			// Owner-override: entities owned by THIS client's player are ALWAYS
			// broadcast, even beyond PERCEPTION_R2. Rule 4 requires the agent
			// client to drive its owned NPCs, which means it must receive their
			// S_ENTITY updates. Without this override, a wandering mob that
			// steps past the 64-block perception ring goes stale on the owner
			// client (→ red lightbulb, stuck reconciler) while the server keeps
			// simulating it perfectly — the exact false-positive stale signal
			// that was misdiagnosed as "server stopped responding".
			m_server.world().entities.forEach([&](Entity& e) {
				if (!shouldBroadcastEntityToClient(e, client.playerId, viewPoints, PERCEPTION_R2))
					return;
				sendEntityState(client, e);
			});

			net::WriteBuffer tb;
			tb.writeF32(m_server.worldTime());
			net::sendMessage(client.transport.fd, net::S_TIME, tb);

			// ── Event-driven decide-loop interrupts ────────────────────
			// Edge-triggered, owner-scoped: emit only on prev→cur rising
			// edge, only for NPCs owned by this client (so each agent
			// client gets exactly one notification).
			const float r2Prox = ServerTuning::proximityRadius *
			                     ServerTuning::proximityRadius;
			m_server.world().entities.forEach([&](Entity& e) {
				int owner = e.getProp<int>(Prop::Owner, 0);
				if (owner != (int)client.playerId) return;
				if (!e.def().isLiving() || e.removed) return;
				const std::string& bid = e.getProp<std::string>(Prop::BehaviorId, "");
				if (bid.empty()) return;

				bool curClose = false;
				for (auto& vp : viewPoints) {
					glm::vec3 d = e.position - vp;
					if (glm::dot(d, d) <= r2Prox) { curClose = true; break; }
				}
				bool prevClose = client.prevProximity[e.id()];
				if (curClose && !prevClose) {
					net::WriteBuffer b;
					b.writeU32(e.id());
					b.writeString("proximity");
					net::sendMessage(client.transport.fd, net::S_NPC_INTERRUPT, b);
				}
				client.prevProximity[e.id()] = curClose;
			});

			if (!worldEventPhase.empty()) {
				net::WriteBuffer b;
				b.writeString("time_of_day");
				b.writeString(worldEventPhase);
				net::sendMessage(client.transport.fd, net::S_WORLD_EVENT, b);
			}

			// Chunk streaming — two-tier: near (priority) then far (opportunistic).
			// Near tier covers the full view frustum so the player never sees void.
			// Far tier loads distant terrain (mountains, vistas) when near is caught up.
			Entity* streamAnchor = pe;
			if (streamAnchor) {
				auto cp = worldToChunk((int)streamAnchor->position.x, (int)streamAnchor->position.y, (int)streamAnchor->position.z);

				// Dynamic vertical range: when flying, terrain is many chunk-heights below.
				// Cover from ground level (chunk Y=0) up to 3 chunks above player.
				// Player at Y=80 → cp.y=5 → dyMin=-7 reaches chunk Y=-2 (below ground).
				// Capped at -14 to avoid scanning absurd depths.
				int dyMin = -std::min(cp.y + 2, 14);
				int dyMax = 3;

				// Surface chunk Y for this player's XZ column (used for eviction)
				int groundY  = (int)m_server.world().surfaceHeight((float)streamAnchor->position.x, (float)streamAnchor->position.z);
				int groundChunkY = groundY / CHUNK_SIZE;

				// Evict far chunks when the player moves to a new chunk column
				if (cp != client.lastChunkPos) {
					client.lastChunkPos = cp;
					evictFarChunks(client, cp, groundChunkY);
				}

				// 3-D look direction for view-biased chunk priority.
				// When pitching down (looking at a valley from a cliff), fwdY < 0
				// so chunks below the player get a lower (better) biasedDist.
				// Use lookYaw/lookPitch (camera direction) — NOT entity yaw which is
				// the movement-derived facing direction and may differ in RPG/RTS.
				float yaw_rad   = glm::radians(streamAnchor->lookYaw);
				float pitch_rad = glm::radians(std::clamp(streamAnchor->lookPitch, -89.0f, 89.0f));
				float cosPitch  = std::cos(pitch_rad);
				float fwdX = std::cos(yaw_rad) * cosPitch;
				float fwdY = -std::sin(pitch_rad);   // negative pitch = looking down
				float fwdZ = std::sin(yaw_rad) * cosPitch;

				struct Candidate { ChunkPos pos; int dist; };

				// ── Tier 1: near radius (R=STREAM_R) — must have no void at fog boundary ──
				// No hasChunk() guard — force generation of new areas eagerly.
				// dy range is dynamic so ground is always included regardless of altitude.
				if (client.transport.pendingChunks.size() < 40) {
					std::vector<Candidate> near;
					const int R = ClientSession::STREAM_R;
					for (int dy = dyMin; dy <= dyMax; dy++)
					for (int dz = -R; dz <= R; dz++)
					for (int dx = -R; dx <= R; dx++) {
						ChunkPos pos = {cp.x + dx, cp.y + dy, cp.z + dz};
						if (client.sentChunks.count(pos)) continue;
						int baseDist = std::abs(dx) + std::abs(dz) + std::abs(dy) * 2;
						// 3-D look bias: chunks in the camera's actual look direction load first
						float dot = fwdX * dx + fwdY * dy + fwdZ * dz;
						int biasedDist = baseDist - (int)(dot * 1.5f);
						near.push_back({pos, biasedDist});
					}
					std::sort(near.begin(), near.end(),
					          [](const Candidate& a, const Candidate& b) { return a.dist < b.dist; });
					int queued = 0;
					for (auto& c : near) {
						if (queued >= 6 || client.transport.pendingChunks.size() >= 40) break;
						queueChunk(client, c.pos);
						queued++;
					}
				}

				// ── Tier 2: far radius (R=STREAM_FAR_R) — opportunistic, 1 chunk/tick ──
				// Only runs when near tier is fully satisfied (pendingChunks empty).
				// Loads distant terrain (mountains, scouting from high ground).
				// Also uses dynamic vertical range so looking down from altitude works.
				if (client.transport.pendingChunks.empty()) {
					const int NEAR = ClientSession::STREAM_R;
					const int FAR  = ClientSession::STREAM_FAR_R;
					Candidate best{};
					bool found = false;
					for (int dy = dyMin; dy <= dyMax; dy++)
					for (int dz = -FAR; dz <= FAR; dz++)
					for (int dx = -FAR; dx <= FAR; dx++) {
						if (std::abs(dx) <= NEAR && std::abs(dz) <= NEAR &&
						    dy >= dyMin && dy <= dyMax) continue; // already covered by near tier
						ChunkPos pos = {cp.x + dx, cp.y + dy, cp.z + dz};
						if (client.sentChunks.count(pos)) continue;
						int baseDist   = std::abs(dx) + std::abs(dz) + std::abs(dy) * 2;
						float dot      = fwdX * dx + fwdY * dy + fwdZ * dz;
						int biasedDist = baseDist - (int)(dot * 2.0f);
						if (!found || biasedDist < best.dist) {
							best  = {pos, biasedDist};
							found = true;
						}
					}
					if (found) queueChunk(client, best.pos);
				}
			}
		}
	}

	// Log server status.
	void logStatus(float& statusTimer, int& tickCount, FILE* logFile) {
		if (statusTimer < ServerTuning::statusLogInterval) return;

		int moving = 0;
		m_server.world().entities.forEach([&](Entity& e) {
			float hSpeed = std::sqrt(e.velocity.x * e.velocity.x + e.velocity.z * e.velocity.z);
			if (hSpeed > 0.01f) moving++;
		});
		printf("[Server] %d ticks, %.1f tps, %zu entities (%d moving), %zu clients\n",
		       tickCount, tickCount / statusTimer,
		       m_server.world().entities.count(), moving, m_clients.size());
		if (logFile) {
			fprintf(logFile, "[Server] %d ticks, %.1f tps, %zu entities (%d moving), %zu clients\n",
			        tickCount, tickCount / statusTimer,
			        m_server.world().entities.count(), moving, m_clients.size());
		}
		for (auto& [cid, c] : m_clients) {
			auto* pe = m_server.world().entities.get(c.playerId);
		}
		tickCount = 0;
		statusTimer = 0;
	}

	// Broadcast server presence on the LAN so clients can discover it.
	// Call from the main loop with the frame delta time.
	void announceOnLAN(float dt) {
		if (m_port <= 0) return;
		m_announceTimer += dt;
		if (m_announceTimer < 2.0f) return;
		m_announceTimer = 0.0f;

		if (!m_announceUdp.isOpen())
			if (!m_announceUdp.open(0, true)) return;

		int humans = (int)m_clients.size();

		char msg[64];
		snprintf(msg, sizeof(msg), "CIVCRAFT %d %d", m_port, humans);
		m_announceUdp.broadcast(msg, (int)strlen(msg), CIVCRAFT_DISCOVER_PORT);
	}

	// NOTE: Agent processes removed. AgentClient now runs inside PlayerClient.
	// Server no longer spawns or manages agent processes.
	// NPCs are assigned to the PlayerClient that owns them.

	// Close all client connections.
	void disconnectAll() {
		// Route through GameServer::removeClient so owned NPCs are snapshotted
		// and player inventories saved, exactly as on a normal TCP disconnect.
		// Without this step, a server shutdown loses everything the disconnect
		// path normally persists.
		for (auto& [cid, client] : m_clients) {
			client.transport.close();
			m_server.removeClient(cid);
		}
		m_clients.clear();
		for (auto& [cid, client] : m_pendingClients)
			client.transport.close();
		m_pendingClients.clear();
	}

	size_t clientCount() const { return m_clients.size(); }

	// Broadcast activity counters — main_server.cpp reads these in the
	// [ServerAlive] log to confirm the server is actually pushing data to
	// every connected client (not just receiving C_ACTION). Reset externally.
	struct BroadcastStats {
		int sEntity    = 0;
		int sBlock     = 0;
		int sRemove    = 0;
		int sInventory = 0;
	};
	const BroadcastStats& broadcastStats() const { return m_broadcastStats; }
	void resetBroadcastStats() { m_broadcastStats = {}; }

	// Access for callbacks (block change, entity remove, inventory)
	void broadcastToAll(net::MsgType msgType, const net::WriteBuffer& wb) {
		for (auto& [cid, c] : m_clients)
			net::sendMessage(c.transport.fd, msgType, wb);
		switch (msgType) {
		case net::S_BLOCK:     m_broadcastStats.sBlock     += (int)m_clients.size(); break;
		case net::S_REMOVE:    m_broadcastStats.sRemove    += (int)m_clients.size(); break;
		case net::S_INVENTORY: m_broadcastStats.sInventory += (int)m_clients.size(); break;
		default: break;
		}
	}

	ClientSession* getClient(ClientId id) {
		auto it = m_clients.find(id);
		return it != m_clients.end() ? &it->second : nullptr;
	}

	// Called when a block changes: broadcast S_BLOCK to all clients and update ChunkInfo.
	void onBlockChanged(glm::ivec3 pos, BlockId oldBid, BlockId newBid, uint8_t p2) {
		// Broadcast S_BLOCK to all clients (player GUI needs this for rendering)
		{
			net::WriteBuffer wb;
			wb.writeI32(pos.x); wb.writeI32(pos.y); wb.writeI32(pos.z);
			wb.writeU32(newBid); wb.writeU8(p2);
			broadcastToAll(net::S_BLOCK, wb);
		}

		// Block break: if the host block had an annotation (flower on grass, …),
		// drop the annotation as a ground item and purge it from the world.
		if (newBid == BLOCK_AIR) {
			auto* ann = m_server.world().getAnnotation(pos.x, pos.y, pos.z);
			if (ann && !ann->empty()) {
				std::string typeId = ann->typeId;
				m_server.world().removeAnnotation(pos.x, pos.y, pos.z);

				// Broadcast removal so clients purge their annotation cache.
				{
					net::WriteBuffer wb;
					wb.writeI32(pos.x); wb.writeI32(pos.y); wb.writeI32(pos.z);
					wb.writeString(std::string()); // empty typeId = remove
					wb.writeU8(0);
					broadcastToAll(net::S_ANNOTATION_SET, wb);
				}

				// Drop as a ground item on top of the broken block.
				glm::vec3 dropPos = glm::vec3(pos) + glm::vec3(0.5f, 0.2f, 0.5f);
				m_server.world().entities.spawn(ItemName::ItemEntity, dropPos,
					{{Prop::ItemType, typeId}, {Prop::Count, 1}, {Prop::Age, 0.0f}});
			}
		}

		// Update ChunkInfo owned by World
		auto div = [](int a, int b) { return (a >= 0) ? a / b : (a - b + 1) / b; };
		ChunkPos cp = {div(pos.x, CHUNK_SIZE), div(pos.y, CHUNK_SIZE), div(pos.z, CHUNK_SIZE)};

		ChunkInfo* ci = m_server.world().getChunkInfo(cp);
		if (!ci) return;

		const std::string& oldTypeId = m_server.world().blocks.get(oldBid).string_id;
		const std::string& newTypeId = m_server.world().blocks.get(newBid).string_id;
		ci->applyBlockChange(pos, oldTypeId, newTypeId);

		// S_CHUNK_INFO_DELTA removed — agents share PlayerClient's chunk cache
	}

private:
	// The one chokepoint that schedules a client for removal. Every
	// disconnect cause — TCP close, C_QUIT, idle heartbeat timeout, hello
	// timeout, skin conflict, persistent send error — routes through here
	// so the cleanup sequence in pruneDisconnected() stays uniform. Idempotent:
	// if the client is already queued, this is a no-op.
	void markDisconnect(ClientId cid, std::string_view reason) {
		auto applyFlag = [&](ClientSession& c) {
			if (c.disconnectQueued) return false;
			c.disconnectQueued = true;
			return true;
		};
		if (auto it = m_clients.find(cid); it != m_clients.end()) {
			if (!applyFlag(it->second)) return;
			printf("[Server] %s queued for disconnect: %.*s\n",
			       it->second.label().c_str(),
			       (int)reason.size(), reason.data());
		} else if (auto pit = m_pendingClients.find(cid); pit != m_pendingClients.end()) {
			if (!applyFlag(pit->second)) return;
			printf("[Server] %s queued for disconnect (pending): %.*s\n",
			       pit->second.label().c_str(),
			       (int)reason.size(), reason.data());
		} else {
			return; // unknown cid — nothing to do
		}
		m_disconnected.push_back(cid);
	}

	// Serialize an inventory into S_INVENTORY wire format:
	// [u32 entityId][u32 n][{str id, i32 count}...][u8 equipCount][{str slot, str id}...]
	static void writeInventoryPayload(net::WriteBuffer& wb, EntityId eid, const Inventory& inv) {
		wb.writeU32(eid);
		auto items = inv.items();
		wb.writeU32((uint32_t)items.size());
		for (auto& [itemId, cnt] : items) { wb.writeString(itemId); wb.writeI32(cnt); }
		uint8_t equipCount = 0;
		for (int i = 0; i < WEAR_SLOT_COUNT; i++)
			if (!inv.equipped((WearSlot)i).empty()) equipCount++;
		wb.writeU8(equipCount);
		for (int i = 0; i < WEAR_SLOT_COUNT; i++) {
			const auto& eq = inv.equipped((WearSlot)i);
			if (!eq.empty()) {
				wb.writeString(equipSlotName((WearSlot)i));
				wb.writeString(eq);
			}
		}
	}

	// Complete the HELLO handshake once ChunkGenService has produced every
	// required chunk for this client. Spawns the player entity (addClient
	// also spawns owned NPCs), restores saved inventory, and emits the
	// S_WELCOME / S_INVENTORY / S_READY sequence the client is waiting for.
	void finalizePreparing(ClientSession& client) {
		try {
			finalizePreparingImpl(client);
		} catch (const std::exception& ex) {
			// Entity spawn can throw via Python artifact errors. Without this
			// the client just sees chunks finish + no S_READY → silent hang
			// until the heartbeat trips. Surface it over S_ERROR instead so
			// the loading screen gets a real reason string.
			std::string reason = std::string("spawn failed: ") + ex.what();
			printf("[Server] finalizePreparing threw: %s (%s)\n",
				ex.what(), client.label().c_str());
			net::WriteBuffer err;
			err.writeU32(0);
			err.writeString(reason);
			net::sendMessage(client.transport.fd, net::S_ERROR, err);
			markDisconnect(client.id, "spawn failed");
		} catch (...) {
			printf("[Server] finalizePreparing threw unknown exception (%s)\n",
				client.label().c_str());
			net::WriteBuffer err;
			err.writeU32(0);
			err.writeString("spawn failed: unknown error");
			net::sendMessage(client.transport.fd, net::S_ERROR, err);
			markDisconnect(client.id, "spawn failed (unknown)");
		}
	}

	void finalizePreparingImpl(ClientSession& client) {
		const std::string& creatureType = client.pendingCreatureType;

#ifdef CIVCRAFT_PERF
		auto t_add0 = std::chrono::steady_clock::now();
#endif
		EntityId eid = m_server.addClient(client.id, creatureType);
#ifdef CIVCRAFT_PERF
		double add_ms = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - t_add0).count();
		if (add_ms > 5.0)
			fprintf(stderr, "[Perf] finalizePreparing.addClient took %.1fms (creature=%s)\n",
				add_ms, creatureType.c_str());
#endif
		client.playerId = eid;

		{
			net::WriteBuffer swb;
			swb.writeU32(eid);
			swb.writeVec3(m_server.spawnPos());
			net::sendMessage(client.transport.fd, net::S_WELCOME, swb);
		}

		Entity* pe = m_server.world().entities.get(eid);
		if (pe && pe->inventory) {
			net::WriteBuffer iwb;
			writeInventoryPayload(iwb, eid, *pe->inventory);
			net::sendMessage(client.transport.fd, net::S_INVENTORY, iwb);
		}

		if (!creatureType.empty() && pe) {
			pe->setProp("character_skin", creatureType);
			if (pe->inventory) {
				auto& saved = m_server.savedInventories();
				auto sit = saved.find(creatureType);
				if (sit != saved.end()) {
					*pe->inventory = sit->second;
					printf("[Server] Restored inventory for '%s'\n", creatureType.c_str());
				}
				net::WriteBuffer wb;
				writeInventoryPayload(wb, client.playerId, *pe->inventory);
				net::sendMessage(client.transport.fd, net::S_INVENTORY, wb);
			}
		}

		client.phase = ClientPhase::Ready;

		// Eager player-entity push — MUST happen before S_READY.
		//
		// Why: the normal path would ship the player's S_ENTITY via the
		// next broadcastState() tick (20 Hz). But after S_READY, the
		// client transitions LOADING → PLAYING and starts a 10s deadline
		// waiting for its own entity to appear. If pendingChunks still
		// has a backlog (common after heavy preparing — we've seen 2,000+
		// chunks queued at this point), the S_ENTITY bytes sit behind
		// ~30 MB of chunk data in the kernel send buffer and arrive too
		// late, tripping the disconnect modal.
		//
		// Doing it eagerly here guarantees S_ENTITY(player) is on the wire
		// before S_READY, so the client's PLAYING-state loop finds
		// playerEntity() non-null on its very first frame.
		if (pe) {
			fprintf(stderr, "[ClientMgr] %s: eager-push S_ENTITY(player) eid=%u before S_READY\n",
				client.label().c_str(), (unsigned)eid);
			sendEntityState(client, *pe);
		} else {
			fprintf(stderr, "[ClientMgr] %s: pe==null, cannot eager-push player entity (eid=%u)\n",
				client.label().c_str(), (unsigned)eid);
		}

		{
			net::WriteBuffer wb;
			net::sendMessage(client.transport.fd, net::S_READY, wb);
		}

		printf("[Server] %s: ready (player eid=%u, %zu chunks queued)\n",
			client.label().c_str(), eid, client.transport.pendingChunks.size());
	}

	void handleMessage(ClientId cid, ClientSession& client, uint32_t type, net::ReadBuffer& rb) {
		switch (type) {
		case net::C_ACTION: {
			auto action = net::deserializeAction(rb);
			m_server.receiveAction(cid, action);
			break;
		}
		case net::C_HELLO: {
			// Wire format: [u32 version][str uuid][str displayName][str creatureType]
			//
			// Enters the Preparing phase: submits every required chunk to the
			// ChunkGenService worker pool and returns immediately so the tick
			// loop keeps running. advancePreparing() drains results each tick
			// and finalizes the handshake (S_WELCOME/S_INVENTORY/S_READY +
			// entity spawn) once all chunks are queued for delivery.
			client.protocolVersion = rb.readU32();
			client.supportsZstd    = (client.protocolVersion >= 2);
			client.name = rb.readString();
			printf("[Server] %s: protocol v%u%s\n",
			       client.label().c_str(), client.protocolVersion,
			       client.supportsZstd ? " (zstd chunks)" : "");
			std::string displayName  = rb.hasMore() ? rb.readString() : "";
			std::string creatureType = rb.hasMore() ? rb.readString() : "";
			if (!displayName.empty())
				client.name = displayName + " (" + client.name.substr(0, 8) + ")";

			// Reject early: one-character-per-server skin lock. Also matches
			// against clients that are still in Preparing with the same skin
			// — otherwise two simultaneous joins could both pass.
			if (!creatureType.empty()) {
				bool skinTaken = false;
				for (auto& [otherId, other] : m_clients) {
					if (otherId == client.id) continue;
					if (other.phase == ClientPhase::Preparing) {
						if (other.pendingCreatureType == creatureType) { skinTaken = true; break; }
						continue;
					}
					Entity* oe = m_server.world().entities.get(other.playerId);
					if (oe && oe->getProp<std::string>("character_skin", "") == creatureType) {
						skinTaken = true;
						break;
					}
				}
				if (skinTaken) {
					net::WriteBuffer err;
					err.writeU32(0);
					err.writeString("Character '" + creatureType + "' is already in use.");
					net::sendMessage(client.transport.fd, net::S_ERROR, err);
					std::string reason = "skin '" + creatureType + "' already online";
					markDisconnect(client.id, reason);
					break;
				}
			}

			client.phase              = ClientPhase::Preparing;
			client.pendingDisplayName = displayName;
			client.pendingCreatureType = creatureType;
			// Reset prep watchdog clock now — its default-initialized value
			// was set at TCP accept, which can be up to ~10s before hello.
			client.lastPrepAdvanceAt    = std::chrono::steady_clock::now();
			client.lastPrepAdvanceValue = 0;

			// Build the required chunk set around the spawn column:
			// feet + feet-1 + (2R+1)² horizontal over dy=[-1..2]. Dedup via a
			// temporary sentChunks reservation.
			ChunkPos feetCp = client.lastChunkPos;
			std::vector<ChunkPos> required;
			auto tryAdd = [&](ChunkPos p) {
				if (!client.sentChunks.insert(p).second) return; // already queued
				required.push_back(p);
			};
			tryAdd(feetCp);
			tryAdd({feetCp.x, feetCp.y - 1, feetCp.z});
			// Prep radius is data-driven per world template (preload_radius_chunks
			// in artifacts/worlds/*.py). Falls back to STREAM_R = 11 via the
			// WorldPyConfig default. Post-spawn streaming still uses STREAM_R.
			const int R = m_server.world().getTemplate().pyConfig().preloadRadiusChunks;
			for (int dy = -1; dy <= 2; dy++)
			for (int dz = -R; dz <= R; dz++)
			for (int dx = -R; dx <= R; dx++)
				tryAdd({feetCp.x + dx, feetCp.y + dy, feetCp.z + dz});

			// Clear the reservation — each chunk flips to "sent" only when the
			// worker's message actually lands in pendingChunks (advancePreparing).
			for (auto& p : required) client.sentChunks.erase(p);

			client.requiredChunkCount    = required.size();
			client.chunksCompletedForPrep = 0;
			client.lastProgressSent       = -1.0f;
			for (auto& p : required)
				m_chunkGen->submit(client.id, p, client.supportsZstd);

			{
				net::WriteBuffer wb;
				wb.writeF32(0.0f);
				net::sendMessage(client.transport.fd, net::S_PREPARING, wb);
				client.lastProgressSent = 0.0f;
			}

			printf("[Server] %s identified: creature=%s; preparing %zu chunks\n",
				client.label().c_str(),
				creatureType.empty() ? "default" : creatureType.c_str(),
				client.requiredChunkCount);
			break;
		}

		// C_RESYNC_CHUNK removed — was agent-only; PlayerClient uses chunk streaming

		// C_AGENT_HELLO removed — agents run inside PlayerClient now
		case net::C_SET_GOAL: {
			uint32_t eid = rb.readU32();
			glm::vec3 gp = {rb.readF32(), rb.readF32(), rb.readF32()};
			if (!m_server.canClientControl(cid, eid)) break;
			Entity* e = m_server.world().entities.get(eid);
			if (e) e->nav.setGoal(gp);
			break;
		}
		case net::C_SET_GOAL_GROUP: {
			glm::vec3 gp = {rb.readF32(), rb.readF32(), rb.readF32()};
			uint32_t count = rb.readU32();
			std::vector<Entity*> group;
			for (uint32_t i = 0; i < count; i++) {
				uint32_t eid = rb.readU32();
				if (!m_server.canClientControl(cid, eid)) continue;
				Entity* e = m_server.world().entities.get(eid);
				if (e) group.push_back(e);
			}
			planGroupFormation(gp, group);
			break;
		}
		case net::C_CANCEL_GOAL: {
			uint32_t eid = rb.readU32();
			Entity* e = m_server.world().entities.get(eid);
			if (e) { e->nav.clear(); e->velocity = {0, 0, 0}; }
			break;
		}
		// C_PROXIMITY removed — agents run inside PlayerClient, no relay needed
		// C_CLAIM_ENTITY removed — ownership is baked in at spawn time.
		case net::C_QUIT: {
			// Graceful shutdown. Same cleanup as a TCP close or an idle timeout
			// — pruneDisconnected() routes through GameServer::removeClient
			// which snapshots owned NPCs and saves inventory.
			markDisconnect(cid, "client quit");
			break;
		}
		case net::C_HEARTBEAT: {
			// No-op. noteActivity() already fired in receiveMessages when this
			// message was extracted; that is the entire point of C_HEARTBEAT.
			break;
		}
		case net::C_GET_INVENTORY: {
			EntityId eid = rb.readU32();
			Entity* target = m_server.world().entities.get(eid);
			Entity* player = m_server.world().entities.get(client.playerId);
			if (!target || !target->inventory || !player) break;
			// Range check: must be within 6 blocks
			float dist = glm::length(target->position - player->position);
			if (dist > 6.0f) break;
			net::WriteBuffer wb;
			writeInventoryPayload(wb, eid, *target->inventory);
			net::sendMessage(client.transport.fd, net::S_INVENTORY, wb);
			break;
		}
		default: break;
		}
	}

	// Remove sentChunks that are now too far from `center` and tell the client to evict them.
	// `groundChunkY` is the chunk Y of the terrain surface at the player's XZ; used so
	// chunks between the player and the ground are never evicted while flying.
	void evictFarChunks(ClientSession& cc, ChunkPos center, int groundChunkY) {
		// Keep everything from groundChunkY-1 up to player+3 vertically
		int dyDown = center.y - std::max(groundChunkY - 1, center.y - ClientSession::EVICT_DY);
		std::vector<ChunkPos> toEvict;
		for (const auto& pos : cc.sentChunks) {
			bool horizFar = std::abs(pos.x - center.x) > ClientSession::EVICT_R ||
			                std::abs(pos.z - center.z) > ClientSession::EVICT_R;
			bool vertFar  = pos.y < center.y - dyDown || pos.y > center.y + 3;
			if (horizFar || vertFar) toEvict.push_back(pos);
		}
		if (toEvict.empty()) return;

		for (const auto& pos : toEvict) {
			cc.sentChunks.erase(pos);
			// ChunkInfo is now owned by World — no eviction needed here
			net::WriteBuffer wb;
			wb.writeI32(pos.x); wb.writeI32(pos.y); wb.writeI32(pos.z);
			net::sendMessage(cc.transport.fd, net::S_CHUNK_EVICT, wb);
		}
		// Cancel any queued (but not yet sent) messages for evicted chunks
		cc.transport.pendingChunks.erase(
			std::remove_if(cc.transport.pendingChunks.begin(), cc.transport.pendingChunks.end(),
				[&](const auto& p) {
					return std::find(toEvict.begin(), toEvict.end(), p.first) != toEvict.end();
				}),
			cc.transport.pendingChunks.end());
	}

	// Force all clients to discard and re-fetch a chunk (e.g. after a bulk terrain change).
	void invalidateChunkForAll(ChunkPos pos) {
		for (auto& [cid, client] : m_clients) {
			if (!client.sentChunks.count(pos)) continue;
			client.sentChunks.erase(pos);
			client.transport.pendingChunks.erase(
				std::remove_if(client.transport.pendingChunks.begin(), client.transport.pendingChunks.end(),
					[&](const auto& p) { return p.first == pos; }),
				client.transport.pendingChunks.end());
			net::WriteBuffer wb;
			wb.writeI32(pos.x); wb.writeI32(pos.y); wb.writeI32(pos.z);
			net::sendMessage(client.transport.fd, net::S_CHUNK_EVICT, wb);
		}
	}

	void queueChunk(ClientSession& cc, ChunkPos pos) {
		if (cc.sentChunks.count(pos)) return;
		Chunk* chunk = m_server.world().getChunk(pos);
		if (!chunk) return;

		// Build uncompressed payload: [i32 cx][i32 cy][i32 cz][u32×4096]
		//                              [u32 annotCount]{[i32 dx][i32 dy][i32 dz][str typeId][u8 slot]}×N
		net::WriteBuffer cb;
		cb.writeI32(pos.x); cb.writeI32(pos.y); cb.writeI32(pos.z);
		for (int i = 0; i < CHUNK_VOLUME; i++)
			cb.writeU32(((uint32_t)chunk->getRawParam2(i) << 16) | chunk->getRaw(i));

		// Annotations piggyback on S_CHUNK so the client can render decorations
		// (flowers, moss, …) the moment the chunk arrives — no extra message.
		auto annots = m_server.world().annotationsInChunk(pos);
		cb.writeU32((uint32_t)annots.size());
		for (auto& [wpos, ann] : annots) {
			cb.writeI32(wpos.x - pos.x * CHUNK_SIZE);
			cb.writeI32(wpos.y - pos.y * CHUNK_SIZE);
			cb.writeI32(wpos.z - pos.z * CHUNK_SIZE);
			cb.writeString(ann.typeId);
			cb.writeU8((uint8_t)ann.slot);
		}

		// Choose message type and payload
		net::MsgType msgType = net::S_CHUNK;
		std::vector<uint8_t> payload;

		if (cc.supportsZstd) {
			size_t srcSize  = cb.size();
			size_t dstBound = ZSTD_compressBound(srcSize);
			payload.resize(dstBound);
			size_t compSize = ZSTD_compress(
				payload.data(), dstBound,
				cb.data().data(), srcSize,
				1 /* level 1 = fastest */);
			payload.resize(compSize);
			msgType = net::S_CHUNK_Z;

			// Log compression ratio once per server run
			static bool s_logged = false;
			if (!s_logged) {
				s_logged = true;
				printf("[Server] zstd chunk: %zu → %zu bytes (%.0f%% saved)\n",
				       srcSize, compSize, 100.0 * (1.0 - (double)compSize / srcSize));
			}
		} else {
			payload.assign(cb.data().begin(), cb.data().end());
		}

		// Prepend 8-byte header and store as a pre-serialised message
		std::vector<uint8_t> msg;
		msg.resize(8 + payload.size());
		net::MsgHeader hdr{ (uint32_t)msgType, (uint32_t)payload.size() };
		memcpy(msg.data(), &hdr, 8);
		memcpy(msg.data() + 8, payload.data(), payload.size());
		cc.transport.pendingChunks.push_back({pos, std::move(msg)});
		cc.sentChunks.insert(pos);
		// S_CHUNK_INFO removed — agents share PlayerClient's chunk cache
	}

	GameServer& m_server;
	std::unique_ptr<ChunkGenService> m_chunkGen;
	std::unordered_map<ClientId, ClientSession> m_clients;         // identified
	std::unordered_map<ClientId, ClientSession> m_pendingClients;  // awaiting hello
	std::vector<ClientId> m_disconnected;
	ClientId m_nextClientId = 1;
	float m_broadcastTimer = 0;
	BroadcastStats m_broadcastStats;

	// Event-driven decide-loop edge tracking. Rising edges on worldTime
	// crossing the 0.25/0.75 day/night boundaries trigger S_WORLD_EVENT.
	// Initialized to -1 so the very first broadcastState call does not
	// emit a spurious edge event.
	float m_prevWorldTime = -1.0f;

	int m_port = 0;

	// LAN discovery
	net::UdpSocket m_announceUdp;
	float m_announceTimer = 0.0f;
};

} // namespace civcraft

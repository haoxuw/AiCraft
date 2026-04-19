#pragma once

// ClientManager — TCP connections for the dedicated server: accept, receive,
// broadcast, chunk streaming, disconnect.

#include "server/server.h"
#include "server/chunk_info.h"
#include "server/chunk_gen_service.h"
#include "net/net_socket.h"
#include "net/net_protocol.h"
#include "logic/constants.h"
#include <zstd.h>
#include <algorithm>
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

// Preparing clients have no playerId — skip broadcast (ownerId==0 would
// match every unowned entity).
enum class ClientPhase : uint8_t {
	Preparing,
	Ready,
};

// Raw TCP pipe; split from ClientSession so transport is swappable (WebSocket/WASM).
struct TransportClient {
	int         fd   = -1;
	std::string ip;
	int         port = 0;

	net::RecvBuffer recvBuf;

	// Pre-serialised; drained with per-tick cap so prep bursts don't starve loop.
	std::vector<std::pair<ChunkPos, std::vector<uint8_t>>> pendingChunks;

	// Disconnect after 5 consecutive send() failures.
	int chunkSendErrors = 0;

	bool isOpen() const { return fd >= 0; }
	void close() { if (fd >= 0) { ::close(fd); fd = -1; } }

	std::string addr() const { return ip + ":" + std::to_string(port); }
};

struct ClientSession {
	TransportClient transport;

	ClientId  id       = 0;
	EntityId  playerId = ENTITY_NONE;

	// Streaming bookkeeping: last chunk player stood in (drives re-eval) + dedup.
	ChunkPos                                  lastChunkPos = {0, 0, 0};
	std::unordered_set<ChunkPos, ChunkPosHash> sentChunks;

	std::string name;
	float       pendingAge = 0;    // seconds in pending pool before hello

	// Idle timer resets on any incoming msg; > ServerTuning::clientIdleTimeoutSec
	// → disconnect. disconnectQueued dedupes concurrent detectors (TCP + C_QUIT).
	float idleSec          = 0.0f;
	bool  disconnectQueued = false;

	void noteActivity() { idleSec = 0.0f; }
	void accumulateIdle(float dt) { idleSec += dt; }
	bool isStale(float thresholdSec) const { return idleSec > thresholdSec; }

	uint32_t protocolVersion = 1;
	bool     supportsZstd    = false; // protocolVersion >= 2

	// Flipped to Ready by advancePreparing() once background chunk prep finishes.
	ClientPhase phase = ClientPhase::Ready;
	std::string pendingDisplayName;   // HELLO fields stashed until Ready
	std::string pendingCreatureType;
	size_t requiredChunkCount     = 0;
	size_t chunksCompletedForPrep = 0;
	float  lastProgressSent       = -1.0f;
	// Watchdog: tear down on no progress for kPrepStallSec (else wedged worker waits for heartbeat).
	std::chrono::steady_clock::time_point lastPrepAdvanceAt
		= std::chrono::steady_clock::now();
	size_t lastPrepAdvanceValue = 0;

	// Edge-trigger state for S_NPC_INTERRUPT (prev tick proximity).
	std::unordered_map<EntityId, bool> prevProximity;

	// Per-client memory of the last EntityState we transmitted, keyed by
	// entity id. Used to compute the field-delta bitmap for S_ENTITY_DELTA
	// (protocol v4+). Cleared on S_REMOVE, and whenever an entity drops out
	// of perception scope (so the re-entry gets a full "first" snapshot).
	std::unordered_map<EntityId, net::EntityState> lastSentEntity;

	// UINT32_MAX sentinel = "never sent" → first broadcast pushes initial weather.
	uint32_t lastWeatherSeq = UINT32_MAX;

	// STREAM_R > fogEnd/CHUNK_SIZE (10) with 1-chunk safety margin = no void at fog.
	static constexpr int STREAM_R     = 11;  // priority
	static constexpr int STREAM_FAR_R = 20;  // opportunistic (distant mountains)
	static constexpr int EVICT_R      = 22;  // STREAM_FAR_R + 2
	static constexpr int EVICT_DY     =  4;

	std::string label() const {
		const std::string& ip = transport.ip;
		int port              = transport.port;
		if (!name.empty())
			return "Client " + std::to_string(id) + " (" + name + "@" + ip + ":" + std::to_string(port) + ")";
		return "Client " + std::to_string(id) + " (" + ip + ":" + std::to_string(port) + ")";
	}
};

// Pure predicate so test_e2e can assert without TCP loopback. MUST match broadcastState().
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

	// Server port (for LAN discovery announcements).
	void setPort(int port) { m_port = port; }

	// Entity creation is deferred to C_HELLO.
	void acceptConnections(net::TcpServer& listener) {
		auto accepted = listener.acceptClient();
		if (accepted.fd < 0) return;

		ClientId cid = m_nextClientId++;

		// Pending pool = invisible to broadcast + chunk pipeline until HELLO.
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

	// Drain ChunkGenService, emit S_PREPARING, finalize (S_WELCOME/S_INVENTORY/S_READY)
	// when all required chunks arrive.
	void advancePreparing() {
		auto results = m_chunkGen->drain();
		for (auto& r : results) {
			auto it = m_clients.find(r.cid);
			if (it == m_clients.end()) continue; // client disconnected
			auto& c = it->second;
			if (c.sentChunks.count(r.pos)) continue; // dedup (shouldn't happen in prep)
			c.transport.pendingChunks.push_back({r.pos, std::move(r.message)});
			c.sentChunks.insert(r.pos);
			// chunksCompletedForPrep is bumped in sendPendingChunks (after wire send)
			// so S_READY can't fire while chunks are still queued.
		}

		// Collect cids first — finalizePreparing may mutate m_clients.
		std::vector<ClientId> toFinalize;
		std::vector<ClientId> toStall;
		auto now = std::chrono::steady_clock::now();
		constexpr float kPrepStallSec = 30.0f;
		for (auto& [cid, c] : m_clients) {
			if (c.phase != ClientPhase::Preparing) continue;
			float pct = c.requiredChunkCount == 0 ? 1.0f
				: (float)c.chunksCompletedForPrep / (float)c.requiredChunkCount;
			if (pct >= 1.0f) pct = 1.0f;
			// ~2% granularity to avoid flooding tiny packets.
			if (pct - c.lastProgressSent >= 0.02f || (pct >= 1.0f && c.lastProgressSent < 1.0f)) {
				c.lastProgressSent = pct;
				net::WriteBuffer wb;
				wb.writeF32(pct);
				net::sendMessage(c.transport.fd, net::S_PREPARING, wb);
			}
			if (c.chunksCompletedForPrep >= c.requiredChunkCount) {
				printf("[Server] %s: prep complete (%zu/%zu chunks delivered) — finalizing handshake\n",
					c.label().c_str(), c.chunksCompletedForPrep, c.requiredChunkCount);
				fflush(stdout);
				toFinalize.push_back(cid);
				continue;
			}
			// Watchdog: forward motion resets the clock.
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

	// Non-blocking, batched.
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
						// Persistent send failure → disconnect to avoid zombie client.
						markDisconnect(cid, "send errors");
					}
					break;
				}
				if (n != (ssize_t)msg.size()) {
					// Partial: trim and retry next tick.
					if (++client.transport.chunkSendErrors <= 3)
						printf("[Server] sendChunk: partial send n=%zd/%zu for %s\n",
							n, msg.size(), client.label().c_str());
					msg.erase(msg.begin(), msg.begin() + n);
					break;
				}
				client.transport.chunkSendErrors = 0;
				client.transport.pendingChunks.erase(client.transport.pendingChunks.begin());
				if (client.phase == ClientPhase::Preparing)
					client.chunksCompletedForPrep++;
				sent++;
			}
		}
	}

	// Pending (unidentified) clients are only promoted to active on C_HELLO.
	void receiveMessages(float dt) {
		// Pending pool: only accept C_HELLO.
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

		// Promote identified clients from pending → active, dispatch hello.
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

		// Active pool: handle all messages.
		for (auto& [cid, client] : m_clients) {
			if (client.disconnectQueued) continue;
			if (!client.transport.recvBuf.readFrom(client.transport.fd)) {
				markDisconnect(cid, "tcp closed");
				continue;
			}
			net::MsgHeader hdr;
			std::vector<uint8_t> payload;
			while (client.transport.recvBuf.tryExtract(hdr, payload)) {
				// Any well-formed message counts as liveness — so C_HEARTBEAT
				// needs no handler logic.
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
				if (client.disconnectQueued) break; // handler triggered quit
			}
		}
	}

	// Converges on same cleanup as C_QUIT/TCP close. Preparing exempt (no app
	// traffic during chunk wait; has its own hello-timeout path).
	void checkIdleTimeouts(float dt) {
		for (auto& [cid, client] : m_clients) {
			if (client.disconnectQueued) continue;
			if (client.phase == ClientPhase::Preparing) continue;
			client.accumulateIdle(dt);
			if (client.isStale(ServerTuning::clientIdleTimeoutSec))
				markDisconnect(cid, "heartbeat timeout");
		}
	}

	void pruneDisconnected() {
		for (auto cid : m_disconnected) {
			// Free workers so they don't burn CPU on a departed client.
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

	// Shared by broadcastState() (steady 20 Hz) and finalizePreparingImpl()
	// (eager push before S_READY so the 10s "waiting for player entity" deadline
	// doesn't trip). One serialization path → byte-identical payloads.
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
		// Players send camera look (remote head tracking); AI creatures fall back
		// to body yaw (lookYaw=0 default would face +X).
		bool hasOwner = es.owner != 0;
		es.lookYaw   = hasOwner ? e.lookYaw   : e.yaw;
		es.lookPitch = hasOwner ? e.lookPitch : 0.0f;

		// Skip fields duplicated in EntityState, AI bookkeeping, or owner-private (Hunger).
		bool isOwnEntity = (e.id() == client.playerId);
		auto isPrivateProp = [&](const std::string& k) {
			if (k == Prop::HP) return true;
			if (k == Prop::Owner) return true;
			if (k == Prop::Goal) return true;
			if (k == "character_skin") return true;
			if (k == Prop::WanderTimer) return true;
			if (k == Prop::WanderYaw) return true;
			if (k == Prop::Age) return true;
			if (k == Prop::Hunger) return !isOwnEntity;
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
		// props comes from unordered_map — sort by key so the delta diff
		// doesn't flag FLD_PROPS on every broadcast because the iteration
		// order shifted after a rehash. Matches our per-client baseline cache.
		std::sort(es.props.begin(), es.props.end(),
		          [](const auto& a, const auto& b) { return a.first < b.first; });

		// v4+: delta-encode against lastSentEntity[eid]. Suppress entirely
		// when mask == 0 (idle entity = zero bytes). v3 still gets full state
		// so older clients keep working.
		if (client.protocolVersion >= 4) {
			auto it = client.lastSentEntity.find(e.id());
			bool isFirst = (it == client.lastSentEntity.end());
			const net::EntityState& prev = isFirst
				? net::EntityState{}   // irrelevant; diff uses isFirst path
				: it->second;
			uint32_t mask = net::diffEntityState(prev, es, isFirst);
			if (mask == 0) return;  // nothing changed — skip broadcast

			net::WriteBuffer wb;
			net::serializeEntityStateDelta(wb, es, mask);
			if (!net::sendMessage(client.transport.fd, net::S_ENTITY_DELTA, wb)) {
				fprintf(stderr, "[ClientMgr] sendEntityState(delta) FAILED: client=%s eid=%u type=%s (peer likely gone)\n",
					client.label().c_str(), (unsigned)e.id(), e.typeId().c_str());
				return;
			}
			client.lastSentEntity[e.id()] = es;
			m_broadcastStats.sEntity++;
			return;
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

	// Perception-scoped entity broadcast + chunk streaming.
	void broadcastState(float dt) {
		m_broadcastTimer += dt;
		if (m_broadcastTimer < ServerTuning::broadcastInterval || m_clients.empty()) return;
		m_broadcastTimer = 0;

		const float PERCEPTION_R2 = 64.0f * 64.0f;

		// World-wide day/night edges. Day = [0.25, 0.75).
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
			// Preparing: advancePreparing() owns their wire traffic.
			if (client.phase == ClientPhase::Preparing) continue;

			std::vector<glm::vec3> viewPoints;
			Entity* pe = m_server.world().entities.get(client.playerId);
			if (pe) viewPoints.push_back(pe->position);

			// Owner-override (Rule 4): owned NPCs broadcast regardless of range,
			// else they go stale past the 64-block ring (red lightbulb false positive).
			m_server.world().entities.forEach([&](Entity& e) {
				if (!shouldBroadcastEntityToClient(e, client.playerId, viewPoints, PERCEPTION_R2))
					return;
				sendEntityState(client, e);
			});

			net::WriteBuffer tb;
			tb.writeF32(m_server.worldTime());
			tb.writeU32(m_server.dayCount());
			net::sendMessage(client.transport.fd, net::S_TIME, tb);

			// Push on seq bump + once per newly-Ready client (UINT32_MAX sentinel).
			// Wind rides along without bumping seq.
			const WeatherState& ws = m_server.weather();
			if (client.lastWeatherSeq != ws.seq) {
				net::WriteBuffer wb;
				wb.writeString(ws.kind);
				wb.writeF32(ws.intensity);
				wb.writeF32(ws.windX);
				wb.writeF32(ws.windZ);
				wb.writeU32(ws.seq);
				net::sendMessage(client.transport.fd, net::S_WEATHER, wb);
				client.lastWeatherSeq = ws.seq;
			}

			// Decide-loop interrupts: edge-triggered, owner-scoped — one notif per agent.
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

			// Two-tier streaming: near (priority) then far (opportunistic, when near drained).
			Entity* streamAnchor = pe;
			if (streamAnchor) {
				auto cp = worldToChunk((int)streamAnchor->position.x, (int)streamAnchor->position.y, (int)streamAnchor->position.z);

				// Dynamic vertical range for flying: ground→player+3; -14 floor.
				int dyMin = -std::min(cp.y + 2, 14);
				int dyMax = 3;

				int groundY  = (int)m_server.world().surfaceHeight((float)streamAnchor->position.x, (float)streamAnchor->position.z);
				int groundChunkY = groundY / CHUNK_SIZE;

				if (cp != client.lastChunkPos) {
					client.lastChunkPos = cp;
					evictFarChunks(client, cp, groundChunkY);
				}

				// Use lookYaw/lookPitch (camera) not entity yaw (may differ in RPG/RTS).
				float yaw_rad   = glm::radians(streamAnchor->lookYaw);
				float pitch_rad = glm::radians(std::clamp(streamAnchor->lookPitch, -89.0f, 89.0f));
				float cosPitch  = std::cos(pitch_rad);
				float fwdX = std::cos(yaw_rad) * cosPitch;
				float fwdY = -std::sin(pitch_rad);
				float fwdZ = std::sin(yaw_rad) * cosPitch;

				struct Candidate { ChunkPos pos; int dist; };

				// Tier 1: near (R=STREAM_R). Force generation eagerly — no hasChunk() guard.
				if (client.transport.pendingChunks.size() < 40) {
					std::vector<Candidate> near;
					const int R = ClientSession::STREAM_R;
					for (int dy = dyMin; dy <= dyMax; dy++)
					for (int dz = -R; dz <= R; dz++)
					for (int dx = -R; dx <= R; dx++) {
						ChunkPos pos = {cp.x + dx, cp.y + dy, cp.z + dz};
						if (client.sentChunks.count(pos)) continue;
						int baseDist = std::abs(dx) + std::abs(dz) + std::abs(dy) * 2;
						// Look-direction bias: chunks the camera faces load first.
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

				// Tier 2: far (R=STREAM_FAR_R), 1 chunk/tick, only when near drained.
				if (client.transport.pendingChunks.empty()) {
					const int NEAR = ClientSession::STREAM_R;
					const int FAR  = ClientSession::STREAM_FAR_R;
					Candidate best{};
					bool found = false;
					for (int dy = dyMin; dy <= dyMax; dy++)
					for (int dz = -FAR; dz <= FAR; dz++)
					for (int dx = -FAR; dx <= FAR; dx++) {
						if (std::abs(dx) <= NEAR && std::abs(dz) <= NEAR &&
						    dy >= dyMin && dy <= dyMax) continue; // covered by near tier
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

	// LAN discovery broadcast.
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

	void disconnectAll() {
		// Route through GameServer::removeClient so NPCs snapshot + inventories save.
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

	// main_server.cpp [ServerAlive] log reads these to confirm server is pushing,
	// not just receiving. Reset externally.
	struct BroadcastStats {
		int sEntity    = 0;
		int sBlock     = 0;
		int sRemove    = 0;
		int sInventory = 0;
	};
	const BroadcastStats& broadcastStats() const { return m_broadcastStats; }
	void resetBroadcastStats() { m_broadcastStats = {}; }

	// Server calls this instead of broadcastToAll(S_REMOVE, ...) so we can
	// drop the dead entity from each client's delta-baseline cache in lockstep.
	// Otherwise lastSentEntity[id] would linger and a future spawn reusing
	// the same id would miss FLD_TYPE_ID and render as the wrong model.
	void broadcastEntityRemove(EntityId id) {
		net::WriteBuffer wb;
		wb.writeU32(id);
		for (auto& [cid, c] : m_clients) {
			net::sendMessage(c.transport.fd, net::S_REMOVE, wb);
			c.lastSentEntity.erase(id);
		}
		m_broadcastStats.sRemove += (int)m_clients.size();
	}

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

	// S_BLOCK broadcast + drop annotations as items on break + update ChunkInfo.
	void onBlockChanged(const BlockChange& bc) {
		const glm::ivec3& pos = bc.pos;
		BlockId oldBid = bc.oldBid;
		BlockId newBid = bc.newBid;
		uint8_t p2 = bc.newP2;
		{
			net::WriteBuffer wb;
			wb.writeI32(pos.x); wb.writeI32(pos.y); wb.writeI32(pos.z);
			wb.writeU32(newBid); wb.writeU8(p2); wb.writeU8(bc.newApp);  // v5+
			broadcastToAll(net::S_BLOCK, wb);
		}

		if (newBid == BLOCK_AIR) {
			auto* ann = m_server.world().getAnnotation(pos.x, pos.y, pos.z);
			if (ann && !ann->empty()) {
				std::string typeId = ann->typeId;
				m_server.world().removeAnnotation(pos.x, pos.y, pos.z);

				{
					net::WriteBuffer wb;
					wb.writeI32(pos.x); wb.writeI32(pos.y); wb.writeI32(pos.z);
					wb.writeString(std::string()); // empty = remove
					wb.writeU8(0);
					broadcastToAll(net::S_ANNOTATION_SET, wb);
				}

				glm::vec3 dropPos = glm::vec3(pos) + glm::vec3(0.5f, 0.2f, 0.5f);
				m_server.world().entities.spawn(ItemName::ItemEntity, dropPos,
					{{Prop::ItemType, typeId}, {Prop::Count, 1}, {Prop::Age, 0.0f}});
			}
		}

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
	// Single chokepoint so pruneDisconnected() stays uniform. Idempotent.
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
			return; // unknown cid
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

	// After all required chunks arrive: spawn (addClient also spawns owned NPCs),
	// restore inventory, emit S_WELCOME/S_INVENTORY/S_READY.
	void finalizePreparing(ClientSession& client) {
		try {
			finalizePreparingImpl(client);
		} catch (const std::exception& ex) {
			// Python artifact errors → S_ERROR so loading screen shows reason
			// instead of hanging until heartbeat trips.
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

		// MUST precede S_READY — client starts a 10s entity-wait deadline after
		// S_READY; pending chunk backlog (~30 MB) would delay steady broadcast.
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

		printf("[Server] %s: S_READY sent — handshake complete (player eid=%u, %zu chunks still queued)\n",
			client.label().c_str(), eid, client.transport.pendingChunks.size());
		fflush(stdout);
	}

	void handleMessage(ClientId cid, ClientSession& client, uint32_t type, net::ReadBuffer& rb) {
		switch (type) {
		case net::C_ACTION: {
			auto action = net::deserializeAction(rb);
			m_server.receiveAction(cid, action);
			break;
		}
		case net::C_HELLO: {
			// Wire: [u32 version][str uuid][str displayName][str creatureType]
			// → Preparing; advancePreparing() drains + finalizes.
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

			// Skin lock: also checks Preparing clients so simultaneous joins can't race.
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
			// Reset prep watchdog (its default was set at TCP accept, ~10s ago).
			client.lastPrepAdvanceAt    = std::chrono::steady_clock::now();
			client.lastPrepAdvanceValue = 0;

			// Required: feet + feet-1 + (2R+1)² horizontal over dy=[-1..2]; sentChunks dedup.
			ChunkPos feetCp = client.lastChunkPos;
			std::vector<ChunkPos> required;
			auto tryAdd = [&](ChunkPos p) {
				if (!client.sentChunks.insert(p).second) return;
				required.push_back(p);
			};
			tryAdd(feetCp);
			tryAdd({feetCp.x, feetCp.y - 1, feetCp.z});
			// preload_radius_chunks in artifacts/worlds/*.py (default STREAM_R).
			const int R = m_server.world().getTemplate().pyConfig().preloadRadiusChunks;
			for (int dy = -1; dy <= 2; dy++)
			for (int dz = -R; dz <= R; dz++)
			for (int dx = -R; dx <= R; dx++)
				tryAdd({feetCp.x + dx, feetCp.y + dy, feetCp.z + dz});

			// Flip to "sent" only when worker message lands in pendingChunks (advancePreparing).
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
		case net::C_QUIT: {
			markDisconnect(cid, "client quit");
			break;
		}
		case net::C_HEARTBEAT: {
			// No-op; noteActivity() in receiveMessages is the entire point.
			break;
		}
		case net::C_GET_INVENTORY: {
			EntityId eid = rb.readU32();
			Entity* target = m_server.world().entities.get(eid);
			Entity* player = m_server.world().entities.get(client.playerId);
			if (!target || !target->inventory || !player) break;
			// Range: 6 blocks.
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

	// Evict sentChunks too far from `center`; groundChunkY preserves ground→player column while flying.
	void evictFarChunks(ClientSession& cc, ChunkPos center, int groundChunkY) {
		// Keep groundChunkY-1 up to player+3 vertically.
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
			net::WriteBuffer wb;
			wb.writeI32(pos.x); wb.writeI32(pos.y); wb.writeI32(pos.z);
			net::sendMessage(cc.transport.fd, net::S_CHUNK_EVICT, wb);
		}
		// Cancel queued-but-unsent messages for evicted chunks.
		cc.transport.pendingChunks.erase(
			std::remove_if(cc.transport.pendingChunks.begin(), cc.transport.pendingChunks.end(),
				[&](const auto& p) {
					return std::find(toEvict.begin(), toEvict.end(), p.first) != toEvict.end();
				}),
			cc.transport.pendingChunks.end());
	}

	// Force all clients to re-fetch a chunk (bulk terrain change).
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

		// Payload: [i32 cx][i32 cy][i32 cz][u32×4096][u8×4096 appearance] (v5+)
		//          [u32 annotCount]{[i32 dx][i32 dy][i32 dz][str typeId][u8 slot]}×N
		net::WriteBuffer cb;
		cb.writeI32(pos.x); cb.writeI32(pos.y); cb.writeI32(pos.z);
		for (int i = 0; i < CHUNK_VOLUME; i++)
			cb.writeU32(((uint32_t)chunk->getRawParam2(i) << 16) | chunk->getRaw(i));
		for (int i = 0; i < CHUNK_VOLUME; i++)
			cb.writeU8(chunk->getRawAppearance(i));

		// Annotations piggyback on S_CHUNK so decorations render immediately.
		auto annots = m_server.world().annotationsInChunk(pos);
		cb.writeU32((uint32_t)annots.size());
		for (auto& [wpos, ann] : annots) {
			cb.writeI32(wpos.x - pos.x * CHUNK_SIZE);
			cb.writeI32(wpos.y - pos.y * CHUNK_SIZE);
			cb.writeI32(wpos.z - pos.z * CHUNK_SIZE);
			cb.writeString(ann.typeId);
			cb.writeU8((uint8_t)ann.slot);
		}

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

			// Log ratio once per run.
			static bool s_logged = false;
			if (!s_logged) {
				s_logged = true;
				printf("[Server] zstd chunk: %zu → %zu bytes (%.0f%% saved)\n",
				       srcSize, compSize, 100.0 * (1.0 - (double)compSize / srcSize));
			}
		} else {
			payload.assign(cb.data().begin(), cb.data().end());
		}

		// Prepend 8-byte header; store as a pre-serialised message.
		std::vector<uint8_t> msg;
		msg.resize(8 + payload.size());
		net::MsgHeader hdr{ (uint32_t)msgType, (uint32_t)payload.size() };
		memcpy(msg.data(), &hdr, 8);
		memcpy(msg.data() + 8, payload.data(), payload.size());
		cc.transport.pendingChunks.push_back({pos, std::move(msg)});
		cc.sentChunks.insert(pos);
	}

	GameServer& m_server;
	std::unique_ptr<ChunkGenService> m_chunkGen;
	std::unordered_map<ClientId, ClientSession> m_clients;         // identified
	std::unordered_map<ClientId, ClientSession> m_pendingClients;  // awaiting hello
	std::vector<ClientId> m_disconnected;
	ClientId m_nextClientId = 1;
	float m_broadcastTimer = 0;
	BroadcastStats m_broadcastStats;

	// -1 so first tick doesn't emit a spurious day/night edge.
	float m_prevWorldTime = -1.0f;

	int m_port = 0;

	net::UdpSocket m_announceUdp;
	float m_announceTimer = 0.0f;
};

} // namespace civcraft

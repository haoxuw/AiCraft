#pragma once

/**
 * ClientManager — manages TCP client connections for the dedicated server.
 *
 * Extracted from main_server.cpp to keep the main loop clean.
 * Handles: accept, receive, broadcast, chunk streaming, disconnect.
 */

#include "server/server.h"
#include "server/chunk_info.h"
#include "shared/net_socket.h"
#include "shared/net_protocol.h"
#include "shared/constants.h"
#include <zstd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <csignal>
#include <sys/wait.h>
#include <cmath>
#include <filesystem>

namespace modcraft {

struct ConnectedClient {
	int fd;
	ClientId id;
	EntityId playerId;
	net::RecvBuffer recvBuf;
	std::vector<std::pair<ChunkPos, std::vector<uint8_t>>> pendingChunks;
	ChunkPos lastChunkPos = {0, 0, 0};
	std::unordered_set<ChunkPos, ChunkPosHash> sentChunks;
	std::string ip;
	int port = 0;
	std::string name;
	bool isAgent = false;
	int chunkSendErrors = 0;
	float pendingAge = 0; // seconds in pending pool before hello received

	// Protocol negotiation (set from C_HELLO version field)
	uint32_t protocolVersion = 1;
	bool supportsZstd = false; // true when protocolVersion >= 2

	// Chunk streaming radii.
	// STREAM_R must exceed fogEnd/CHUNK_SIZE so the player never sees void at the fog boundary.
	// fogEnd = 160 blocks → 10 chunks; use 11 for a 1-chunk safety margin.
	static constexpr int STREAM_R     = 11;  // priority streaming radius (must exceed fog end)
	static constexpr int STREAM_FAR_R = 20;  // opportunistic far streaming (distant mountains)
	static constexpr int EVICT_R      = 22;  // evict sentChunks beyond this (STREAM_FAR_R + 2)
	static constexpr int EVICT_DY     =  4;  // vertical eviction radius

	std::string label() const {
		if (!name.empty())
			return "Client " + std::to_string(id) + " (" + name + "@" + ip + ":" + std::to_string(port) + ")";
		return "Client " + std::to_string(id) + " (" + ip + ":" + std::to_string(port) + ")";
	}
};

class ClientManager {
public:
	explicit ClientManager(GameServer& server) : m_server(server) {}

	~ClientManager() { stopAllAIClients(); }

	// Set the directory containing modcraft-agent binary.
	// Call before the main loop. If empty, AI client spawning is disabled.
	void setExecDir(const std::string& dir) { m_execDir = dir; }

	// Set the server port (needed to tell AI clients where to connect).
	void setPort(int port) { m_port = port; }

	// Accept new TCP connections and register as clients.
	// Entity creation is deferred to C_HELLO — agents don't need a temp entity.
	void acceptConnections(net::TcpServer& listener) {
		auto accepted = listener.acceptClient();
		if (accepted.fd < 0) return;

		ClientId cid = m_nextClientId++;

		// Hold in pending pool until C_HELLO/C_AGENT_HELLO identifies this client.
		// No entity is created yet — prevents ghost entities from agent connections.
		// Pending clients are invisible to the broadcast + chunk pipeline, so
		// there is no timing dependency between C_HELLO arrival and broadcastState().
		ConnectedClient cc;
		cc.fd = accepted.fd; cc.id = cid; cc.playerId = ENTITY_NONE;
		cc.ip = accepted.ip; cc.port = accepted.port;

		auto sp = m_server.spawnPos();
		auto cp = worldToChunk((int)sp.x, (int)sp.y, (int)sp.z);
		cc.lastChunkPos = cp;

		m_pendingClients[cid] = std::move(cc);
		printf("[Server] %s connected. Waiting for hello...\n",
		       m_pendingClients[cid].label().c_str());
	}

	// Send queued chunks to clients (non-blocking, batched).
	void sendPendingChunks() {
		for (auto& [cid, client] : m_clients) {
			int sent = 0;
			while (!client.pendingChunks.empty() && sent < 10) {
				auto& msg = client.pendingChunks.front().second;
				ssize_t n = send(client.fd, msg.data(), msg.size(), MSG_NOSIGNAL);
				if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
				if (n <= 0) {
					client.chunkSendErrors++;
					if (client.chunkSendErrors <= 3)
						printf("[Server] sendChunk: n=%zd errno=%d (%s) for %s, pending=%zu\n",
							n, errno, strerror(errno), client.label().c_str(),
							client.pendingChunks.size());
					if (client.chunkSendErrors >= 5) {
						// Persistent send failure — treat as disconnect to avoid zombie client
						printf("[Server] %s: too many send errors, disconnecting\n",
						       client.label().c_str());
						m_disconnected.push_back(cid);
					}
					break;
				}
				if (n != (ssize_t)msg.size()) {
					// Partial send — trim and retry next tick
					if (++client.chunkSendErrors <= 3)
						printf("[Server] sendChunk: partial send n=%zd/%zu for %s\n",
							n, msg.size(), client.label().c_str());
					msg.erase(msg.begin(), msg.begin() + n);
					break;
				}
				client.chunkSendErrors = 0; // reset on success
				client.pendingChunks.erase(client.pendingChunks.begin());
				sent++;
			}
		}
	}

	// Receive and dispatch all pending messages from clients.
	// Pending (unidentified) clients are only promoted to active on C_HELLO/C_AGENT_HELLO.
	// This eliminates all timing races between C_HELLO arrival and broadcastState().
	void receiveMessages(float dt) {
		// --- Pending pool: only accept C_HELLO / C_AGENT_HELLO ---
		struct Hello { ClientId cid; uint32_t type; std::vector<uint8_t> payload; };
		std::vector<Hello> hellos;

		for (auto& [cid, client] : m_pendingClients) {
			client.pendingAge += dt;
			if (client.pendingAge > 10.0f) {
				printf("[Server] %s: timed out waiting for hello, dropping\n",
				       client.label().c_str());
				m_disconnected.push_back(cid);
				continue;
			}
			if (!client.recvBuf.readFrom(client.fd)) {
				m_disconnected.push_back(cid);
				continue;
			}
			net::MsgHeader hdr;
			std::vector<uint8_t> payload;
			while (client.recvBuf.tryExtract(hdr, payload)) {
				if (hdr.type == net::C_HELLO || hdr.type == net::C_AGENT_HELLO) {
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
			handleMessage(h.cid, m_clients[h.cid], h.type, rb);
		}

		// --- Active pool: handle all messages ---
		for (auto& [cid, client] : m_clients) {
			if (!client.recvBuf.readFrom(client.fd)) {
				m_disconnected.push_back(cid);
				continue;
			}
			net::MsgHeader hdr;
			std::vector<uint8_t> payload;
			while (client.recvBuf.tryExtract(hdr, payload)) {
				net::ReadBuffer rb(payload.data(), payload.size());
				handleMessage(cid, client, hdr.type, rb);
			}
		}
	}

	// Remove clients that disconnected since last call.
	void pruneDisconnected() {
		for (auto cid : m_disconnected) {
			auto pit = m_pendingClients.find(cid);
			if (pit != m_pendingClients.end()) {
				printf("[Server] %s disconnected (no hello).\n", pit->second.label().c_str());
				close(pit->second.fd);
				m_server.removeClient(cid);
				m_pendingClients.erase(pit);
				continue;
			}
			auto it = m_clients.find(cid);
			if (it != m_clients.end()) {
				printf("[Server] %s disconnected.\n", it->second.label().c_str());
				close(it->second.fd);
				m_server.removeClient(cid);
				m_clients.erase(it);
			}
		}
		m_disconnected.clear();
	}

	// Forward pending behavior reloads to controlling agent clients.
	void forwardBehaviorReloads() {
		for (auto& reload : m_server.drainPendingReloads()) {
			ClientId owner = m_server.getEntityOwner(reload.actorId);
			if (owner == 0) {
				printf("[Server] No agent controls entity %u, reload dropped\n", reload.actorId);
				continue;
			}
			auto it = m_clients.find(owner);
			if (it == m_clients.end()) continue;
			net::WriteBuffer wb;
			wb.writeU32(reload.actorId);
			wb.writeString(reload.behaviorSource);
			net::sendMessage(it->second.fd, net::S_RELOAD_BEHAVIOR, wb);
			printf("[Server] Forwarded behavior reload for entity %u to %s\n",
				reload.actorId, it->second.label().c_str());
		}
	}

	// Broadcast entity state with perception scoping + stream chunks.
	void broadcastState(float dt) {
		m_broadcastTimer += dt;
		if (m_broadcastTimer < ServerTuning::broadcastInterval || m_clients.empty()) return;
		m_broadcastTimer = 0;

		const float PERCEPTION_R2 = 64.0f * 64.0f;

		for (auto& [cid, client] : m_clients) {
			// Gather viewpoints for perception scoping
			std::vector<glm::vec3> viewPoints;
			Entity* pe = m_server.world().entities.get(client.playerId);
			if (pe) viewPoints.push_back(pe->position);
			for (auto eid : m_server.getControlledEntities(cid)) {
				auto* ce = m_server.world().entities.get(eid);
				if (ce) viewPoints.push_back(ce->position);
			}

			// Entity + time broadcast — always fires regardless of pending chunks
			m_server.world().entities.forEach([&](Entity& e) {
				bool visible = false;
				for (auto& vp : viewPoints) {
					if (glm::dot(e.position - vp, e.position - vp) <= PERCEPTION_R2) {
						visible = true; break;
					}
				}
				if (!visible) return;

				net::EntityState es;
				es.id = e.id(); es.typeId = e.typeId();
				es.position = e.position; es.velocity = e.velocity;
				es.yaw = e.yaw; es.pitch = e.pitch; es.onGround = e.onGround;
				es.goalText = e.goalText;
				es.characterSkin = e.getProp<std::string>("character_skin", "");
				es.hp = e.hp(); es.maxHp = e.def().max_hp;
				for (auto& [key, val] : e.props()) {
					if (auto* s = std::get_if<std::string>(&val))
						es.stringProps.push_back({key, *s});
				}

				net::WriteBuffer wb;
				net::serializeEntityState(wb, es);
				net::sendMessage(client.fd, net::S_ENTITY, wb);
			});

			net::WriteBuffer tb;
			tb.writeF32(m_server.worldTime());
			net::sendMessage(client.fd, net::S_TIME, tb);

			// Chunk streaming — two-tier: near (priority) then far (opportunistic).
			// Near tier covers the full view frustum so the player never sees void.
			// Far tier loads distant terrain (mountains, vistas) when near is caught up.
			// For agent clients, use the controlled entity's position as viewpoint.
			Entity* streamAnchor = pe;
			if (!streamAnchor && client.isAgent && !viewPoints.empty()) {
				// Agent client: find first controlled entity to use as chunk stream anchor
				for (auto eid : m_server.getControlledEntities(cid)) {
					streamAnchor = m_server.world().entities.get(eid);
					if (streamAnchor) break;
				}
			}
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
				float yaw_rad   = glm::radians(streamAnchor->yaw);
				float pitch_rad = glm::radians(std::clamp(streamAnchor->pitch, -89.0f, 89.0f));
				float cosPitch  = std::cos(pitch_rad);
				float fwdX = std::cos(yaw_rad) * cosPitch;
				float fwdY = -std::sin(pitch_rad);   // negative pitch = looking down
				float fwdZ = std::sin(yaw_rad) * cosPitch;

				struct Candidate { ChunkPos pos; int dist; };

				// ── Tier 1: near radius (R=STREAM_R) — must have no void at fog boundary ──
				// No hasChunk() guard — force generation of new areas eagerly.
				// dy range is dynamic so ground is always included regardless of altitude.
				if (client.pendingChunks.size() < 40) {
					std::vector<Candidate> near;
					const int R = ConnectedClient::STREAM_R;
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
						if (queued >= 6 || client.pendingChunks.size() >= 40) break;
						queueChunk(client, c.pos);
						queued++;
					}
				}

				// ── Tier 2: far radius (R=STREAM_FAR_R) — opportunistic, 1 chunk/tick ──
				// Only runs when near tier is fully satisfied (pendingChunks empty).
				// Loads distant terrain (mountains, scouting from high ground).
				// Also uses dynamic vertical range so looking down from altitude works.
				if (client.pendingChunks.empty()) {
					const int NEAR = ConnectedClient::STREAM_R;
					const int FAR  = ConnectedClient::STREAM_FAR_R;
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
			if (pe) {
				printf("[Server]   %s: pos=(%.1f,%.1f,%.1f)\n",
					c.label().c_str(), pe->position.x, pe->position.y, pe->position.z);
			}
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

		int humans = 0;
		for (auto& [cid, c] : m_clients)
			if (!c.isAgent) humans++;

		char msg[64];
		snprintf(msg, sizeof(msg), "MODCRAFT %d %d", m_port, humans);
		m_announceUdp.broadcast(msg, (int)strlen(msg), MODCRAFT_DISCOVER_PORT);
	}

	// Spawn AI client processes for uncontrolled NPC entities.
	// Called periodically from the server main loop.
	void spawnAIClients() {
		if (m_execDir.empty() || m_port <= 0) return;

		std::string agentBin = m_execDir + "/modcraft-agent";
		if (!std::filesystem::exists(agentBin)) return;

		// Reap finished AI client processes
		for (auto it = m_aiProcesses.begin(); it != m_aiProcesses.end(); ) {
			int status;
			pid_t result = waitpid(it->pid, &status, WNOHANG);
			if (result > 0) {
				drainOnePipe(*it);  // flush any final output
				if (it->logFd >= 0) { close(it->logFd); it->logFd = -1; }
				if (WIFEXITED(status)) {
					int code = WEXITSTATUS(status);
					if (code == 0)
						printf("[AI] Agent for entity %u exited normally (pid %d)\n", it->entityId, it->pid);
					else
						printf("[AI] Agent for entity %u CRASHED with exit code %d (pid %d)\n", it->entityId, it->pid, code);
				} else if (WIFSIGNALED(status)) {
					printf("[AI] Agent for entity %u KILLED by signal %d (pid %d)\n", it->entityId, it->pid, WTERMSIG(status));
				} else {
					printf("[AI] Agent for entity %u exited (pid %d, raw status=%d)\n", it->entityId, it->pid, status);
				}
				it = m_aiProcesses.erase(it);
			} else {
				++it;
			}
		}

		// Find entities that need AI clients (NPCs + player for navigation)
		auto uncontrolled = m_server.getUncontrolledNPCs();
		for (EntityId eid : uncontrolled) {
			// Skip if we already have a process for this entity
			bool hasProcess = false;
			for (auto& ai : m_aiProcesses) {
				if (ai.entityId == eid) { hasProcess = true; break; }
			}
			if (hasProcess) continue;

			// Create a pipe: child writes stdout → server reads logs
			int pipefd[2] = {-1, -1};
			pipe(pipefd);

			// Spawn AI client process
			std::string name = "ai_" + std::to_string(eid);
			std::vector<std::string> args = {
				agentBin,
				"--host", "127.0.0.1",
				"--port", std::to_string(m_port),
				"--entity", std::to_string(eid),
				"--name", name
			};

			pid_t pid = fork();
			if (pid == 0) {
				// Child: route stdout+stderr through the pipe
				if (pipefd[1] >= 0) {
					dup2(pipefd[1], STDOUT_FILENO);
					dup2(pipefd[1], STDERR_FILENO);
					close(pipefd[0]);
					close(pipefd[1]);
				}
				std::vector<char*> cargs;
				for (auto& a : args) cargs.push_back(const_cast<char*>(a.c_str()));
				cargs.push_back(nullptr);
				execv(cargs[0], cargs.data());
				_exit(127);
			} else if (pid > 0) {
				// Parent: close write end, make read end non-blocking
				if (pipefd[1] >= 0) close(pipefd[1]);
				if (pipefd[0] >= 0)
					fcntl(pipefd[0], F_SETFL, fcntl(pipefd[0], F_GETFL) | O_NONBLOCK);
				AIProcess ap;
				ap.pid = pid; ap.entityId = eid; ap.logFd = pipefd[0];
				m_aiProcesses.push_back(std::move(ap));
				printf("[AI] Spawned agent for entity %u (pid %d)\n", eid, pid);
			} else {
				if (pipefd[0] >= 0) close(pipefd[0]);
				if (pipefd[1] >= 0) close(pipefd[1]);
			}
		}
	}

	// Drain stdout from all agent pipes and print to server console.
	// Non-blocking: returns immediately if no data is available.
	void drainAgentLogs() {
		for (auto& ai : m_aiProcesses)
			drainOnePipe(ai);
	}

	// Stop all AI client processes.
	void stopAllAIClients() {
		for (auto& ai : m_aiProcesses) {
			if (ai.pid > 0 && kill(ai.pid, 0) == 0)
				kill(ai.pid, SIGTERM);
		}
		usleep(300000);
		for (auto& ai : m_aiProcesses) {
			if (ai.pid > 0) {
				int status;
				if (waitpid(ai.pid, &status, WNOHANG) == 0) {
					kill(ai.pid, SIGKILL);
					waitpid(ai.pid, &status, 0);
				}
			}
			if (ai.logFd >= 0) close(ai.logFd);
		}
		m_aiProcesses.clear();
	}

	// Close all client connections and stop AI processes.
	void disconnectAll() {
		stopAllAIClients();
		for (auto& [cid, client] : m_clients)
			close(client.fd);
		m_clients.clear();
		for (auto& [cid, client] : m_pendingClients)
			close(client.fd);
		m_pendingClients.clear();
	}

	size_t clientCount() const { return m_clients.size(); }
	size_t aiClientCount() const { return m_aiProcesses.size(); }

	// Access for callbacks (block change, entity remove, inventory)
	void broadcastToAll(net::MsgType msgType, const net::WriteBuffer& wb) {
		for (auto& [cid, c] : m_clients)
			net::sendMessage(c.fd, msgType, wb);
	}

	ConnectedClient* getClient(ClientId id) {
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

		// Update ChunkInfo owned by World
		auto div = [](int a, int b) { return (a >= 0) ? a / b : (a - b + 1) / b; };
		ChunkPos cp = {div(pos.x, CHUNK_SIZE), div(pos.y, CHUNK_SIZE), div(pos.z, CHUNK_SIZE)};

		ChunkInfo* ci = m_server.world().getChunkInfo(cp);
		if (!ci) return;

		const std::string& oldTypeId = m_server.world().blocks.get(oldBid).string_id;
		const std::string& newTypeId = m_server.world().blocks.get(newBid).string_id;
		ci->applyBlockChange(pos, oldTypeId, newTypeId);

		// Send S_CHUNK_INFO_DELTA (changed entries only) to agent clients subscribed to this chunk
		bool anyAgent = false;
		for (auto& [cid, client] : m_clients)
			if (client.isAgent && client.sentChunks.count(cp)) { anyAgent = true; break; }
		if (!anyAgent) return;

		// Build delta: send updated entries for old and new type ids
		for (auto& [cid, client] : m_clients) {
			if (!client.isAgent || !client.sentChunks.count(cp)) continue;
			// Send a minimal S_CHUNK_INFO_DELTA: cp + entries for changed types
			net::WriteBuffer db;
			db.writeI32(cp.x); db.writeI32(cp.y); db.writeI32(cp.z);
			// Collect unique type ids that changed
			std::vector<std::string> changedTypes;
			if (!oldTypeId.empty()) changedTypes.push_back(oldTypeId);
			if (!newTypeId.empty() && newTypeId != oldTypeId) changedTypes.push_back(newTypeId);
			db.writeU32((uint32_t)changedTypes.size());
			for (const auto& tid : changedTypes) {
				db.writeString(tid);
				auto eIt = ci->entries.find(tid);
				if (eIt != ci->entries.end()) {
					db.writeI32(eIt->second.count);
					db.writeU32((uint32_t)eIt->second.samples.size());
					for (auto& s : eIt->second.samples) {
						db.writeI32(s.x); db.writeI32(s.y); db.writeI32(s.z);
					}
				} else {
					db.writeI32(0);
					db.writeU32(0);
				}
			}
			net::sendMessage(client.fd, net::S_CHUNK_INFO_DELTA, db);
		}
	}

private:
	struct AIProcess {
		pid_t       pid;
		EntityId    entityId;
		int         logFd = -1;   // read end of stdout pipe from child
		std::string logBuf;       // partial-line accumulator
	};

	// Read available bytes from one agent pipe, split into lines, print each.
	void drainOnePipe(AIProcess& ai) {
		if (ai.logFd < 0) return;
		char buf[4096];
		ssize_t n;
		while ((n = read(ai.logFd, buf, sizeof(buf) - 1)) > 0) {
			buf[n] = '\0';
			ai.logBuf += buf;
		}
		size_t pos;
		while ((pos = ai.logBuf.find('\n')) != std::string::npos) {
			std::string line = ai.logBuf.substr(0, pos);
			ai.logBuf.erase(0, pos + 1);
			if (!line.empty())
				printf("%s\n", line.c_str());
		}
	}

	void handleMessage(ClientId cid, ConnectedClient& client, uint32_t type, net::ReadBuffer& rb) {
		switch (type) {
		case net::C_ACTION: {
			auto action = net::deserializeAction(rb);
			m_server.receiveAction(cid, action);
			break;
		}
		case net::C_SLOT: {
			uint32_t slot = rb.readU32();
			auto* pe = m_server.world().entities.get(client.playerId);
			if (pe) pe->setProp(Prop::SelectedSlot, (int)slot);
			break;
		}
		case net::C_HELLO: {
			// Wire format: [u32 version][str uuid][str displayName][str creatureType]
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

			// Create player entity (deferred from acceptConnections to prevent
			// ghost entities from agent connections).
			{
				EntityId eid = m_server.addClient(cid, creatureType);
				client.playerId = eid;
				net::WriteBuffer swb;
				swb.writeU32(eid);
				swb.writeVec3(m_server.spawnPos());
				net::sendMessage(client.fd, net::S_WELCOME, swb);
				Entity* pe = m_server.world().entities.get(eid);
				if (pe && pe->inventory) {
					net::WriteBuffer iwb;
					iwb.writeU32(eid);
					auto items = pe->inventory->items();
					iwb.writeU32((uint32_t)items.size());
					for (auto& [itemId, cnt] : items) { iwb.writeString(itemId); iwb.writeI32(cnt); }
					for (int i = 0; i < Inventory::HOTBAR_SLOTS; i++)
						iwb.writeString(pe->inventory->hotbar(i));
					net::sendMessage(client.fd, net::S_INVENTORY, iwb);
				}
			}

			if (!creatureType.empty()) {
				// Enforce one-character-per-server: reject if skin already occupied
				bool skinTaken = false;
				for (auto& [otherId, other] : m_clients) {
					if (otherId == client.id || other.isAgent) continue;
					Entity* oe = m_server.world().entities.get(other.playerId);
					if (oe && oe->getProp<std::string>("character_skin", "") == creatureType) {
						skinTaken = true;
						break;
					}
				}
				if (skinTaken) {
					net::WriteBuffer err;
					err.writeU32(client.playerId);
					err.writeString("Character '" + creatureType + "' is already in use.");
					net::sendMessage(client.fd, net::S_ERROR, err);
					printf("[Server] %s rejected: '%s' already online\n",
						client.label().c_str(), creatureType.c_str());
					break;
				}

				Entity* pe = m_server.world().entities.get(client.playerId);
				if (pe) {
					pe->setProp("character_skin", creatureType);

					// Restore saved inventory for this character skin
					if (pe->inventory) {
						auto& saved = m_server.savedInventories();
						auto sit = saved.find(creatureType);
						if (sit != saved.end()) {
							*pe->inventory = sit->second;
							printf("[Server] Restored inventory for '%s'\n", creatureType.c_str());
						}
						// else: keep starting items given by addClient

						// Resend inventory (with hotbar) now that skin is resolved
						net::WriteBuffer wb;
						wb.writeU32(client.playerId);
						auto items = pe->inventory->items();
						wb.writeU32((uint32_t)items.size());
						for (auto& [itemId, cnt] : items) {
							wb.writeString(itemId);
							wb.writeI32(cnt);
						}
						for (int i = 0; i < Inventory::HOTBAR_SLOTS; i++)
							wb.writeString(pe->inventory->hotbar(i));
						net::sendMessage(client.fd, net::S_INVENTORY, wb);
					}
				}
			}
			printf("[Server] %s identified: creature=%s\n",
				client.label().c_str(),
				creatureType.empty() ? "default" : creatureType.c_str());

			// Queue initial world chunks now that the human player is confirmed.
			// Sorted nearest-first so the player sees terrain under their feet
			// immediately, not far corners of the load radius first.
			{
				Entity* pe2 = m_server.world().entities.get(client.playerId);
				float yaw2 = pe2 ? pe2->yaw : 0.0f;
				float yrad2 = glm::radians(yaw2);
				float fx2 = std::cos(yrad2), fz2 = std::sin(yrad2);

				const int R = ConnectedClient::STREAM_R;
				struct IC { ChunkPos pos; int dist; };
				std::vector<IC> init;
				init.reserve((2*R+1) * (2*R+1) * 4);
				for (int dy = -1; dy <= 2; dy++)
				for (int dz = -R; dz <= R; dz++)
				for (int dx = -R; dx <= R; dx++) {
					int base = std::abs(dx) + std::abs(dz) + std::abs(dy) * 2;
					float dot = fx2 * dx + fz2 * dz;
					int biased = base - (int)(dot * 1.5f);
					init.push_back({{client.lastChunkPos.x + dx,
					                 client.lastChunkPos.y + dy,
					                 client.lastChunkPos.z + dz}, biased});
				}
				std::sort(init.begin(), init.end(),
				          [](const IC& a, const IC& b){ return a.dist < b.dist; });
				for (auto& ic : init)
					queueChunk(client, ic.pos);
			}
			printf("[Server] %s: queued %zu initial chunks (sorted nearest-first)\n",
				client.label().c_str(), client.pendingChunks.size());
			break;
		}

		case net::C_RESYNC_CHUNK: {
			ChunkPos pos = {rb.readI32(), rb.readI32(), rb.readI32()};
			// Remove from sentChunks so the chunk gets re-queued on next broadcastState
			client.sentChunks.erase(pos);
			// Cancel any in-flight stale version of this chunk
			client.pendingChunks.erase(
				std::remove_if(client.pendingChunks.begin(), client.pendingChunks.end(),
					[&](const auto& p) { return p.first == pos; }),
				client.pendingChunks.end());
			break;
		}

		case net::C_HOTBAR: {
			uint32_t slot = rb.readU32();
			std::string itemId = rb.hasMore() ? rb.readString() : "";
			Entity* pe = m_server.world().entities.get(client.playerId);
			if (pe && pe->inventory && (int)slot < Inventory::HOTBAR_SLOTS)
				pe->inventory->setHotbar((int)slot, itemId);
			break;
		}
		case net::C_AGENT_HELLO: {
			client.name = rb.readString();
			uint32_t targetEntity = rb.readU32();
			client.isAgent = true;
			m_server.addAgentClient(cid);
			printf("[Server] %s identified as agent, wants entity %u\n",
				client.label().c_str(), targetEntity);

			Entity* te = m_server.world().entities.get(targetEntity);
			if (!te || te->removed) {
				printf("[Server] Entity %u not found for agent %s\n",
					targetEntity, client.label().c_str());
				break;
			}

			client.playerId = targetEntity; // so C_SET_GOAL can find this agent by entity ID
			m_server.assignEntityToClient(cid, targetEntity);
			std::string behaviorId = te->getProp<std::string>(Prop::BehaviorId, "");

			net::WriteBuffer awb;
			awb.writeU32(targetEntity);
			awb.writeString(behaviorId);
			net::sendMessage(client.fd, net::S_ASSIGN_ENTITY, awb);
			printf("[Server] Assigned entity %u (behavior: %s) to %s\n",
				targetEntity, behaviorId.c_str(), client.label().c_str());
			break;
		}
		case net::C_SET_GOAL: {
			uint32_t eid = rb.readU32();
			float gx = rb.readF32(), gy = rb.readF32(), gz = rb.readF32();
			printf("[Server] C_SET_GOAL received: entity=%u goal=(%.1f,%.1f,%.1f) from client %u\n",
				eid, gx, gy, gz, cid);
			// Ownership check: client must own the entity or be admin
			if (!m_server.canClientControl(cid, eid)) {
				printf("[Server] C_SET_GOAL for entity %u denied (ownership)\n", eid);
				break;
			}
			// Forward to the agent controlling this entity
			bool found = false;
			for (auto& [aid, ac] : m_clients) {
				if (ac.isAgent && ac.playerId == eid) {
					net::WriteBuffer wb;
					wb.writeF32(gx); wb.writeF32(gy); wb.writeF32(gz);
					net::sendMessage(ac.fd, net::S_SET_GOAL, wb);
					printf("[Server] C_SET_GOAL(%.1f,%.1f,%.1f) forwarded to agent for entity %u\n",
						gx, gy, gz, eid);
					found = true;
					break;
				}
			}
			if (!found)
				printf("[Server] C_SET_GOAL for entity %u: no agent found\n", eid);
			break;
		}
		case net::C_CANCEL_GOAL: {
			uint32_t eid = rb.readU32();
			for (auto& [aid, ac] : m_clients) {
				if (ac.isAgent && ac.playerId == eid) {
					net::WriteBuffer wb;
					net::sendMessage(ac.fd, net::S_CANCEL_GOAL, wb);
					break;
				}
			}
			break;
		}
		case net::C_CLAIM_ENTITY: {
			uint32_t eid = rb.readU32();
			Entity* target = m_server.world().entities.get(eid);
			Entity* player = m_server.world().entities.get(client.playerId);
			if (!target || !player) break;
			int currentOwner = target->getProp<int>(Prop::Owner, 0);
			bool isAdmin = player->getProp<bool>("fly_mode", false);
			// Can claim if: unclaimed (owner=0) or admin
			if (currentOwner == 0 || isAdmin) {
				target->setProp(Prop::Owner, (int)client.playerId);
				printf("[Server] Entity %u claimed by player %u\n", eid, client.playerId);
			}
			break;
		}
		default: break;
		}
	}

	// Remove sentChunks that are now too far from `center` and tell the client to evict them.
	// `groundChunkY` is the chunk Y of the terrain surface at the player's XZ; used so
	// chunks between the player and the ground are never evicted while flying.
	void evictFarChunks(ConnectedClient& cc, ChunkPos center, int groundChunkY) {
		// Keep everything from groundChunkY-1 up to player+3 vertically
		int dyDown = center.y - std::max(groundChunkY - 1, center.y - ConnectedClient::EVICT_DY);
		std::vector<ChunkPos> toEvict;
		for (const auto& pos : cc.sentChunks) {
			bool horizFar = std::abs(pos.x - center.x) > ConnectedClient::EVICT_R ||
			                std::abs(pos.z - center.z) > ConnectedClient::EVICT_R;
			bool vertFar  = pos.y < center.y - dyDown || pos.y > center.y + 3;
			if (horizFar || vertFar) toEvict.push_back(pos);
		}
		if (toEvict.empty()) return;

		for (const auto& pos : toEvict) {
			cc.sentChunks.erase(pos);
			// ChunkInfo is now owned by World — no eviction needed here
			net::WriteBuffer wb;
			wb.writeI32(pos.x); wb.writeI32(pos.y); wb.writeI32(pos.z);
			net::sendMessage(cc.fd, net::S_CHUNK_EVICT, wb);
		}
		// Cancel any queued (but not yet sent) messages for evicted chunks
		cc.pendingChunks.erase(
			std::remove_if(cc.pendingChunks.begin(), cc.pendingChunks.end(),
				[&](const auto& p) {
					return std::find(toEvict.begin(), toEvict.end(), p.first) != toEvict.end();
				}),
			cc.pendingChunks.end());
	}

	// Force all clients to discard and re-fetch a chunk (e.g. after a bulk terrain change).
	void invalidateChunkForAll(ChunkPos pos) {
		for (auto& [cid, client] : m_clients) {
			if (!client.sentChunks.count(pos)) continue;
			client.sentChunks.erase(pos);
			client.pendingChunks.erase(
				std::remove_if(client.pendingChunks.begin(), client.pendingChunks.end(),
					[&](const auto& p) { return p.first == pos; }),
				client.pendingChunks.end());
			net::WriteBuffer wb;
			wb.writeI32(pos.x); wb.writeI32(pos.y); wb.writeI32(pos.z);
			net::sendMessage(client.fd, net::S_CHUNK_EVICT, wb);
		}
	}

	void queueChunk(ConnectedClient& cc, ChunkPos pos) {
		if (cc.sentChunks.count(pos)) return;
		Chunk* chunk = m_server.world().getChunk(pos);
		if (!chunk) return;

		// Build uncompressed payload: [i32 cx][i32 cy][i32 cz][u32×4096]
		net::WriteBuffer cb;
		cb.writeI32(pos.x); cb.writeI32(pos.y); cb.writeI32(pos.z);
		for (int i = 0; i < CHUNK_VOLUME; i++)
			cb.writeU32(((uint32_t)chunk->getRawParam2(i) << 16) | chunk->getRaw(i));

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
		cc.pendingChunks.push_back({pos, std::move(msg)});
		cc.sentChunks.insert(pos);

		// Agent clients receive S_CHUNK_INFO so behaviors can query block types.
		// ChunkInfo was built during generateChunk() — just send it here.
		if (cc.isAgent) {
			ChunkInfo* ci = m_server.world().getChunkInfo(pos);
			if (ci) {
				net::WriteBuffer cib;
				cib.writeI32(pos.x); cib.writeI32(pos.y); cib.writeI32(pos.z);
				// Count non-empty entries
				uint32_t entryCount = (uint32_t)ci->entries.size();
				cib.writeU32(entryCount);
				for (auto& [typeId, entry] : ci->entries) {
					cib.writeString(typeId);
					cib.writeI32(entry.count);
					cib.writeU32((uint32_t)entry.samples.size());
					for (auto& s : entry.samples) {
						cib.writeI32(s.x); cib.writeI32(s.y); cib.writeI32(s.z);
					}
				}
				net::sendMessage(cc.fd, net::S_CHUNK_INFO, cib);
			}
		}
	}

	GameServer& m_server;
	std::unordered_map<ClientId, ConnectedClient> m_clients;         // identified
	std::unordered_map<ClientId, ConnectedClient> m_pendingClients;  // awaiting hello
	std::vector<ClientId> m_disconnected;
	ClientId m_nextClientId = 1;
	float m_broadcastTimer = 0;

	// AI client process management
	std::string m_execDir;
	int m_port = 0;
	std::vector<AIProcess> m_aiProcesses;

	// LAN discovery
	net::UdpSocket m_announceUdp;
	float m_announceTimer = 0.0f;
};

} // namespace modcraft

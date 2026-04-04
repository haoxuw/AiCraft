#pragma once

/**
 * ClientManager — manages TCP client connections for the dedicated server.
 *
 * Extracted from main_server.cpp to keep the main loop clean.
 * Handles: accept, receive, broadcast, chunk streaming, disconnect.
 */

#include "server/server.h"
#include "shared/net_socket.h"
#include "shared/net_protocol.h"
#include "shared/constants.h"
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

namespace agentica {

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
	bool isBot = false;

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

	// Set the directory containing agentica-bot binary.
	// Call before the main loop. If empty, AI client spawning is disabled.
	void setExecDir(const std::string& dir) { m_execDir = dir; }

	// Set the server port (needed to tell AI clients where to connect).
	void setPort(int port) { m_port = port; }

	// Accept new TCP connections and register as clients.
	void acceptConnections(net::TcpServer& listener) {
		auto accepted = listener.acceptClient();
		if (accepted.fd < 0) return;

		ClientId cid = m_nextClientId++;
		EntityId eid = m_server.addClient(cid);

		// Send S_WELCOME
		{
			net::WriteBuffer wb;
			wb.writeU32(eid);
			wb.writeVec3(m_server.spawnPos());
			net::sendMessage(accepted.fd, net::S_WELCOME, wb);
		}

		// Send initial inventory
		{
			Entity* pe = m_server.world().entities.get(eid);
			if (pe && pe->inventory) {
				net::WriteBuffer wb;
				wb.writeU32(eid);
				auto items = pe->inventory->items();
				wb.writeU32((uint32_t)items.size());
				for (auto& [itemId, count] : items) {
					wb.writeString(itemId);
					wb.writeI32(count);
				}
				net::sendMessage(accepted.fd, net::S_INVENTORY, wb);
			}
		}

		// Queue initial chunks around spawn
		ConnectedClient cc;
		cc.fd = accepted.fd; cc.id = cid; cc.playerId = eid;
		cc.ip = accepted.ip; cc.port = accepted.port;

		auto sp = m_server.spawnPos();
		auto cp = worldToChunk((int)sp.x, (int)sp.y, (int)sp.z);
		cc.lastChunkPos = cp;

		for (int dy = 0; dy <= 1; dy++)
		for (int dz = -4; dz <= 4; dz++)
		for (int dx = -4; dx <= 4; dx++)
			queueChunk(cc, {cp.x + dx, cp.y + dy, cp.z + dz});

		m_clients[cid] = std::move(cc);
		printf("[Server] %s joined (entity %u). Sending %zu chunks...\n",
		       m_clients[cid].label().c_str(), eid, m_clients[cid].pendingChunks.size());
	}

	// Send queued chunks to clients (non-blocking, batched).
	void sendPendingChunks() {
		for (auto& [cid, client] : m_clients) {
			int sent = 0;
			while (!client.pendingChunks.empty() && sent < 10) {
				auto& msg = client.pendingChunks.front().second;
				ssize_t n = send(client.fd, msg.data(), msg.size(), MSG_NOSIGNAL);
				if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
				if (n <= 0) break;
				client.pendingChunks.erase(client.pendingChunks.begin());
				sent++;
			}
		}
	}

	// Receive and dispatch all pending messages from clients.
	void receiveMessages() {
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
			printf("[Server] %s disconnected.\n", m_clients[cid].label().c_str());
			close(m_clients[cid].fd);
			m_server.removeClient(cid);
			m_clients.erase(cid);
		}
		m_disconnected.clear();
	}

	// Forward pending behavior reloads to controlling bot clients.
	void forwardBehaviorReloads() {
		for (auto& reload : m_server.drainPendingReloads()) {
			ClientId owner = m_server.getEntityOwner(reload.actorId);
			if (owner == 0) {
				printf("[Server] No bot controls entity %u, reload dropped\n", reload.actorId);
				continue;
			}
			auto it = m_clients.find(owner);
			if (it == m_clients.end()) continue;
			net::WriteBuffer wb;
			wb.writeU32(reload.actorId);
			wb.writeString(reload.blockType);
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
			if (!client.pendingChunks.empty()) continue;

			// Gather viewpoints for perception scoping
			std::vector<glm::vec3> viewPoints;
			Entity* pe = m_server.world().entities.get(client.playerId);
			if (pe) viewPoints.push_back(pe->position);
			for (auto eid : m_server.getControlledEntities(cid)) {
				auto* ce = m_server.world().entities.get(eid);
				if (ce) viewPoints.push_back(ce->position);
			}

			// Send visible entities
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
				es.yaw = e.yaw; es.onGround = e.onGround;
				es.goalText = e.goalText;
				es.characterSkin = e.getProp<std::string>("character_skin", "");
				es.hp = e.hp(); es.maxHp = e.def().max_hp;

				net::WriteBuffer wb;
				net::serializeEntityState(wb, es);
				net::sendMessage(client.fd, net::S_ENTITY, wb);
			});

			// Stream chunks around player
			if (pe && client.pendingChunks.size() < 20) {
				auto cp = worldToChunk((int)pe->position.x, (int)pe->position.y, (int)pe->position.z);
				const int R = 6;
				int queued = 0;
				for (int dy = -1; dy <= 2 && queued < 4; dy++)
				for (int dz = -R; dz <= R && queued < 4; dz++)
				for (int dx = -R; dx <= R && queued < 4; dx++) {
					ChunkPos pos = {cp.x + dx, cp.y + dy, cp.z + dz};
					if (client.sentChunks.count(pos)) continue;
					queueChunk(client, pos);
					if (client.pendingChunks.size() > 0 &&
					    client.pendingChunks.back().first == pos) queued++;
				}
			}

			// Send world time
			net::WriteBuffer tb;
			tb.writeF32(m_server.worldTime());
			net::sendMessage(client.fd, net::S_TIME, tb);
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

	// Spawn AI client processes for uncontrolled NPC entities.
	// Called periodically from the server main loop.
	void spawnAIClients() {
		if (m_execDir.empty() || m_port <= 0) return;

		std::string botBin = m_execDir + "/agentica-bot";
		if (!std::filesystem::exists(botBin)) return;

		// Reap finished AI client processes
		for (auto it = m_aiProcesses.begin(); it != m_aiProcesses.end(); ) {
			int status;
			pid_t result = waitpid(it->pid, &status, WNOHANG);
			if (result > 0) {
				// Process exited — entity is now uncontrolled again
				printf("[AI] AI client for entity %u exited (pid %d)\n", it->entityId, it->pid);
				it = m_aiProcesses.erase(it);
			} else {
				++it;
			}
		}

		// Find NPC entities that need AI clients
		auto uncontrolled = m_server.getUncontrolledNPCs();
		for (EntityId eid : uncontrolled) {
			// Skip if we already have a process for this entity
			bool hasProcess = false;
			for (auto& ai : m_aiProcesses) {
				if (ai.entityId == eid) { hasProcess = true; break; }
			}
			if (hasProcess) continue;

			// Spawn AI client process
			std::string name = "ai_" + std::to_string(eid);
			std::vector<std::string> args = {
				botBin,
				"--host", "127.0.0.1",
				"--port", std::to_string(m_port),
				"--entity", std::to_string(eid),
				"--name", name
			};

			pid_t pid = fork();
			if (pid == 0) {
				// Child: redirect output to /dev/null, exec bot
				std::vector<char*> cargs;
				for (auto& a : args) cargs.push_back(const_cast<char*>(a.c_str()));
				cargs.push_back(nullptr);
				int devnull = open("/dev/null", O_WRONLY);
				if (devnull >= 0) {
					dup2(devnull, STDOUT_FILENO);
					dup2(devnull, STDERR_FILENO);
					close(devnull);
				}
				execv(cargs[0], cargs.data());
				_exit(127);
			} else if (pid > 0) {
				m_aiProcesses.push_back({pid, eid});
				printf("[AI] Spawned AI client for entity %u (pid %d)\n", eid, pid);
			}
		}
	}

	// Stop all AI client processes.
	void stopAllAIClients() {
		for (auto& ai : m_aiProcesses) {
			if (ai.pid > 0 && kill(ai.pid, 0) == 0) {
				kill(ai.pid, SIGTERM);
			}
		}
		// Wait for them to exit
		usleep(300000);
		for (auto& ai : m_aiProcesses) {
			if (ai.pid > 0) {
				int status;
				if (waitpid(ai.pid, &status, WNOHANG) == 0) {
					kill(ai.pid, SIGKILL);
					waitpid(ai.pid, &status, 0);
				}
			}
		}
		m_aiProcesses.clear();
	}

	// Close all client connections and stop AI processes.
	void disconnectAll() {
		stopAllAIClients();
		for (auto& [cid, client] : m_clients)
			close(client.fd);
		m_clients.clear();
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

private:
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
			client.name = rb.readString();
			std::string displayName = rb.hasMore() ? rb.readString() : "";
			std::string creatureType = rb.hasMore() ? rb.readString() : "";
			if (!displayName.empty())
				client.name = displayName + " (" + client.name.substr(0, 8) + ")";
			// Apply selected character skin so the server broadcasts the right model
			if (!creatureType.empty()) {
				Entity* pe = m_server.world().entities.get(client.playerId);
				if (pe) pe->setProp("character_skin", creatureType);
			}
			printf("[Server] %s identified: creature=%s\n",
				client.label().c_str(),
				creatureType.empty() ? "default" : creatureType.c_str());
			break;
		}
		case net::C_BOT_HELLO: {
			client.name = rb.readString();
			uint32_t targetEntity = rb.readU32();
			client.isBot = true;
			printf("[Server] %s identified as bot, wants entity %u\n",
				client.label().c_str(), targetEntity);

			Entity* te = m_server.world().entities.get(targetEntity);
			if (!te || te->removed) {
				printf("[Server] Entity %u not found for bot %s (keeping player entity)\n",
					targetEntity, client.label().c_str());
				break;
			}

			if (client.playerId != ENTITY_NONE) {
				m_server.world().entities.remove(client.playerId);
				m_server.removeClient(cid);
				m_server.addBotClient(cid);
				client.playerId = ENTITY_NONE;
			}

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
		default: break;
		}
	}

	void queueChunk(ConnectedClient& cc, ChunkPos pos) {
		if (cc.sentChunks.count(pos)) return;
		Chunk* chunk = m_server.world().getChunk(pos);
		if (!chunk) return;

		net::WriteBuffer cb;
		cb.writeI32(pos.x); cb.writeI32(pos.y); cb.writeI32(pos.z);
		for (int i = 0; i < 16*16*16; i++)
			cb.writeU32(chunk->getRaw(i));

		std::vector<uint8_t> msg;
		net::MsgHeader hdr;
		hdr.type = net::S_CHUNK;
		hdr.length = (uint32_t)cb.size();
		msg.resize(8 + cb.size());
		memcpy(msg.data(), &hdr, 8);
		memcpy(msg.data() + 8, cb.data().data(), cb.size());
		cc.pendingChunks.push_back({pos, std::move(msg)});
		cc.sentChunks.insert(pos);
	}

	GameServer& m_server;
	std::unordered_map<ClientId, ConnectedClient> m_clients;
	std::vector<ClientId> m_disconnected;
	ClientId m_nextClientId = 1;
	float m_broadcastTimer = 0;

	// AI client process management
	std::string m_execDir;
	int m_port = 0;
	struct AIProcess { pid_t pid; EntityId entityId; };
	std::vector<AIProcess> m_aiProcesses;
};

} // namespace agentica

/**
 * Dedicated server — runs headless, accepts TCP client connections.
 *
 * Usage: ./agentworld-server [--port PORT] [--seed SEED] [--survival]
 */

#include "server/server.h"
#include "server/world_template.h"
#include "content/builtin.h"
#include "shared/net_socket.h"
#include "shared/net_protocol.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
#include <csignal>
#include <unordered_map>
#include <fcntl.h>

#include "server/python_bridge.h"

static volatile bool g_running = true;
static void signalHandler(int) { g_running = false; }

struct ConnectedClient {
	int fd;
	agentworld::ClientId id;
	agentworld::EntityId playerId;
	agentworld::net::RecvBuffer recvBuf;
};

int main(int argc, char** argv) {
	printf("=== AgentWorld Dedicated Server ===\n");
	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);

	agentworld::pythonBridge().init("python");

	// Parse args
	agentworld::ServerConfig config;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
			config.port = atoi(argv[++i]);
		else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
			config.seed = atoi(argv[++i]);
		else if (strcmp(argv[i], "--survival") == 0)
			config.creative = false;
	}

	// World templates
	std::vector<std::shared_ptr<agentworld::WorldTemplate>> templates = {
		std::make_shared<agentworld::FlatWorldTemplate>(),
		std::make_shared<agentworld::VillageWorldTemplate>(),
	};

	// Initialize server
	agentworld::GameServer server;
	server.init(config, templates);

	// Start TCP listener
	agentworld::net::TcpServer listener;
	if (!listener.listen(config.port)) {
		printf("[Server] Failed to start listener.\n");
		return 1;
	}

	printf("[Server] Waiting for clients on port %d...\n", config.port);

	std::unordered_map<agentworld::ClientId, ConnectedClient> clients;
	agentworld::ClientId nextClientId = 1;

	// Fixed timestep server loop (60 tps)
	const float TICK_RATE = 1.0f / 60.0f;
	auto lastTime = std::chrono::steady_clock::now();
	float accumulator = 0;
	int tickCount = 0;
	float statusTimer = 0;

	while (g_running) {
		auto now = std::chrono::steady_clock::now();
		float dt = std::chrono::duration<float>(now - lastTime).count();
		lastTime = now;
		accumulator += dt;
		statusTimer += dt;

		// Accept new clients
		int newFd = listener.acceptClient();
		if (newFd >= 0) {
			// Temporarily set blocking for initial data send (chunks are large)
			int flags = fcntl(newFd, F_GETFL, 0);
			fcntl(newFd, F_SETFL, flags & ~O_NONBLOCK);
			agentworld::ClientId cid = nextClientId++;
			agentworld::EntityId eid = server.addClient(cid);

			clients[cid] = {newFd, cid, eid, {}};

			// Send welcome message with player entity ID and spawn pos
			agentworld::net::WriteBuffer wb;
			wb.writeU32(eid);
			wb.writeVec3(server.spawnPos());
			agentworld::net::sendMessage(newFd, agentworld::net::S_WELCOME, wb);

			// Send surface chunks around spawn (9×9 horizontal, 2 Y levels)
			// Includes forest area outside village clearing
			auto sp = server.spawnPos();
			auto cp = agentworld::worldToChunk((int)sp.x, (int)sp.y, (int)sp.z);
			for (int dy = 0; dy <= 1; dy++)
			for (int dz = -4; dz <= 4; dz++)
			for (int dx = -4; dx <= 4; dx++) {
				agentworld::ChunkPos pos = {cp.x + dx, cp.y, cp.z + dz};
				agentworld::Chunk* chunk = server.world().getChunk(pos);
				if (chunk) {
					agentworld::net::WriteBuffer cb;
					cb.writeI32(pos.x); cb.writeI32(pos.y); cb.writeI32(pos.z);
					// Send raw block data
					for (int i = 0; i < 16*16*16; i++)
						cb.writeU32(chunk->getRaw(i));
					agentworld::net::sendMessage(newFd, agentworld::net::S_CHUNK, cb);
				}
			}

			// Now set non-blocking for regular updates
			agentworld::net::setNonBlocking(newFd);
		}

		// Receive from clients
		std::vector<agentworld::ClientId> disconnected;
		for (auto& [cid, client] : clients) {
			if (!client.recvBuf.readFrom(client.fd)) {
				disconnected.push_back(cid);
				continue;
			}

			// Process received messages
			agentworld::net::MsgHeader hdr;
			std::vector<uint8_t> payload;
			while (client.recvBuf.tryExtract(hdr, payload)) {
				agentworld::net::ReadBuffer rb(payload.data(), payload.size());

				switch (hdr.type) {
				case agentworld::net::C_ACTION: {
					auto action = agentworld::net::deserializeAction(rb);
					server.receiveAction(cid, action);
					break;
				}
				case agentworld::net::C_SLOT: {
					uint32_t slot = rb.readU32();
					auto* pe = server.world().entities.get(client.playerId);
					if (pe) pe->setProp(agentworld::Prop::SelectedSlot, (int)slot);
					break;
				}
				default: break;
				}
			}
		}

		// Remove disconnected clients
		for (auto cid : disconnected) {
			printf("[Server] Client %u disconnected.\n", cid);
			close(clients[cid].fd);
			server.removeClient(cid);
			clients.erase(cid);
		}

		// Server tick (fixed timestep)
		while (accumulator >= TICK_RATE) {
			server.tick(TICK_RATE);
			accumulator -= TICK_RATE;
			tickCount++;
		}

		// Broadcast entity state (throttled: 20 Hz, not every tick)
		static float broadcastTimer = 0;
		broadcastTimer += dt;
		if (broadcastTimer >= 0.05f && !clients.empty()) {
			broadcastTimer = 0;
		for (auto& [cid, client] : clients) {
			server.world().entities.forEach([&](agentworld::Entity& e) {
				agentworld::net::EntityState es;
				es.id = e.id();
				es.typeId = e.typeId();
				es.position = e.position;
				es.velocity = e.velocity;
				es.yaw = e.yaw;
				es.onGround = e.onGround;
				es.goalText = e.goalText;
				es.hp = e.hp();
				es.maxHp = e.def().max_hp;

				agentworld::net::WriteBuffer wb;
				agentworld::net::serializeEntityState(wb, es);
				agentworld::net::sendMessage(client.fd, agentworld::net::S_ENTITY, wb);
			});

			// Send world time
			agentworld::net::WriteBuffer tb;
			tb.writeF32(server.worldTime());
			agentworld::net::sendMessage(client.fd, agentworld::net::S_TIME, tb);
		}
		} // end broadcast throttle

		// Status logging
		if (statusTimer >= 5.0f) {
			printf("[Server] %d ticks, %.1f tps, %zu entities, %zu clients\n",
			       tickCount, tickCount / statusTimer,
			       server.world().entities.count(),
			       clients.size());
			tickCount = 0;
			statusTimer = 0;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	// Cleanup
	for (auto& [cid, client] : clients)
		close(client.fd);
	listener.shutdown();

	printf("[Server] Shut down.\n");

	agentworld::pythonBridge().shutdown();
	return 0;
}

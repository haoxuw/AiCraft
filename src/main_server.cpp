/**
 * Dedicated server — runs headless, accepts TCP client connections.
 *
 * Usage: ./aicraft-server [--port PORT] [--seed SEED] [--survival]
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

#include "server/python_bridge.h"

static volatile bool g_running = true;
static void signalHandler(int) { g_running = false; }

struct ConnectedClient {
	int fd;
	aicraft::ClientId id;
	aicraft::EntityId playerId;
	aicraft::net::RecvBuffer recvBuf;
};

int main(int argc, char** argv) {
	printf("=== AiCraft Dedicated Server ===\n");
	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);

	aicraft::pythonBridge().init("python");

	// Parse args
	aicraft::ServerConfig config;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
			config.port = atoi(argv[++i]);
		else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
			config.seed = atoi(argv[++i]);
		else if (strcmp(argv[i], "--survival") == 0)
			config.creative = false;
	}

	// World templates
	std::vector<std::shared_ptr<aicraft::WorldTemplate>> templates = {
		std::make_shared<aicraft::FlatWorldTemplate>(),
		std::make_shared<aicraft::VillageWorldTemplate>(),
	};

	// Initialize server
	aicraft::GameServer server;
	server.init(config, templates);

	// Start TCP listener
	aicraft::net::TcpServer listener;
	if (!listener.listen(config.port)) {
		printf("[Server] Failed to start listener.\n");
		return 1;
	}

	printf("[Server] Waiting for clients on port %d...\n", config.port);

	std::unordered_map<aicraft::ClientId, ConnectedClient> clients;
	aicraft::ClientId nextClientId = 1;

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
			aicraft::ClientId cid = nextClientId++;
			aicraft::EntityId eid = server.addClient(cid);

			clients[cid] = {newFd, cid, eid, {}};

			// Send welcome message with player entity ID and spawn pos
			aicraft::net::WriteBuffer wb;
			wb.writeU32(eid);
			wb.writeVec3(server.spawnPos());
			aicraft::net::sendMessage(newFd, aicraft::net::S_WELCOME, wb);

			// Send initial chunk data around spawn
			auto sp = server.spawnPos();
			auto cp = aicraft::World::worldToChunk((int)sp.x, (int)sp.y, (int)sp.z);
			for (int dy = -2; dy <= 2; dy++)
			for (int dz = -4; dz <= 4; dz++)
			for (int dx = -4; dx <= 4; dx++) {
				aicraft::ChunkPos pos = {cp.x + dx, cp.y + dy, cp.z + dz};
				aicraft::Chunk* chunk = server.world().getChunk(pos);
				if (chunk) {
					aicraft::net::WriteBuffer cb;
					cb.writeI32(pos.x); cb.writeI32(pos.y); cb.writeI32(pos.z);
					// Send raw block data
					for (int i = 0; i < 16*16*16; i++)
						cb.writeU32(chunk->getRaw(i));
					aicraft::net::sendMessage(newFd, aicraft::net::S_CHUNK, cb);
				}
			}
		}

		// Receive from clients
		std::vector<aicraft::ClientId> disconnected;
		for (auto& [cid, client] : clients) {
			if (!client.recvBuf.readFrom(client.fd)) {
				disconnected.push_back(cid);
				continue;
			}

			// Process received messages
			aicraft::net::MsgHeader hdr;
			std::vector<uint8_t> payload;
			while (client.recvBuf.tryExtract(hdr, payload)) {
				aicraft::net::ReadBuffer rb(payload.data(), payload.size());

				switch (hdr.type) {
				case aicraft::net::C_ACTION: {
					auto action = aicraft::net::deserializeAction(rb);
					server.receiveAction(cid, action);
					break;
				}
				case aicraft::net::C_SLOT: {
					uint32_t slot = rb.readU32();
					auto* pe = server.world().entities.get(client.playerId);
					if (pe) pe->setProp(aicraft::Prop::SelectedSlot, (int)slot);
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
			server.world().entities.forEach([&](aicraft::Entity& e) {
				aicraft::net::EntityState es;
				es.id = e.id();
				es.typeId = e.typeId();
				es.position = e.position;
				es.velocity = e.velocity;
				es.yaw = e.yaw;
				es.onGround = e.onGround;
				es.goalText = e.goalText;
				es.hp = e.hp();
				es.maxHp = e.def().max_hp;

				aicraft::net::WriteBuffer wb;
				aicraft::net::serializeEntityState(wb, es);
				aicraft::net::sendMessage(client.fd, aicraft::net::S_ENTITY, wb);
			});

			// Send world time
			aicraft::net::WriteBuffer tb;
			tb.writeF32(server.worldTime());
			aicraft::net::sendMessage(client.fd, aicraft::net::S_TIME, tb);
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

	aicraft::pythonBridge().shutdown();
	return 0;
}

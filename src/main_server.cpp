/**
 * Dedicated server — runs headless, accepts TCP client connections.
 *
 * Usage:
 *   ./agentworld-server                              # interactive world selection
 *   ./agentworld-server --world saves/my_village      # load saved world
 *   ./agentworld-server --port 7778 --template 1      # new world on custom port
 */

#include "server/server.h"
#include "server/world_template.h"
#include "server/world_save.h"
#include "game/world_manager.h"
#include "content/builtin.h"
#include "shared/net_socket.h"
#include "shared/net_protocol.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
#include <csignal>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <fcntl.h>
#include <iostream>
#include <string>

#include "server/python_bridge.h"

static volatile bool g_running = true;
static void signalHandler(int) { g_running = false; }

struct ConnectedClient {
	int fd;
	agentworld::ClientId id;
	agentworld::EntityId playerId;
	agentworld::net::RecvBuffer recvBuf;
	// Pending chunk sends (non-blocking, sent over multiple ticks)
	std::vector<std::pair<agentworld::ChunkPos, std::vector<uint8_t>>> pendingChunks;
	// Track which chunks this client has, to send new ones as they move
	agentworld::ChunkPos lastChunkPos = {0, 0, 0};
	std::unordered_set<agentworld::ChunkPos, agentworld::ChunkPosHash> sentChunks;
	// Connection info for logging
	std::string ip;
	int port = 0;
	std::string name;  // from C_HELLO (UUID or display name)

	// Formatted label for log messages: "Client 1 (name@ip:port)" or "Client 1 (ip:port)"
	std::string label() const {
		if (!name.empty())
			return "Client " + std::to_string(id) + " (" + name + "@" + ip + ":" + std::to_string(port) + ")";
		return "Client " + std::to_string(id) + " (" + ip + ":" + std::to_string(port) + ")";
	}
};

// Interactive CLI: let user pick a world or create new
static void interactiveWorldSelect(agentworld::ServerConfig& config,
                                    std::string& worldPath,
                                    const std::vector<std::shared_ptr<agentworld::WorldTemplate>>& templates) {
	agentworld::WorldManager mgr;
	mgr.setSavesDir("saves");
	mgr.refresh();

	auto& worlds = mgr.worlds();

	printf("\n");
	printf("  ┌─────────────────────────────────┐\n");
	printf("  │       AGENTWORLD SERVER          │\n");
	printf("  └─────────────────────────────────┘\n\n");

	if (!worlds.empty()) {
		printf("  Saved worlds:\n");
		for (size_t i = 0; i < worlds.size(); i++) {
			printf("    [%zu] %s (%s, seed=%d)\n",
			       i + 1, worlds[i].name.c_str(), worlds[i].templateName.c_str(), worlds[i].seed);
		}
		printf("\n");
	}

	printf("  New world templates:\n");
	for (size_t i = 0; i < templates.size(); i++) {
		printf("    [%c] %s — %s\n",
		       (char)('a' + i), templates[i]->name().c_str(), templates[i]->description().c_str());
	}

	printf("\n  Enter choice (number for saved, letter for new, or 'q' to quit): ");
	fflush(stdout);

	std::string input;
	if (!std::getline(std::cin, input) || input.empty() || input == "q") {
		printf("Cancelled.\n");
		exit(0);
	}

	// Check if it's a number (saved world)
	if (input[0] >= '1' && input[0] <= '9') {
		int idx = atoi(input.c_str()) - 1;
		if (idx >= 0 && idx < (int)worlds.size()) {
			worldPath = worlds[idx].path;
			config.seed = worlds[idx].seed;
			config.templateIndex = worlds[idx].templateIndex;
			printf("  Loading: %s\n\n", worlds[idx].name.c_str());
			return;
		}
	}

	// Check if it's a letter (new template)
	if (input[0] >= 'a' && input[0] < (char)('a' + templates.size())) {
		int tmplIdx = input[0] - 'a';
		config.templateIndex = tmplIdx;

		printf("  World name: ");
		fflush(stdout);
		std::string name;
		std::getline(std::cin, name);
		if (name.empty()) name = "New World";

		printf("  Seed (blank = random): ");
		fflush(stdout);
		std::string seedStr;
		std::getline(std::cin, seedStr);
		if (!seedStr.empty()) {
			config.seed = atoi(seedStr.c_str());
		} else {
			srand(time(nullptr));
			config.seed = rand();
		}

		// Create save directory
		worldPath = mgr.createWorld(name, config.seed, tmplIdx, templates[tmplIdx]->name());
		printf("  Created: %s (seed=%d)\n\n", name.c_str(), config.seed);
		return;
	}

	printf("  Invalid choice. Starting default world.\n\n");
}

int main(int argc, char** argv) {
	setvbuf(stdout, nullptr, _IONBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);
	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);

	// Server log file: /tmp/agentica_log_{port}.log
	// Console output is NOT redirected — log file gets periodic snapshots.
	int logPort = 7777;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
			logPort = atoi(argv[i + 1]);
	}
	char logPath[256];
	snprintf(logPath, sizeof(logPath), "/tmp/agentica_log_%d.log", logPort);
	FILE* g_logFile = fopen(logPath, "w");
	if (g_logFile) {
		setvbuf(g_logFile, nullptr, _IONBF, 0);
		printf("[Server] Also logging to %s\n", logPath);
	}

	printf("=== AgentWorld Dedicated Server ===\n");

	agentworld::pythonBridge().init("python");

	// Parse args
	agentworld::ServerConfig config;
	std::string worldPath;
	bool interactive = true;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("AgentWorld — dedicated server\n\n"
			       "Usage: %s [options]\n"
			       "  --port PORT       Listen port (default 7777)\n"
			       "  --world PATH      Load saved world from PATH\n"
			       "  --seed N          World seed (default 42)\n"
			       "  --template N      World template: 0=flat, 1=village (default 1)\n"
			       "  --survival        Survival mode (default)\n"
			       "  --help, -h        Show this help\n", argv[0]);
			return 0;
		}
		else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
			config.port = atoi(argv[++i]);
		else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
			config.seed = atoi(argv[++i]); interactive = false;
		} else if (strcmp(argv[i], "--template") == 0 && i + 1 < argc) {
			config.templateIndex = atoi(argv[++i]); interactive = false;
		} else if (strcmp(argv[i], "--world") == 0 && i + 1 < argc) {
			worldPath = argv[++i]; interactive = false;
		} else if (strcmp(argv[i], "--survival") == 0)
			config.creative = false;
	}

	// World templates
	std::vector<std::shared_ptr<agentworld::WorldTemplate>> templates = {
		std::make_shared<agentworld::FlatWorldTemplate>(),
		std::make_shared<agentworld::VillageWorldTemplate>(),
	};

	// Interactive world selection if no --world/--seed/--template provided
	if (interactive && isatty(fileno(stdin))) {
		interactiveWorldSelect(config, worldPath, templates);
	}

	// Initialize server
	agentworld::GameServer server;
	if (!worldPath.empty() && std::filesystem::exists(worldPath + "/world.json")) {
		// Load existing world
		printf("[Server] Loading world from %s\n", worldPath.c_str());
		if (!agentworld::loadWorld(server, worldPath, templates)) {
			printf("[Server] Failed to load world, creating new\n");
			server.init(config, templates);
		}
	} else {
		server.init(config, templates);
	}

	// Start TCP listener
	agentworld::net::TcpServer listener;
	if (!listener.listen(config.port)) {
		printf("[Server] Failed to start listener on port %d\n", config.port);
		return 1;
	}

	printf("[Server] Listening on port %d (seed=%d, template=%d)\n",
	       config.port, config.seed, config.templateIndex);
	printf("[Server] Press Ctrl+C to save and stop.\n");

	std::unordered_map<agentworld::ClientId, ConnectedClient> clients;
	agentworld::ClientId nextClientId = 1;

	// Network broadcast callbacks — send state changes to all connected clients
	agentworld::ServerCallbacks cbs;
	cbs.onBlockChange = [&](glm::ivec3 pos, agentworld::BlockId bid) {
		agentworld::net::WriteBuffer wb;
		wb.writeI32(pos.x); wb.writeI32(pos.y); wb.writeI32(pos.z);
		wb.writeU32(bid);
		for (auto& [cid, c] : clients)
			agentworld::net::sendMessage(c.fd, agentworld::net::S_BLOCK, wb);
	};
	cbs.onEntityRemove = [&](agentworld::EntityId id) {
		agentworld::net::WriteBuffer wb;
		wb.writeU32(id);
		for (auto& [cid, c] : clients)
			agentworld::net::sendMessage(c.fd, agentworld::net::S_REMOVE, wb);
	};
	cbs.onInventoryChange = [&](agentworld::EntityId id, const agentworld::Inventory& inv) {
		agentworld::net::WriteBuffer wb;
		wb.writeU32(id);
		auto items = inv.items(); // copy (returns by value)
		wb.writeU32((uint32_t)items.size());
		for (auto& [itemId, count] : items) {
			wb.writeString(itemId);
			wb.writeI32(count);
		}
		for (auto& [cid, c] : clients)
			agentworld::net::sendMessage(c.fd, agentworld::net::S_INVENTORY, wb);
	};
	server.setCallbacks(cbs);

	// Fixed timestep server loop
	const float TICK_RATE = agentworld::ServerTuning::tickRate;
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
		auto accepted = listener.acceptClient();
		if (accepted.fd >= 0) {
			agentworld::ClientId cid = nextClientId++;
			agentworld::EntityId eid = server.addClient(cid);

			// Send S_WELCOME (player entity ID + spawn position)
			{
				agentworld::net::WriteBuffer wb;
				wb.writeU32(eid);
				wb.writeVec3(server.spawnPos());
				agentworld::net::sendMessage(accepted.fd, agentworld::net::S_WELCOME, wb);
			}

			// Send initial inventory
			{
				agentworld::Entity* pe = server.world().entities.get(eid);
				if (pe && pe->inventory) {
					agentworld::net::WriteBuffer wb;
					wb.writeU32(eid);
					auto items = pe->inventory->items();
					wb.writeU32((uint32_t)items.size());
					for (auto& [itemId, count] : items) {
						wb.writeString(itemId);
						wb.writeI32(count);
					}
					agentworld::net::sendMessage(accepted.fd, agentworld::net::S_INVENTORY, wb);
				}
			}

			// Queue initial chunks around spawn
			ConnectedClient cc;
			cc.fd = accepted.fd; cc.id = cid; cc.playerId = eid;
			cc.ip = accepted.ip; cc.port = accepted.port;

			auto sp = server.spawnPos();
			auto cp = agentworld::worldToChunk((int)sp.x, (int)sp.y, (int)sp.z);
			cc.lastChunkPos = cp;

			auto queueChunk = [&](agentworld::ChunkPos pos) {
				if (cc.sentChunks.count(pos)) return;
				agentworld::Chunk* chunk = server.world().getChunk(pos);
				if (!chunk) return;

				agentworld::net::WriteBuffer cb;
				cb.writeI32(pos.x); cb.writeI32(pos.y); cb.writeI32(pos.z);
				for (int i = 0; i < 16*16*16; i++)
					cb.writeU32(chunk->getRaw(i));

				std::vector<uint8_t> msg;
				agentworld::net::MsgHeader hdr;
				hdr.type = agentworld::net::S_CHUNK;
				hdr.length = (uint32_t)cb.size();
				msg.resize(8 + cb.size());
				memcpy(msg.data(), &hdr, 8);
				memcpy(msg.data() + 8, cb.data().data(), cb.size());
				cc.pendingChunks.push_back({pos, std::move(msg)});
				cc.sentChunks.insert(pos);
			};

			for (int dy = 0; dy <= 1; dy++)
			for (int dz = -4; dz <= 4; dz++)
			for (int dx = -4; dx <= 4; dx++)
				queueChunk({cp.x + dx, cp.y + dy, cp.z + dz});

			clients[cid] = std::move(cc);
			printf("[Server] %s joined (entity %u). Sending %zu chunks...\n",
			       clients[cid].label().c_str(), eid, clients[cid].pendingChunks.size());
		}

		// Send pending chunks to clients (non-blocking, a few per tick)
		for (auto& [cid, client] : clients) {
			int sent = 0;
			while (!client.pendingChunks.empty() && sent < 10) {
				auto& msg = client.pendingChunks.front().second;
				ssize_t n = send(client.fd, msg.data(), msg.size(), MSG_NOSIGNAL);
				if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
				if (n <= 0) break; // error or disconnected
				client.pendingChunks.erase(client.pendingChunks.begin());
				sent++;
			}
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
				case agentworld::net::C_HELLO: {
					client.name = rb.readString();
					std::string displayName = rb.hasMore() ? rb.readString() : "";
					std::string creatureType = rb.hasMore() ? rb.readString() : "";
					if (!displayName.empty())
						client.name = displayName + " (" + client.name.substr(0, 8) + ")";
					printf("[Server] %s identified: creature=%s\n",
						client.label().c_str(),
						creatureType.empty() ? "default" : creatureType.c_str());
					// TODO: respawn as requested creature type (requires removing old
					// entity and spawning new one — deferred to avoid mid-tick mutation)
					break;
				}
				default: break;
				}
			}
		}

		// Remove disconnected clients
		for (auto cid : disconnected) {
			printf("[Server] %s disconnected.\n", clients[cid].label().c_str());
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

		// Broadcast entity state (throttled: 20 Hz)
		// Skip clients still receiving initial chunks — their TCP buffer
		// is full of chunk data and sendMessage() would fail silently.
		static float broadcastTimer = 0;
		broadcastTimer += dt;
		if (broadcastTimer >= agentworld::ServerTuning::broadcastInterval && !clients.empty()) {
			broadcastTimer = 0;
			for (auto& [cid, client] : clients) {
				if (!client.pendingChunks.empty()) continue; // still loading

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

				// Stream chunks around player — preemptively load wider area.
				// Sends up to 4 new chunks per broadcast tick (20Hz × 4 = 80 chunks/sec).
				// R=6 means ~13×13×2 = 338 chunks total. Initial 81 + ~260 streamed.
				agentworld::Entity* pe = server.world().entities.get(client.playerId);
				if (pe && client.pendingChunks.size() < 20) { // don't queue too many
					auto cp = agentworld::worldToChunk(
						(int)pe->position.x, (int)pe->position.y, (int)pe->position.z);
					const int R = 6; // wider than render distance (8 chunks = 128 blocks)
					int queued = 0;
					for (int dy = -1; dy <= 2 && queued < 4; dy++)
					for (int dz = -R; dz <= R && queued < 4; dz++)
					for (int dx = -R; dx <= R && queued < 4; dx++) {
						agentworld::ChunkPos pos = {cp.x + dx, cp.y + dy, cp.z + dz};
						if (client.sentChunks.count(pos)) continue;
						agentworld::Chunk* chunk = server.world().getChunk(pos);
						if (!chunk) continue;

						agentworld::net::WriteBuffer cb;
						cb.writeI32(pos.x); cb.writeI32(pos.y); cb.writeI32(pos.z);
						for (int i = 0; i < 16*16*16; i++)
							cb.writeU32(chunk->getRaw(i));

						std::vector<uint8_t> msg;
						agentworld::net::MsgHeader hdr;
						hdr.type = agentworld::net::S_CHUNK;
						hdr.length = (uint32_t)cb.size();
						msg.resize(8 + cb.size());
						memcpy(msg.data(), &hdr, 8);
						memcpy(msg.data() + 8, cb.data().data(), cb.size());
						client.pendingChunks.push_back({pos, std::move(msg)});
						client.sentChunks.insert(pos);
						queued++;
					}
				}

				agentworld::net::WriteBuffer tb;
				tb.writeF32(server.worldTime());
				agentworld::net::sendMessage(client.fd, agentworld::net::S_TIME, tb);
			}
		}

		// Status logging
		if (statusTimer >= agentworld::ServerTuning::statusLogInterval) {
			int moving = 0;
			server.world().entities.forEach([&](agentworld::Entity& e) {
				float hSpeed = std::sqrt(e.velocity.x * e.velocity.x + e.velocity.z * e.velocity.z);
				if (hSpeed > 0.01f) moving++;
			});
			printf("[Server] %d ticks, %.1f tps, %zu entities (%d moving), %zu clients\n",
			       tickCount, tickCount / statusTimer,
			       server.world().entities.count(), moving,
			       clients.size());
			if (g_logFile) {
				fprintf(g_logFile, "[Server] %d ticks, %.1f tps, %zu entities (%d moving), %zu clients\n",
				        tickCount, tickCount / statusTimer,
				        server.world().entities.count(), moving, clients.size());
			}
			for (auto& [cid, c] : clients) {
				auto* pe = server.world().entities.get(c.playerId);
				if (pe) {
					printf("[Server]   %s: pos=(%.1f,%.1f,%.1f)\n",
						c.label().c_str(), pe->position.x, pe->position.y, pe->position.z);
				}
			}
			tickCount = 0;
			statusTimer = 0;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	// Save world on shutdown
	if (!worldPath.empty()) {
		printf("[Server] Saving world to %s...\n", worldPath.c_str());
		agentworld::WorldMetadata meta;
		meta.name = worldPath.substr(worldPath.rfind('/') + 1);
		meta.seed = config.seed;
		meta.templateIndex = config.templateIndex;
		meta.gameMode = config.creative ? "admin" : "survival";
		meta.version = 1;
		if (config.templateIndex < (int)templates.size())
			meta.templateName = templates[config.templateIndex]->name();
		agentworld::saveWorld(server, worldPath, meta);
	}

	// Cleanup
	for (auto& [cid, client] : clients)
		close(client.fd);
	listener.shutdown();

	printf("[Server] Shut down.\n");

	agentworld::pythonBridge().shutdown();
	return 0;
}

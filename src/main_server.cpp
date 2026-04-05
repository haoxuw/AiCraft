/**
 * Dedicated server — runs headless, accepts TCP client connections.
 *
 * Usage:
 *   ./agentica-server                              # interactive world selection
 *   ./agentica-server --world saves/my_village      # load saved world
 *   ./agentica-server --port 7778 --template 1      # new world on custom port
 */

#include "server/server.h"
#include "server/client_manager.h"
#include "server/world_template.h"
#include "server/world_save.h"
#include "client/world_manager.h"
#include "content/builtin.h"
#include "shared/net_socket.h"
#include "shared/net_protocol.h"
#include "server/python_bridge.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
#include <csignal>
#include <unistd.h>
#include <iostream>
#include <string>

static volatile bool g_running = true;
static char g_readyPath[64] = {};

static void signalHandler(int) {
	g_running = false;
	// Ready-file cleanup happens at end of main() after this sets g_running = false
}

// Interactive CLI: let user pick a world or create new
static void interactiveWorldSelect(agentica::ServerConfig& config,
                                    std::string& worldPath,
                                    const std::vector<std::shared_ptr<agentica::WorldTemplate>>& templates) {
	agentica::WorldManager mgr;
	mgr.setSavesDir("saves");
	mgr.refresh();

	auto& worlds = mgr.worlds();

	printf("\n");
	printf("  ┌─────────────────────────────────┐\n");
	printf("  │       AGENTICA SERVER          │\n");
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

	// Server log file
	int logPort = 7777;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
			logPort = atoi(argv[i + 1]);
	}
	char logPath[256];
	snprintf(logPath, sizeof(logPath), "/tmp/agentica_log_%d.log", logPort);
	FILE* logFile = fopen(logPath, "w");
	if (logFile) {
		setvbuf(logFile, nullptr, _IONBF, 0);
		printf("[Server] Also logging to %s\n", logPath);
	}

	printf("=== Agentica Dedicated Server ===\n");

	agentica::pythonBridge().init("python");

	// Parse args
	agentica::ServerConfig config;
	std::string worldPath;
	bool interactive = true;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("Agentica — dedicated server\n\n"
			       "Usage: %s [options]\n"
			       "  --port PORT       Listen port (default 7777)\n"
			       "  --world PATH      Load saved world from PATH\n"
			       "  --seed N          World seed (default 42)\n"
			       "  --template N      World template: 0=flat, 1=village (default 1)\n"
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
		}
	}

	// World templates
	std::vector<std::shared_ptr<agentica::WorldTemplate>> templates = {
		std::make_shared<agentica::FlatWorldTemplate>(),
		std::make_shared<agentica::VillageWorldTemplate>(),
	};

	if (interactive && isatty(fileno(stdin)))
		interactiveWorldSelect(config, worldPath, templates);

	// Initialize server
	agentica::GameServer server;
	if (!worldPath.empty() && std::filesystem::exists(worldPath + "/world.json")) {
		printf("[Server] Loading world from %s\n", worldPath.c_str());
		if (!agentica::loadWorld(server, worldPath, templates)) {
			printf("[Server] Failed to load world, creating new\n");
			server.init(config, templates);
		}
	} else {
		server.init(config, templates);
	}

	// Start TCP listener
	agentica::net::TcpServer listener;
	if (!listener.listen(config.port)) {
		printf("[Server] Failed to start listener on port %d\n", config.port);
		return 1;
	}

	printf("[Server] Listening on port %d (seed=%d, template=%d)\n",
	       config.port, config.seed, config.templateIndex);
	printf("[Server] Press Ctrl+C to save and stop.\n");

	// Signal readiness for launchers
	snprintf(g_readyPath, sizeof(g_readyPath), "/tmp/agentica_ready_%d", config.port);
	if (FILE* f = fopen(g_readyPath, "w")) fclose(f);

	// Client manager handles all TCP client operations + AI agent spawning
	agentica::ClientManager clients(server);

	// Determine executable directory for spawning AI agent processes
	{
		std::string exe = argv[0];
		auto pos = exe.rfind('/');
		std::string execDir = (pos != std::string::npos) ? exe.substr(0, pos) : ".";
		clients.setExecDir(execDir);
	}
	clients.setPort(config.port);

	// Network broadcast callbacks
	agentica::ServerCallbacks cbs;
	cbs.onBlockChange = [&](glm::ivec3 pos, agentica::BlockId bid) {
		agentica::net::WriteBuffer wb;
		wb.writeI32(pos.x); wb.writeI32(pos.y); wb.writeI32(pos.z);
		wb.writeU32(bid);
		clients.broadcastToAll(agentica::net::S_BLOCK, wb);
	};
	cbs.onEntityRemove = [&](agentica::EntityId id) {
		agentica::ClientId owner = server.getEntityOwner(id);
		if (owner != 0) {
			server.revokeEntityFromClient(owner, id);
			auto* c = clients.getClient(owner);
			if (c) {
				agentica::net::WriteBuffer rwb;
				rwb.writeU32(id);
				agentica::net::sendMessage(c->fd, agentica::net::S_REVOKE_ENTITY, rwb);
			}
		}
		agentica::net::WriteBuffer wb;
		wb.writeU32(id);
		clients.broadcastToAll(agentica::net::S_REMOVE, wb);
	};
	cbs.onInventoryChange = [&](agentica::EntityId id, const agentica::Inventory& inv) {
		agentica::net::WriteBuffer wb;
		wb.writeU32(id);
		auto items = inv.items();
		wb.writeU32((uint32_t)items.size());
		for (auto& [itemId, count] : items) {
			wb.writeString(itemId);
			wb.writeI32(count);
		}
		for (int i = 0; i < agentica::Inventory::HOTBAR_SLOTS; i++)
			wb.writeString(inv.hotbar(i));
		// Equipment slots
		for (int i = 0; i < agentica::WEAR_SLOT_COUNT; i++)
			wb.writeString(inv.equipped((agentica::WearSlot)i));
		clients.broadcastToAll(agentica::net::S_INVENTORY, wb);
	};
	server.setCallbacks(cbs);

	// Main loop
	const float TICK_RATE = agentica::ServerTuning::tickRate;
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

		clients.acceptConnections(listener);
		clients.sendPendingChunks();
		clients.receiveMessages();
		clients.pruneDisconnected();

		while (accumulator >= TICK_RATE) {
			server.tick(TICK_RATE);
			accumulator -= TICK_RATE;
			tickCount++;
		}

		clients.forwardBehaviorReloads();
		clients.spawnAIClients(); // spawn agent processes for uncontrolled NPCs
		clients.broadcastState(dt);
		clients.announceOnLAN(dt);
		clients.logStatus(statusTimer, tickCount, logFile);

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	// Save world on shutdown
	if (!worldPath.empty()) {
		printf("[Server] Saving world to %s...\n", worldPath.c_str());
		agentica::WorldMetadata meta;
		meta.name = worldPath.substr(worldPath.rfind('/') + 1);
		meta.seed = config.seed;
		meta.templateIndex = config.templateIndex;
		meta.gameMode = "playing";
		meta.version = 1;
		if (config.templateIndex < (int)templates.size())
			meta.templateName = templates[config.templateIndex]->name();
		agentica::saveWorld(server, worldPath, meta);
	}

	clients.disconnectAll();
	listener.shutdown();
	if (logFile) fclose(logFile);

	// Remove ready-file so AgentManager's findFreePort() can reuse this port
	if (g_readyPath[0]) std::remove(g_readyPath);

	printf("[Server] Shut down.\n");
	agentica::pythonBridge().shutdown();
	return 0;
}

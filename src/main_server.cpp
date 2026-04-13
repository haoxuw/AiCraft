/**
 * Dedicated server — runs headless, accepts TCP client connections.
 *
 * Usage:
 *   ./modcraft-server                              # interactive world selection
 *   ./modcraft-server --world saves/my_village      # load saved world
 *   ./modcraft-server --port 7778 --template 1      # new world on custom port
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
#include "shared/artifact_registry.h"
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
static void interactiveWorldSelect(modcraft::ServerConfig& config,
                                    std::string& worldPath,
                                    const std::vector<std::shared_ptr<modcraft::WorldTemplate>>& templates) {
	modcraft::WorldManager mgr;
	mgr.setSavesDir("saves");
	mgr.refresh();

	auto& worlds = mgr.worlds();

	printf("\n");
	printf("  ┌─────────────────────────────────┐\n");
	printf("  │       MODCRAFT SERVER          │\n");
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
	snprintf(logPath, sizeof(logPath), "/tmp/modcraft_log_%d.log", logPort);
	FILE* logFile = fopen(logPath, "w");
	if (logFile) {
		setvbuf(logFile, nullptr, _IONBF, 0);
		printf("[Server] Also logging to %s\n", logPath);
	}

	printf("=== ModCraft Dedicated Server ===\n");

	modcraft::pythonBridge().init("python");

	// Parse args
	modcraft::ServerConfig config;
	std::string worldPath;
	bool interactive = true;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("ModCraft — dedicated server\n\n"
			       "Usage: %s [options]\n"
			       "  --port PORT       Listen port (default 7777)\n"
			       "  --world PATH      Load saved world from PATH\n"
			       "  --seed N          World seed (default 42)\n"
			       "  --template N      World template: 0=flat, 1=village, 2=test_behaviors,\n"
			       "                    3=test_dog, 4=test_villager (default 1)\n"
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

	// World templates — indices referenced by --template:
	//   0 flat  1 village  2 test_behaviors  3 test_dog  4 test_villager
	std::vector<std::shared_ptr<modcraft::WorldTemplate>> templates = {
		std::make_shared<modcraft::ConfigurableWorldTemplate>("artifacts/worlds/base/flat.py"),
		std::make_shared<modcraft::ConfigurableWorldTemplate>("artifacts/worlds/base/village.py"),
		std::make_shared<modcraft::ConfigurableWorldTemplate>("artifacts/worlds/base/test_behaviors.py"),
		std::make_shared<modcraft::ConfigurableWorldTemplate>("artifacts/worlds/base/test_dog.py"),
		std::make_shared<modcraft::ConfigurableWorldTemplate>("artifacts/worlds/base/test_villager.py"),
	};

	if (interactive && isatty(fileno(stdin)))
		interactiveWorldSelect(config, worldPath, templates);

	// Initialize server
	modcraft::GameServer server;
	if (!worldPath.empty() && std::filesystem::exists(worldPath + "/world.json")) {
		printf("[Server] Loading world from %s\n", worldPath.c_str());
		if (!modcraft::loadWorld(server, worldPath, templates)) {
			printf("[Server] Failed to load world, creating new\n");
			server.init(config, templates);
		}
	} else {
		server.init(config, templates);
	}

	// Load artifact registry and merge Python-declared feature tags into EntityDefs
	{
		modcraft::ArtifactRegistry artifacts;
		artifacts.loadAll("artifacts");
		server.mergeArtifactTags(artifacts.livingTags());
	}

	// Start TCP listener
	modcraft::net::TcpServer listener;
	if (!listener.listen(config.port)) {
		printf("[Server] Failed to start listener on port %d\n", config.port);
		return 1;
	}

	printf("[Server] Listening on port %d (seed=%d, template=%d)\n",
	       config.port, config.seed, config.templateIndex);
	printf("[Server] Press Ctrl+C to save and stop.\n");

	// Signal readiness for launchers
	snprintf(g_readyPath, sizeof(g_readyPath), "/tmp/modcraft_ready_%d", config.port);
	if (FILE* f = fopen(g_readyPath, "w")) fclose(f);

	// Client manager handles all TCP client operations + AI agent spawning
	modcraft::ClientManager clients(server);

	clients.setPort(config.port);

	// Network broadcast callbacks
	modcraft::ServerCallbacks cbs;
	cbs.onBlockChange = [&](glm::ivec3 pos, modcraft::BlockId oldBid, modcraft::BlockId newBid, uint8_t p2) {
		clients.onBlockChanged(pos, oldBid, newBid, p2);
	};
	cbs.onEntityRemove = [&](modcraft::EntityId id) {
		modcraft::net::WriteBuffer wb;
		wb.writeU32(id);
		clients.broadcastToAll(modcraft::net::S_REMOVE, wb);
	};
	cbs.onInventoryChange = [&](modcraft::EntityId id, const modcraft::Inventory& inv) {
		modcraft::net::WriteBuffer wb;
		wb.writeU32(id);
		auto items = inv.items();
		wb.writeU32((uint32_t)items.size());
		for (auto& [itemId, count] : items) {
			wb.writeString(itemId);
			wb.writeI32(count);
		}
		// Equipment slots (count + slot/item pairs)
		uint8_t equipCount = 0;
		for (int i = 0; i < modcraft::WEAR_SLOT_COUNT; i++)
			if (!inv.equipped((modcraft::WearSlot)i).empty()) equipCount++;
		wb.writeU8(equipCount);
		for (int i = 0; i < modcraft::WEAR_SLOT_COUNT; i++) {
			const auto& eid = inv.equipped((modcraft::WearSlot)i);
			if (!eid.empty()) {
				wb.writeString(modcraft::equipSlotName((modcraft::WearSlot)i));
				wb.writeString(eid);
			}
		}
		clients.broadcastToAll(modcraft::net::S_INVENTORY, wb);
	};
	server.setCallbacks(cbs);

	// Main loop
	const float TICK_RATE = modcraft::ServerTuning::tickRate;
	const double TICK_BUDGET_MS = TICK_RATE * 1000.0;  // 16.67ms at 60tps
	auto lastTime = std::chrono::steady_clock::now();
	float accumulator = 0;
	int tickCount = 0;
	float statusTimer = 0;
	int slowTickCount = 0;
	double worstTickMs = 0.0;
	float perfTimer = 0.0f;
	constexpr float PERF_LOG_INTERVAL = 5.0f;  // same cadence as status log

	while (g_running) {
		auto frameStart = std::chrono::steady_clock::now();
		auto now = frameStart;
		float dt = std::chrono::duration<float>(now - lastTime).count();
		lastTime = now;
		accumulator += dt;
		statusTimer += dt;
		perfTimer += dt;

		clients.acceptConnections(listener);
		clients.sendPendingChunks();
		clients.receiveMessages(dt);
		clients.pruneDisconnected();

		// Measure only the simulation cost (physics, AI, entity updates) —
		// this is what the fixed tick budget applies to.
		auto tickStart = std::chrono::steady_clock::now();
		int ticksThisFrame = 0;
		while (accumulator >= TICK_RATE) {
			// Catch C++ exceptions so a single bad tick doesn't kill the
			// server silently — client would then see "server silent" with
			// no hint of what happened. We log and advance the accumulator
			// to avoid hot-spinning on a permanently-broken tick.
			try {
				server.tick(TICK_RATE);
			} catch (const std::exception& ex) {
				fprintf(stderr, "[ServerCrash] tick threw std::exception: %s\n", ex.what());
				fflush(stderr);
			} catch (...) {
				fprintf(stderr, "[ServerCrash] tick threw unknown exception\n");
				fflush(stderr);
			}
			accumulator -= TICK_RATE;
			tickCount++;
			ticksThisFrame++;
		}
		auto tickEnd = std::chrono::steady_clock::now();

		if (ticksThisFrame > 0) {
			double tickMs = std::chrono::duration<double, std::milli>(tickEnd - tickStart).count();
			double perTickMs = tickMs / ticksThisFrame;
			if (perTickMs > TICK_BUDGET_MS) {
				slowTickCount++;
				if (perTickMs > worstTickMs) worstTickMs = perTickMs;
				// Log the first few and then every 30th breach to avoid spam
				if (slowTickCount <= 5 || slowTickCount % 30 == 0) {
					fprintf(stderr, "[Perf] SLOW tick: %.1fms (budget %.1fms, %d ticks this frame, %zu entities)\n",
						perTickMs, TICK_BUDGET_MS, ticksThisFrame,
						server.world().entities.count());
				}
			}
		}

		clients.broadcastState(dt);
		clients.announceOnLAN(dt);
		clients.logStatus(statusTimer, tickCount, logFile);

		if (perfTimer >= PERF_LOG_INTERVAL) {
			if (slowTickCount > 0)
				fprintf(stderr, "[Perf] Last %.0fs: %d slow ticks, worst=%.1fms (budget %.1fms)\n",
					perfTimer, slowTickCount, worstTickMs, TICK_BUDGET_MS);
			slowTickCount = 0;
			worstTickMs = 0;
			perfTimer = 0;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	// Save world on shutdown
	if (!worldPath.empty()) {
		printf("[Server] Saving world to %s...\n", worldPath.c_str());
		modcraft::WorldMetadata meta;
		meta.name = worldPath.substr(worldPath.rfind('/') + 1);
		meta.seed = config.seed;
		meta.templateIndex = config.templateIndex;
		meta.gameMode = "playing";
		meta.version = 1;
		if (config.templateIndex < (int)templates.size())
			meta.templateName = templates[config.templateIndex]->name();
		modcraft::saveWorld(server, worldPath, meta);
	}

	clients.disconnectAll();
	listener.shutdown();
	if (logFile) fclose(logFile);

	// Remove ready-file so AgentManager's findFreePort() can reuse this port
	if (g_readyPath[0]) std::remove(g_readyPath);

	printf("[Server] Shut down.\n");
	modcraft::pythonBridge().shutdown();
	return 0;
}

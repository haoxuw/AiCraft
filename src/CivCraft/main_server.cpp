/**
 * Dedicated server — runs headless, accepts TCP client connections.
 *
 * Usage:
 *   ./civcraft-server                              # interactive world selection
 *   ./civcraft-server --world saves/my_village      # load saved world
 *   ./civcraft-server --port 7778 --template 1      # new world on custom port
 */

#include "server/server.h"
#include "server/client_manager.h"
#include "server/world_template.h"
#include "server/world_save.h"
#include "client/world_manager.h"
#include "content/builtin.h"
#include "net/net_socket.h"
#include "net/net_protocol.h"
#include "server/python_bridge.h"
#include "logic/artifact_registry.h"
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
static void interactiveWorldSelect(civcraft::ServerConfig& config,
                                    std::string& worldPath,
                                    const std::vector<std::shared_ptr<civcraft::WorldTemplate>>& templates) {
	civcraft::WorldManager mgr;
	mgr.setSavesDir("saves");
	mgr.refresh();

	auto& worlds = mgr.worlds();

	printf("\n");
	printf("  ┌─────────────────────────────────┐\n");
	printf("  │       CIVCRAFT SERVER          │\n");
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
	snprintf(logPath, sizeof(logPath), "/tmp/civcraft_log_%d.log", logPort);
	FILE* logFile = fopen(logPath, "w");
	if (logFile) {
		setvbuf(logFile, nullptr, _IONBF, 0);
		printf("[Server] Also logging to %s\n", logPath);
	}

	printf("=== CivCraft Dedicated Server ===\n");

	civcraft::pythonBridge().init("python");

	// Parse args
	civcraft::ServerConfig config;
	std::string worldPath;
	bool interactive = true;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("CivCraft — dedicated server\n\n"
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
	std::vector<std::shared_ptr<civcraft::WorldTemplate>> templates = {
		std::make_shared<civcraft::ConfigurableWorldTemplate>("artifacts/worlds/base/flat.py"),
		std::make_shared<civcraft::ConfigurableWorldTemplate>("artifacts/worlds/base/village.py"),
		std::make_shared<civcraft::ConfigurableWorldTemplate>("artifacts/worlds/base/test_behaviors.py"),
		std::make_shared<civcraft::ConfigurableWorldTemplate>("artifacts/worlds/base/test_dog.py"),
		std::make_shared<civcraft::ConfigurableWorldTemplate>("artifacts/worlds/base/test_villager.py"),
	};

	if (interactive && isatty(fileno(stdin)))
		interactiveWorldSelect(config, worldPath, templates);

	// Initialize server
	civcraft::GameServer server;
	if (!worldPath.empty() && std::filesystem::exists(worldPath + "/world.json")) {
		printf("[Server] Loading world from %s\n", worldPath.c_str());
		if (!civcraft::loadWorld(server, worldPath, templates)) {
			printf("[Server] Failed to load world, creating new\n");
			server.init(config, templates);
		}
	} else {
		server.init(config, templates);
	}

	// Load artifact registry and merge Python-declared feature tags into EntityDefs
	{
		civcraft::ArtifactRegistry artifacts;
		artifacts.loadAll("artifacts");
		server.mergeArtifactTags(artifacts.livingTags());

		// Feed annotation spawn rules into the World so chunk gen can scatter
		// flowers (and anything else declared under artifacts/annotations/).
		for (auto* e : artifacts.byCategory("annotation")) {
			civcraft::World::AnnotationSpawnRule rule;
			rule.typeId = e->id;
			auto sIt = e->fields.find("slot");
			if (sIt != e->fields.end()) {
				if      (sIt->second == "top")    rule.slot = civcraft::AnnotationSlot::Top;
				else if (sIt->second == "bottom") rule.slot = civcraft::AnnotationSlot::Bottom;
				else if (sIt->second == "around") rule.slot = civcraft::AnnotationSlot::Around;
			}
			auto chIt = e->fields.find("spawn_chance");
			if (chIt != e->fields.end()) rule.chance = (float)std::atof(chIt->second.c_str());
			auto onIt = e->fields.find("spawn_on");
			if (onIt != e->fields.end()) {
				std::string s = onIt->second;
				size_t start = 0;
				while (start < s.size()) {
					size_t comma = s.find(',', start);
					std::string tok = s.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
					if (!tok.empty()) rule.onBlocks.push_back(tok);
					if (comma == std::string::npos) break;
					start = comma + 1;
				}
			}
			if (!rule.onBlocks.empty() && rule.chance > 0.0f)
				server.world().addAnnotationSpawnRule(rule);
		}
	}

	// Start TCP listener
	civcraft::net::TcpServer listener;
	if (!listener.listen(config.port)) {
		printf("[Server] Failed to start listener on port %d\n", config.port);
		return 1;
	}

	printf("[Server] Listening on port %d (seed=%d, template=%d)\n",
	       config.port, config.seed, config.templateIndex);
	printf("[Server] Press Ctrl+C to save and stop.\n");

	// Signal readiness for launchers
	snprintf(g_readyPath, sizeof(g_readyPath), "/tmp/civcraft_ready_%d", config.port);
	if (FILE* f = fopen(g_readyPath, "w")) fclose(f);

	// Client manager handles all TCP client operations + AI agent spawning
	civcraft::ClientManager clients(server);

	clients.setPort(config.port);

	// Network broadcast callbacks
	civcraft::ServerCallbacks cbs;
	cbs.onBlockChange = [&](glm::ivec3 pos, civcraft::BlockId oldBid, civcraft::BlockId newBid, uint8_t p2) {
		clients.onBlockChanged(pos, oldBid, newBid, p2);
	};
	cbs.onEntityRemove = [&](civcraft::EntityId id) {
		civcraft::net::WriteBuffer wb;
		wb.writeU32(id);
		clients.broadcastToAll(civcraft::net::S_REMOVE, wb);
	};
	cbs.onInventoryChange = [&](civcraft::EntityId id, const civcraft::Inventory& inv) {
		civcraft::net::WriteBuffer wb;
		wb.writeU32(id);
		auto items = inv.items();
		wb.writeU32((uint32_t)items.size());
		for (auto& [itemId, count] : items) {
			wb.writeString(itemId);
			wb.writeI32(count);
		}
		// Equipment slots (count + slot/item pairs)
		uint8_t equipCount = 0;
		for (int i = 0; i < civcraft::WEAR_SLOT_COUNT; i++)
			if (!inv.equipped((civcraft::WearSlot)i).empty()) equipCount++;
		wb.writeU8(equipCount);
		for (int i = 0; i < civcraft::WEAR_SLOT_COUNT; i++) {
			const auto& eid = inv.equipped((civcraft::WearSlot)i);
			if (!eid.empty()) {
				wb.writeString(civcraft::equipSlotName((civcraft::WearSlot)i));
				wb.writeString(eid);
			}
		}
		clients.broadcastToAll(civcraft::net::S_INVENTORY, wb);
	};
	server.setCallbacks(cbs);

	// Main loop
	const float TICK_RATE = civcraft::ServerTuning::tickRate;
	const double TICK_BUDGET_MS = TICK_RATE * 1000.0;  // 16.67ms at 60tps
	auto lastTime = std::chrono::steady_clock::now();
	float accumulator = 0;
	int tickCount = 0;
	float statusTimer = 0;
#ifdef CIVCRAFT_PERF
	int slowTickCount = 0;
	double worstTickMs = 0.0;
	civcraft::GameServer::TickProfile worstProfile{};
	float perfTimer = 0.0f;
	constexpr float PERF_LOG_INTERVAL = 5.0f;  // same cadence as status log

	// Frame-phase timing for the worst frame in the perf window.
	struct FramePhases {
		double acceptMs = 0, sendChunksMs = 0, receiveMs = 0, pruneMs = 0;
		double tickMs = 0, broadcastMs = 0, announceLanMs = 0, statusLogMs = 0;
		double totalMs = 0;
	};
	FramePhases worstFrame{};
	double worstFrameMs = 0.0;
	int slowFrameCount = 0;
	constexpr double SLOW_FRAME_MS = 16.7;  // anything above the 60Hz budget
#endif

	while (g_running) {
		auto frameStart = std::chrono::steady_clock::now();
		auto now = frameStart;
		float dt = std::chrono::duration<float>(now - lastTime).count();
		lastTime = now;
		accumulator += dt;
		statusTimer += dt;
#ifdef CIVCRAFT_PERF
		perfTimer += dt;

		FramePhases fp{};
		auto phaseStart = std::chrono::steady_clock::now();
		auto markPhase = [&](double& slot) {
			auto t = std::chrono::steady_clock::now();
			slot = std::chrono::duration<double, std::milli>(t - phaseStart).count();
			phaseStart = t;
		};
#else
		auto markPhase = [](double&) {};
		struct { double acceptMs, sendChunksMs, receiveMs, pruneMs,
		         tickMs, broadcastMs, announceLanMs, statusLogMs, totalMs; } fp{};
#endif

		clients.acceptConnections(listener);
		markPhase(fp.acceptMs);
		clients.sendPendingChunks();
		markPhase(fp.sendChunksMs);
		clients.receiveMessages(dt);
		markPhase(fp.receiveMs);
		// Drain async chunk-gen results + finalize Preparing clients.
		clients.advancePreparing();
		// Drop any client whose heartbeat stopped arriving (convergent path
		// with C_QUIT and TCP close: all three end up in pruneDisconnected).
		clients.checkIdleTimeouts(dt);
		clients.pruneDisconnected();
		markPhase(fp.pruneMs);

		// Measure only the simulation cost (physics, AI, entity updates) —
		// this is what the fixed tick budget applies to.
		[[maybe_unused]] auto tickStart = std::chrono::steady_clock::now();
		int ticksThisFrame = 0;
		while (accumulator >= TICK_RATE) {
			// Catch C++ exceptions so a single bad tick doesn't kill the
			// server silently — client would then see "server silent" with
			// no hint of what happened. We log and advance the accumulator
			// to avoid hot-spinning on a permanently-broken tick.
			try {
				server.tick(TICK_RATE);
			} catch (const std::exception& ex) {
				fprintf(stdout, "[ServerCrash] tick threw std::exception: %s\n", ex.what());
				fflush(stdout);
			} catch (...) {
				fprintf(stdout, "[ServerCrash] tick threw unknown exception\n");
				fflush(stdout);
			}
			accumulator -= TICK_RATE;
			tickCount++;
			ticksThisFrame++;
		}
		[[maybe_unused]] auto tickEnd = std::chrono::steady_clock::now();
#ifdef CIVCRAFT_PERF
		fp.tickMs = std::chrono::duration<double, std::milli>(tickEnd - tickStart).count();
		phaseStart = tickEnd;
#endif

#ifdef CIVCRAFT_PERF
		if (ticksThisFrame > 0) {
			double tickMs = std::chrono::duration<double, std::milli>(tickEnd - tickStart).count();
			double perTickMs = tickMs / ticksThisFrame;
			if (perTickMs > TICK_BUDGET_MS) {
				slowTickCount++;
				if (perTickMs > worstTickMs) {
					worstTickMs = perTickMs;
					// Capture the last tick's phase breakdown — only meaningful
					// when ticksThisFrame == 1 (otherwise it's just the final
					// tick of a burst), but the burst case is rare and the
					// single-tick case is what we care about.
					worstProfile = server.lastTickProfile();
				}
				// Per-breach logging removed — the 5s window summary below
				// reports count + worst + top phase, which is what you need.
			}
		}
#endif

		clients.broadcastState(dt);
		markPhase(fp.broadcastMs);
		clients.announceOnLAN(dt);
		markPhase(fp.announceLanMs);
		clients.logStatus(statusTimer, tickCount, logFile);
		markPhase(fp.statusLogMs);

#ifdef CIVCRAFT_PERF
		fp.totalMs = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - frameStart).count();
		if (fp.totalMs > SLOW_FRAME_MS) {
			slowFrameCount++;
			if (fp.totalMs > worstFrameMs) {
				worstFrameMs = fp.totalMs;
				worstFrame = fp;
			}
		}

		if (perfTimer >= PERF_LOG_INTERVAL) {
			// Healthy windows stay silent — only report when something breached
			// a budget. Each breach type collapses to "count, worst, top phase"
			// so one line tells you "how bad, and where to look next".
			if (slowTickCount > 0 || slowFrameCount > 0) {
				auto topTick = [&]() {
					struct { const char* n; double v; } x[] = {
						{"resolve",   worstProfile.resolveActionsMs},
						{"nav",       worstProfile.navigationMs},
						{"phys",      worstProfile.physicsMs},
						{"yaw",       worstProfile.yawSmoothMs},
						{"blocks",    worstProfile.activeBlocksMs},
						{"hpregen",   worstProfile.hpRegenMs},
						{"structregen", worstProfile.structureRegenMs},
						{"stuck",     worstProfile.stuckDetectionMs},
					};
					const char* bn = "?"; double bv = 0.0;
					for (auto& e : x) if (e.v > bv) { bv = e.v; bn = e.n; }
					return std::make_pair(bn, bv);
				};
				auto topFrame = [&]() {
					struct { const char* n; double v; } x[] = {
						{"tick",       worstFrame.tickMs},
						{"broadcast",  worstFrame.broadcastMs},
						{"sendChunks", worstFrame.sendChunksMs},
						{"recv",       worstFrame.receiveMs},
						{"prune",      worstFrame.pruneMs},
						{"accept",     worstFrame.acceptMs},
						{"lan",        worstFrame.announceLanMs},
						{"statusLog",  worstFrame.statusLogMs},
					};
					const char* bn = "?"; double bv = 0.0;
					for (auto& e : x) if (e.v > bv) { bv = e.v; bn = e.n; }
					return std::make_pair(bn, bv);
				};
				char tickPart[96] = "";
				char framePart[96] = "";
				if (slowTickCount > 0) {
					auto [n, v] = topTick();
					snprintf(tickPart, sizeof(tickPart), " ticks=%d worst=%.1fms(%s=%.1f)",
						slowTickCount, worstTickMs, n, v);
				}
				if (slowFrameCount > 0) {
					auto [n, v] = topFrame();
					snprintf(framePart, sizeof(framePart), " frames=%d worst=%.1fms(%s=%.1f)",
						slowFrameCount, worstFrameMs, n, v);
				}
				fprintf(stderr, "[Perf] 5s budget=%.1fms:%s%s\n",
					TICK_BUDGET_MS, tickPart, framePart);
			}
			slowTickCount = 0;
			worstTickMs = 0;
			worstProfile = {};
			slowFrameCount = 0;
			worstFrameMs = 0;
			worstFrame = {};
			perfTimer = 0;
		}
#endif

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	// Order matters: disconnect clients FIRST so their owned NPCs get
	// snapshotted into savedOwnedEntities (via removeClient), then save.
	// Otherwise entities.bin would carry owned mobs into the next boot as
	// orphaned zombies with stale owner ids, and owned_entities.bin would
	// be empty.
	clients.disconnectAll();

	// Save world on shutdown
	if (!worldPath.empty()) {
		printf("[Server] Saving world to %s...\n", worldPath.c_str());
		civcraft::WorldMetadata meta;
		meta.name = worldPath.substr(worldPath.rfind('/') + 1);
		meta.seed = config.seed;
		meta.templateIndex = config.templateIndex;
		meta.gameMode = "playing";
		meta.version = 1;
		if (config.templateIndex < (int)templates.size())
			meta.templateName = templates[config.templateIndex]->name();
		civcraft::saveWorld(server, worldPath, meta);
	}

	listener.shutdown();
	if (logFile) fclose(logFile);

	// Remove ready-file so AgentManager's findFreePort() can reuse this port
	if (g_readyPath[0]) std::remove(g_readyPath);

	printf("[Server] Shut down.\n");
	civcraft::pythonBridge().shutdown();
	return 0;
}

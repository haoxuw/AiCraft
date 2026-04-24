// Dedicated headless server; accepts TCP clients.
// Usage: ./civcraft-server [--world PATH] [--port N] [--template N]

#include "server/server.h"
#include "server/client_manager.h"
#include "server/world_template.h"
#include "server/world_save.h"
#include "server/world_manager.h"
#include "server/builtin.h"
#include "net/net_socket.h"
#include "net/net_protocol.h"
#include "python/python_bridge.h"
#include "logic/artifact_registry.h"
#include "debug/perf_registry.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <ctime>
#include <thread>
#include <csignal>
#include <unistd.h>
#include <iostream>
#include <string>

static volatile bool g_running = true;
static char g_readyPath[64] = {};

static void signalHandler(int) {
	g_running = false;
	// ready-file cleanup happens after main loop exits.
}

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

// ─────────────────────────────────────────────────────────────────────
// Boot helpers — each one does a single stage of main() so the top-level
// function is a short outline instead of a 500-line scroll of globals.
// ─────────────────────────────────────────────────────────────────────

namespace {

// Parsed CLI state, passed around by value.
struct ServerCliArgs {
	civcraft::ServerConfig config;
	std::string            worldPath;
	bool                   interactive = true;
	int                    logPort     = 7777;
};

// Print --help text and exit(0). Kept as a function so both the arg
// parser and the help-flag check share one source for the usage string.
void printServerUsage(const char* prog) {
	printf("CivCraft — dedicated server\n\n"
	       "Usage: %s [options]\n"
	       "  --port PORT       Listen port (default 7777)\n"
	       "  --world PATH      Load saved world from PATH\n"
	       "  --seed N          World seed (default 42)\n"
	       "  --template N      World template: 0=village, 1=test_behaviors,\n"
	       "                    2=test_dog, 3=test_villager, 4=test_chicken,\n"
	       "                    5=perf_stress (100 villagers) (default 0)\n"
	       "  --sim-speed N     Sim-time multiplier (default 1; 4 = 4× faster sim)\n"
	       "  --help, -h        Show this help\n", prog);
}

// Parse argv into a ServerCliArgs. Handles --help by exiting.
// `interactive` stays true only when the user supplied nothing but
// --port (so no preselected world/seed/template).
ServerCliArgs parseServerArgs(int argc, char** argv) {
	ServerCliArgs out;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printServerUsage(argv[0]);
			exit(0);
		}
		if      (strcmp(argv[i], "--port")     == 0 && i + 1 < argc) {
			out.config.port = atoi(argv[++i]);
			out.logPort     = out.config.port;
		}
		else if (strcmp(argv[i], "--seed")     == 0 && i + 1 < argc) {
			out.config.seed = atoi(argv[++i]); out.interactive = false;
		}
		else if (strcmp(argv[i], "--template") == 0 && i + 1 < argc) {
			out.config.templateIndex = atoi(argv[++i]); out.interactive = false;
		}
		else if (strcmp(argv[i], "--world")    == 0 && i + 1 < argc) {
			out.worldPath = argv[++i]; out.interactive = false;
		}
		else if (strcmp(argv[i], "--sim-speed") == 0 && i + 1 < argc) {
			float s = (float)atof(argv[++i]);
			if (s > 0.0f) civcraft::ServerTuning::simSpeed = s;
		}
	}
	return out;
}

// Open the per-port tee log file (tmpfs). Returns the FILE* handle to
// close at shutdown; nullptr if open failed.
FILE* openServerLog(int port) {
	char logPath[256];
	snprintf(logPath, sizeof(logPath), "/tmp/civcraft_log_%d.log", port);
	FILE* f = fopen(logPath, "w");
	if (f) {
		setvbuf(f, nullptr, _IONBF, 0);
		printf("[Server] Also logging to %s\n", logPath);
	}
	return f;
}

// Build the ordered template list the world-select + save-load code
// indexes by integer. One place to register a new template.
std::vector<std::shared_ptr<civcraft::WorldTemplate>> buildWorldTemplates() {
	using T = civcraft::ConfigurableWorldTemplate;
	return {
		std::make_shared<T>("artifacts/worlds/base/village.py"),
		std::make_shared<T>("artifacts/worlds/base/test_behaviors.py"),
		std::make_shared<T>("artifacts/worlds/base/test_dog.py"),
		std::make_shared<T>("artifacts/worlds/base/test_villager.py"),
		std::make_shared<T>("artifacts/worlds/base/test_chicken.py"),
		std::make_shared<T>("artifacts/worlds/base/perf_stress.py"),
	};
}

// Initialise GameServer from either a saved world on disk or a fresh
// template. Returns true on success; server is usable on return.
bool initServerFromArgs(civcraft::GameServer& server,
                         const ServerCliArgs& args,
                         const std::vector<std::shared_ptr<civcraft::WorldTemplate>>& templates) {
	if (!args.worldPath.empty() &&
	    std::filesystem::exists(args.worldPath + "/world.json")) {
		printf("[Server] Loading world from %s\n", args.worldPath.c_str());
		if (civcraft::loadWorld(server, args.worldPath, templates)) return true;
		printf("[Server] Failed to load world, creating new\n");
	}
	server.init(args.config, templates);
	return true;
}

// Read artifact metadata (feature tags, living stats, annotation
// spawn rules) and hand them to the server. Runs once after init.
void applyArtifactData(civcraft::GameServer& server) {
	civcraft::ArtifactRegistry artifacts;
	artifacts.loadAll("artifacts");
	server.mergeArtifactTags(artifacts.livingTags());
	server.applyLivingStats(artifacts.livingStats());

	for (auto* e : artifacts.byCategory("annotation")) {
		civcraft::World::AnnotationSpawnRule rule;
		rule.typeId = e->id;
		if (auto sIt = e->fields.find("slot"); sIt != e->fields.end()) {
			if      (sIt->second == "top")    rule.slot = civcraft::AnnotationSlot::Top;
			else if (sIt->second == "bottom") rule.slot = civcraft::AnnotationSlot::Bottom;
			else if (sIt->second == "around") rule.slot = civcraft::AnnotationSlot::Around;
		}
		if (auto chIt = e->fields.find("spawn_chance"); chIt != e->fields.end())
			rule.chance = (float)std::atof(chIt->second.c_str());
		if (auto onIt = e->fields.find("spawn_on"); onIt != e->fields.end()) {
			const std::string& s = onIt->second;
			for (size_t start = 0; start < s.size(); ) {
				size_t comma = s.find(',', start);
				std::string tok = s.substr(start,
					comma == std::string::npos ? std::string::npos : comma - start);
				if (!tok.empty()) rule.onBlocks.push_back(tok);
				if (comma == std::string::npos) break;
				start = comma + 1;
			}
		}
		if (!rule.onBlocks.empty() && rule.chance > 0.0f)
			server.world().addAnnotationSpawnRule(rule);
	}
}

// Build the block/entity/inventory broadcast callbacks that bridge
// GameServer mutations → ClientManager wire messages. One place to
// tweak serialisation formats.
civcraft::ServerCallbacks makeServerCallbacks(civcraft::ClientManager& clients) {
	civcraft::ServerCallbacks cbs;
	cbs.onBlockChange = [&clients](const civcraft::BlockChange& bc,
	                                civcraft::BroadcastPriority pri) {
		clients.onBlockChanged(bc, pri);
	};
	cbs.onEntityRemove = [&clients](civcraft::EntityId id, uint8_t reason) {
		clients.broadcastEntityRemove(id,
			(civcraft::net::EntityRemoveReason)reason);
	};
	cbs.onInventoryChange = [&clients](civcraft::EntityId id,
	                                     const civcraft::Inventory& inv) {
		civcraft::net::WriteBuffer wb;
		wb.writeU32(id);
		auto items = inv.items();
		wb.writeU32((uint32_t)items.size());
		for (auto& [itemId, count] : items) {
			wb.writeString(itemId);
			wb.writeI32(count);
		}
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
	return cbs;
}

// Persist the current world to disk if the user specified --world.
// Runs once during shutdown.
void saveWorldIfNeeded(civcraft::GameServer& server,
                        const ServerCliArgs& args,
                        const std::vector<std::shared_ptr<civcraft::WorldTemplate>>& templates) {
	if (args.worldPath.empty()) return;
	printf("[Server] Saving world to %s...\n", args.worldPath.c_str());
	civcraft::WorldMetadata meta;
	meta.name          = args.worldPath.substr(args.worldPath.rfind('/') + 1);
	meta.seed          = args.config.seed;
	meta.templateIndex = args.config.templateIndex;
	meta.gameMode      = "playing";
	meta.version       = 1;
	if (args.config.templateIndex < (int)templates.size())
		meta.templateName = templates[args.config.templateIndex]->name();
	civcraft::saveWorld(server, args.worldPath, meta);
}

}  // namespace

int main(int argc, char** argv) {
	setvbuf(stdout, nullptr, _IONBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);
	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);

	ServerCliArgs args = parseServerArgs(argc, argv);
	FILE* logFile = openServerLog(args.logPort);
	printf("=== CivCraft Dedicated Server ===\n");

	civcraft::pythonBridge().init("python");

	auto templates = buildWorldTemplates();
	if (args.interactive && isatty(fileno(stdin)))
		interactiveWorldSelect(args.config, args.worldPath, templates);

	civcraft::GameServer server;
	initServerFromArgs(server, args, templates);
	applyArtifactData(server);

	civcraft::net::TcpServer listener;
	if (!listener.listen(args.config.port)) {
		printf("[Server] Failed to start listener on port %d\n", args.config.port);
		return 1;
	}

	printf("[Server] Listening on port %d (seed=%d, template=%d)\n",
	       args.config.port, args.config.seed, args.config.templateIndex);
	printf("[Server] Press Ctrl+C to save and stop.\n");

	// Readiness file for launchers.
	snprintf(g_readyPath, sizeof(g_readyPath), "/tmp/civcraft_ready_%d", args.config.port);
	if (FILE* f = fopen(g_readyPath, "w")) fclose(f);

	civcraft::ClientManager clients(server);
	clients.setPort(args.config.port);
	server.setCallbacks(makeServerCallbacks(clients));

	// Main-loop pacing: how often a tick fires on the wall clock. At
	// simSpeed=1 this is 1/60s = 16.67ms. At simSpeed=4 it's 1/240s, so
	// four ticks fire per real-60Hz cycle and sim time advances 4× faster.
	// The sim dt passed into server.tick() stays ServerTuning::tickRate
	// (1/60) so physics, timers, and broadcast intervals are unchanged in
	// sim-time.
	const float TICK_RATE = civcraft::ServerTuning::tickIntervalRealSec();
	const double TICK_BUDGET_MS = TICK_RATE * 1000.0;
	auto lastTime = std::chrono::steady_clock::now();
	float accumulator = 0;
	int tickCount = 0;
	float statusTimer = 0;
#ifdef CIVCRAFT_PERF
	int slowTickCount = 0;
	double worstTickMs = 0.0;
	civcraft::GameServer::TickProfile worstProfile{};
	float perfTimer = 0.0f;
	constexpr float PERF_LOG_INTERVAL = 5.0f;

	// Worst-frame phase breakdown per perf window.
	struct FramePhases {
		double acceptMs = 0, sendChunksMs = 0, receiveMs = 0, pruneMs = 0;
		double tickMs = 0, broadcastMs = 0, announceLanMs = 0, statusLogMs = 0;
		double totalMs = 0;
	};
	FramePhases worstFrame{};
	double worstFrameMs = 0.0;
	int slowFrameCount = 0;
	constexpr double SLOW_FRAME_MS = 16.7;  // over 60Hz budget

	// Anchor for elapsed-time in the exit summary. 0 until the first client
	// reaches S_READY — recording is suppressed during world-gen / handshake
	// so the summary reflects actual gameplay, not boot cost.
	double perfSessionStart = 0.0;
	bool   perfRecording    = false;
#endif

	while (g_running) {
		auto frameStart = std::chrono::steady_clock::now();
		auto now = frameStart;
		float dt = std::chrono::duration<float>(now - lastTime).count();
		lastTime = now;
		accumulator += dt;
		statusTimer += dt;
#ifdef CIVCRAFT_PERF
		// Flip recording on the first frame after the first client is READY.
		// perfSessionStart anchors "now" so the exit summary's elapsed clock
		// starts at handshake, not process start.
		if (!perfRecording && clients.anyReady()) {
			perfRecording = true;
			perfSessionStart = std::chrono::duration<double>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			printf("[Perf] gameplay started — recording enabled\n");
			fflush(stdout);
		}
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
		// Drain async chunk gen + finalize Preparing clients.
		clients.advancePreparing();
		// Idle timeout, C_QUIT, TCP close → all converge on pruneDisconnected.
		clients.checkIdleTimeouts(dt);
		clients.pruneDisconnected();
		markPhase(fp.pruneMs);

		// Simulation time only — this is what the tick budget applies to.
		[[maybe_unused]] auto tickStart = std::chrono::steady_clock::now();
		int ticksThisFrame = 0;
		// Spiral-of-death guard: if a tick stalls (GC pause, long IO, OS
		// schedule hiccup) the accumulator balloons and the inner loop tries
		// to "catch up" by running dozens of ticks back-to-back, which
		// stalls harder and never recovers. Cap at 5 ticks of backlog;
		// anything beyond is simulation-time we willfully drop.
		const float kMaxBacklog = TICK_RATE * 5.0f;
		if (accumulator > kMaxBacklog) {
			fprintf(stdout, "[Server] accumulator %.3fs > %.3fs — dropping %.0f tick(s) of backlog\n",
			        accumulator, kMaxBacklog, (accumulator - kMaxBacklog) / TICK_RATE);
			fflush(stdout);
			accumulator = kMaxBacklog;
		}
		// Sim dt fed into server.tick() is the fixed sim-second-per-tick
		// (1/60), *not* the wall-clock pacing TICK_RATE. At simSpeed>1
		// ticks fire more often on the wall clock but each still advances
		// 1/60 sim-seconds, so physics, timers, and broadcast cadences are
		// identical in sim-time.
		const float SIM_DT = civcraft::ServerTuning::tickRate;
		while (accumulator >= TICK_RATE) {
			// Log + advance on exception so one bad tick doesn't silently
			// kill the server or hot-spin on a permanently broken tick.
			try {
				server.tick(SIM_DT);
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
					// Captures last tick of a frame; meaningful only when
					// ticksThisFrame==1 (the common case we care about).
					worstProfile = server.lastTickProfile();
				}
			}
			if (perfRecording) {
				// Session-long histograms. Record per-tick total + each
				// TickProfile phase so the exit summary can report p50/p95/p99.
				const auto& p = server.lastTickProfile();
				PERF_RECORD_MS("server.tick.total_ms",       perTickMs);
				PERF_RECORD_MS("server.tick.resolve_ms",     p.resolveActionsMs);
				PERF_RECORD_MS("server.tick.nav_ms",         p.navigationMs);
				PERF_RECORD_MS("server.tick.physics_ms",     p.physicsMs);
				PERF_RECORD_MS("server.tick.yaw_ms",         p.yawSmoothMs);
				PERF_RECORD_MS("server.tick.blocks_ms",      p.activeBlocksMs);
				PERF_RECORD_MS("server.tick.hpregen_ms",     p.hpRegenMs);
				PERF_RECORD_MS("server.tick.structregen_ms", p.structureRegenMs);
				PERF_RECORD_MS("server.tick.structfeat_ms",  p.structureFeaturesMs);
				PERF_RECORD_MS("server.tick.stuck_ms",       p.stuckDetectionMs);
				PERF_COUNT("server.ticks.total");
				if (perTickMs > TICK_BUDGET_MS) PERF_COUNT("server.ticks.over_budget");
			}
		}
#endif

		clients.broadcastState(dt);
		// Any low-pri block batches the worker finished this cycle go out here,
		// after high-pri S_BLOCKs have already hit the wire during tick().
		clients.drainLowPriBroadcasts();
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

		if (perfRecording) {
			// Session-long frame-phase histograms. Covers the outer server loop —
			// network I/O, accept, broadcast — not just the simulation tick.
			PERF_RECORD_MS("server.frame.total_ms",     fp.totalMs);
			PERF_RECORD_MS("server.frame.accept_ms",    fp.acceptMs);
			PERF_RECORD_MS("server.frame.sendchunks_ms",fp.sendChunksMs);
			PERF_RECORD_MS("server.frame.receive_ms",   fp.receiveMs);
			PERF_RECORD_MS("server.frame.prune_ms",     fp.pruneMs);
			PERF_RECORD_MS("server.frame.tick_ms",      fp.tickMs);
			PERF_RECORD_MS("server.frame.broadcast_ms", fp.broadcastMs);
			PERF_RECORD_MS("server.frame.lan_ms",       fp.announceLanMs);
			PERF_RECORD_MS("server.frame.statuslog_ms", fp.statusLogMs);
			PERF_COUNT("server.frames.total");
			if (fp.totalMs > SLOW_FRAME_MS) PERF_COUNT("server.frames.slow_16ms");
			if (fp.totalMs > 33.3)          PERF_COUNT("server.frames.dropped_33ms");
		}

		if (perfTimer >= PERF_LOG_INTERVAL) {
			// Silent when healthy; on breach: "count, worst, top phase".
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
						{"structfeat", worstProfile.structureFeaturesMs},
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

	// Disconnect FIRST so removeClient snapshots owned NPCs into
	// savedOwnedEntities; otherwise owned_entities.bin would be empty and
	// entities.bin would carry orphaned mobs with stale owner ids.
	clients.disconnectAll();

	saveWorldIfNeeded(server, args, templates);

	listener.shutdown();
	if (logFile) fclose(logFile);

	// Let AgentManager::findFreePort() reuse this port.
	if (g_readyPath[0]) std::remove(g_readyPath);

#ifdef CIVCRAFT_PERF
	// End-of-session perf dump. Structured block to stderr + a timestamped
	// file so `make game` can surface the summary after the client exits.
	// Skip entirely if no client ever reached READY — nothing was recorded,
	// and printing a zero-sample block is noisy.
	if (perfRecording) {
		double elapsed = std::chrono::duration<double>(
			std::chrono::steady_clock::now().time_since_epoch()).count()
			- perfSessionStart;
		std::string summary = civcraft::perf::formatSummary(
			"CIVCRAFT SERVER PERF", elapsed);

		// Highlight the top-p99 tick phase so the bottleneck is obvious.
		auto [topName, topV] = civcraft::perf::topByP99({
			"server.tick.resolve_ms",
			"server.tick.nav_ms",
			"server.tick.physics_ms",
			"server.tick.yaw_ms",
			"server.tick.blocks_ms",
			"server.tick.hpregen_ms",
			"server.tick.structregen_ms",
			"server.tick.structfeat_ms",
			"server.tick.stuck_ms",
		});
		char blline[128] = "";
		if (!topName.empty()) {
			std::snprintf(blline, sizeof(blline),
				"── bottleneck (highest p99): %s = %.2f ms\n",
				topName.c_str(), topV);
		}

		std::fprintf(stderr, "\n%s%s\n", summary.c_str(), blline);

		char ts[32];
		std::time_t tt = std::time(nullptr);
		std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", std::localtime(&tt));
		char path[96];
		std::snprintf(path, sizeof(path), "/tmp/civcraft_perf_server_%s.txt", ts);
		if (FILE* f = std::fopen(path, "w")) {
			std::fputs(summary.c_str(), f);
			std::fputs(blline, f);
			std::fclose(f);
			std::fprintf(stderr, "[Perf] summary written to %s\n", path);
		}
	}
#endif

	printf("[Server] Shut down.\n");
	civcraft::pythonBridge().shutdown();
	return 0;
}

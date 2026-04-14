// evocraft-server entry point — M1 (handshake + heartbeat).
//
// Usage:
//   evocraft-server [--port N] [--name STR]
//
// Opens a TCP listener, broadcasts an S_TICK every second to every connected
// client. The Godot client (src/EvoCraft/godot/) connects and prints both
// S_HELLO and S_TICK to stdout + /tmp/evocraft_client.log.

#include "net_protocol.h"
#include "net_server.h"
#include "python_bridge.h"
#include "sim.h"
#include "swim_slab.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {

std::atomic<bool> g_stop{false};

void onSignal(int) { g_stop.store(true); }

struct Args {
	uint16_t    port    = 7888;
	std::string name    = "evocraft-server M1";
	uint32_t    seedDna = 0;  // --seed-dna N: pre-fill player DNA for editor testing
};

void usage() {
	std::puts(
		"evocraft-server [--port N] [--name STR]\n"
		"  --port N     TCP port to listen on (default 7888)\n"
		"  --name STR   server display name sent in S_HELLO"
	);
}

Args parseArgs(int argc, char** argv) {
	Args a;
	for (int i = 1; i < argc; ++i) {
		std::string_view s = argv[i];
		if (s == "--port" && i + 1 < argc) {
			a.port = (uint16_t)std::atoi(argv[++i]);
		} else if (s == "--name" && i + 1 < argc) {
			a.name = argv[++i];
		} else if (s == "--seed-dna" && i + 1 < argc) {
			a.seedDna = (uint32_t)std::atoi(argv[++i]);
		} else if (s == "--help" || s == "-h") {
			usage();
			std::exit(0);
		} else {
			std::fprintf(stderr, "[evocraft-server] unknown arg: %s\n",
				argv[i]);
			usage();
			std::exit(2);
		}
	}
	return a;
}

} // namespace

int main(int argc, char** argv) {
	std::signal(SIGINT,  onSignal);
	std::signal(SIGTERM, onSignal);
	std::signal(SIGPIPE, SIG_IGN);

	Args args = parseArgs(argc, argv);
	evocraft::NetServer net(args.name);
	if (!net.listen(args.port)) return 1;

	evocraft::Sim sim;
	evocraft::PythonBridge bridge;
	// Microbe species — names map to Python files under
	// evocraft_artifacts/species/. The .py modules still expose decide_batch();
	// behavior intent (seek food / flee predators / wander) is unchanged for
	// the Spore Cell Stage pivot, only the visual archetype shifts.
	bridge.registerSpecies(0, "base:amoeba",     "seeker");
	bridge.registerSpecies(1, "base:ciliate",    "dodger");
	bridge.registerSpecies(2, "base:flagellate", "wanderer");

	evocraft::SwimSlab slab;
	slab.populate(60);  // 20 per NPC species + 1 player cell at id=PLAYER_CELL_ID
	if (args.seedDna > 0) {
		slab.grantPlayerDna(args.seedDna);
		std::fprintf(stderr,
			"[evocraft-server] seeded player with %u DNA (--seed-dna)\n",
			args.seedDna);
	}

	// Player input arrives at the client's send cadence (typ. 60Hz). We
	// store the latest (vx,vz) and apply it at each sim step.
	net.onPlayerInput([&slab](float vx, float vz) {
		slab.setPlayerInput(vx, vz);
	});
	net.onBuyPart([&slab](uint8_t kind, float angle, float distance) {
		bool ok = slab.tryBuyPart(kind, angle, distance);
		std::fprintf(stderr,
			"[evocraft-server] BUY_PART kind=%u angle=%.2f dist=%.2f -> %s\n",
			(unsigned)kind, angle, distance, ok ? "ok" : "denied");
	});
	net.onResetParts([&slab]() {
		slab.resetParts();
		std::fprintf(stderr, "[evocraft-server] RESET_PARTS (refunded)\n");
	});

	using clock = std::chrono::steady_clock;
	auto prev = clock::now();

	std::fprintf(stderr,
		"[evocraft-server] Spore — 30Hz petri-dish sim, 15Hz cell broadcast, "
		"proto=%u name=\"%s\" cells=%zu\n",
		(unsigned)evocraft::net::PROTO_VERSION, args.name.c_str(),
		slab.cellCount());

	// Low-frequency tick log so stderr doesn't flood at 30Hz.
	int logEvery = 30;

	while (!g_stop.load()) {
		net.poll(/*timeoutMs=*/5);

		auto now = clock::now();
		float dt = std::chrono::duration<float>(now - prev).count();
		prev = now;

		if (sim.advance(dt)) {
			slab.step(1.0f / 30.0f, &bridge);

			// Heartbeat every 30 ticks (1Hz) for protocol-keepalive observers.
			if (sim.tick() % 30 == 0) {
				net.broadcast(
					evocraft::net::build_s_tick(sim.tick(), sim.simTime()));
			}

			// Cell snapshot every 2 ticks → 15Hz.
			if (sim.tick() % 2 == 0) {
				net.broadcast(evocraft::net::build_s_cells(
					sim.tick(), slab.cellSnapshot()));
			}

			// Food snapshot every 10 ticks → 3Hz (changes rarely).
			if (sim.tick() % 10 == 0) {
				net.broadcast(evocraft::net::build_s_food(
					sim.tick(), slab.foodSnapshot()));
			}

			// Player stats every 2 ticks → 15Hz (matches cell snapshot
			// cadence so HP bar updates in lockstep with damage).
			if (sim.tick() % 2 == 0) {
				auto [hp, maxHp] = slab.playerHp();
				net.broadcast(evocraft::net::build_s_player_stats(
					hp, maxHp, slab.playerDna()));
			}

			// Parts snapshot — only on change. consumePartsDirty() returns
			// true exactly once per modification so bandwidth stays at zero
			// while the player isn't editing.
			if (slab.consumePartsDirty()) {
				net.broadcast(evocraft::net::build_s_player_parts(
					slab.playerParts()));
			}

			if ((int)(sim.tick() % logEvery) == 0) {
				std::fprintf(stderr,
					"[evocraft-server] tick=%llu cells=%zu food=%zu clients=%zu\n",
					(unsigned long long)sim.tick(), slab.cellCount(),
					slab.foodCount(), net.clientCount());
			}
		}
	}

	std::fprintf(stderr, "[evocraft-server] shutting down\n");
	net.shutdown();
	return 0;
}

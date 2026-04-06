/**
 * Agent client — headless AI process that controls a single entity.
 *
 * Connects to a running GameServer via TCP, receives world state,
 * runs Python behavior logic, and sends ActionProposals back.
 *
 * Usage:
 *   ./modcraft-agent --host 127.0.0.1 --port 7777 --entity 5
 */

#include "agent/agent_client.h"
#include "server/python_bridge.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
#include <csignal>

static volatile bool g_running = true;
static void signalHandler(int) { g_running = false; }

int main(int argc, char** argv) {
	setvbuf(stdout, nullptr, _IONBF, 0);

	std::string host = "127.0.0.1";
	int port = 7777;
	uint32_t entityId = 0;
	std::string name = "agent";

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--host") == 0 && i + 1 < argc)
			host = argv[++i];
		else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
			port = atoi(argv[++i]);
		else if (strcmp(argv[i], "--entity") == 0 && i + 1 < argc)
			entityId = (uint32_t)atoi(argv[++i]);
		else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc)
			name = argv[++i];
		else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("modcraft-agent — headless AI client\n\n"
			       "Usage: %s [options]\n"
			       "  --host HOST       Server address (default: 127.0.0.1)\n"
			       "  --port PORT       Server port (default: 7777)\n"
			       "  --entity ID       Entity ID to control\n"
			       "  --name NAME       Agent display name (default: agent)\n"
			       "  --help, -h        Show this help\n", argv[0]);
			return 0;
		}
	}

	if (entityId == 0) {
		printf("[Agent] Error: --entity ID is required\n");
		return 1;
	}

	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);

	// Initialize Python interpreter for behavior execution
	modcraft::pythonBridge().init("python");

	modcraft::AgentClient agent;
	agent.setTargetEntity(entityId);
	if (!agent.connect(host, port, name)) {
		modcraft::pythonBridge().shutdown();
		return 1;
	}

	printf("[Agent:%s] Running AI for entity %u on %s:%d\n",
		name.c_str(), entityId, host.c_str(), port);

	// Fixed-timestep main loop (50 Hz, matching server tick rate)
	const float TICK_RATE = 0.02f;
	auto lastTime = std::chrono::steady_clock::now();
	float accumulator = 0;

	while (g_running && agent.isConnected()) {
		auto now = std::chrono::steady_clock::now();
		float elapsed = std::chrono::duration<float>(now - lastTime).count();
		lastTime = now;
		accumulator += elapsed;

		// Cap to prevent spiral of death
		if (accumulator > 0.2f) accumulator = 0.2f;

		while (accumulator >= TICK_RATE) {
			agent.tick(TICK_RATE);
			accumulator -= TICK_RATE;
		}

		// Sleep to avoid spinning at 100% CPU
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	printf("[Agent:%s] Shutting down\n", name.c_str());
	agent.disconnect();
	modcraft::pythonBridge().shutdown();
	return 0;
}

/**
 * Network client — full game experience connecting to a remote server.
 *
 * Has the complete menu, character select, world rendering.
 * When entering a game, connects to the server via TCP instead of
 * creating a local GameServer.
 *
 * Usage: ./agentworld-client [--host HOST] [--port PORT]
 *        Default: 127.0.0.1:7777
 */

#include "game/game.h"
#include "server/python_bridge.h"

int main(int argc, char** argv) {
	agentworld::pythonBridge().init("python");

	agentworld::Game game;
	if (!game.init(argc, argv)) return 1;
	game.run();
	game.shutdown();

	agentworld::pythonBridge().shutdown();
	return 0;
}

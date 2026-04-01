#include "game/game.h"
#include "server/python_bridge.h"

int main(int argc, char** argv) {
	aicraft::pythonBridge().init("python");

	aicraft::Game game;
	if (!game.init(argc, argv)) return 1;
	game.run();
	game.shutdown();

	aicraft::pythonBridge().shutdown();
	return 0;
}

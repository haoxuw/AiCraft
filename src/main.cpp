#include "game/game.h"

int main(int argc, char** argv) {
	aicraft::Game game;
	if (!game.init(argc, argv)) return 1;
	game.run();
	game.shutdown();
	return 0;
}

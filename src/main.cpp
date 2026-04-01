#include "game/game.h"
#include "server/python_bridge.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

int main(int argc, char** argv) {
	setvbuf(stdout, nullptr, _IONBF, 0); // unbuffered stdout
#ifdef __EMSCRIPTEN__
	// Ensure canvas gets keyboard focus on click
	EM_ASM({
		var canvas = document.getElementById('canvas');
		canvas.addEventListener('click', function() {
			canvas.focus();
		});
		canvas.setAttribute('tabindex', '0');
		canvas.focus();
	});
#endif

	aicraft::pythonBridge().init("python");

	aicraft::Game game;
	if (!game.init(argc, argv)) return 1;
	game.run();
	game.shutdown();

	aicraft::pythonBridge().shutdown();
	return 0;
}

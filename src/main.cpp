#include "game/game.h"
#include "server/python_bridge.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

int main(int argc, char** argv) {
	setvbuf(stdout, nullptr, _IONBF, 0); // unbuffered stdout
#ifdef __EMSCRIPTEN__
	// Web: set up canvas for keyboard input and pointer lock
	EM_ASM({
		var canvas = document.getElementById('canvas');
		canvas.setAttribute('tabindex', '0');
		canvas.focus();

		// Click on canvas: focus + request pointer lock for mouse capture
		canvas.addEventListener('click', function() {
			canvas.focus();
			if (!document.pointerLockElement) {
				canvas.requestPointerLock();
			}
		});

		// Re-focus on any key press (in case focus was lost)
		document.addEventListener('keydown', function(e) {
			if (document.activeElement !== canvas) {
				canvas.focus();
			}
		});
	});
#endif

	agentworld::pythonBridge().init("python");

	agentworld::Game game;
	if (!game.init(argc, argv)) return 1;
	game.run();
	game.shutdown();

	agentworld::pythonBridge().shutdown();
	return 0;
}

#include "client/game.h"
#include "client/game_logger.h"
#include "server/python_bridge.h"
#include <cstring>
#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

int main(int argc, char** argv) {
	setvbuf(stdout, nullptr, _IONBF, 0); // unbuffered stdout

	bool logOnly = false;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("ModCraft — voxel game (singleplayer)\n\n"
			       "Usage: %s [options]\n"
			       "  --skip-menu       Start game directly (skip menu)\n"
			       "  --log-only        Headless: no window, stream WoW-style events\n"
			       "                    to stdout and /tmp/modcraft_game.log\n"
			       "  --host HOST       Connect to server instead of local\n"
			       "  --port PORT       Server port (default 7777)\n"
			       "  --help, -h        Show this help\n", argv[0]);
			return 0;
		}
		if (strcmp(argv[i], "--log-only") == 0) logOnly = true;
	}

	// Set up the persistent event log BEFORE any subsystem might want to emit.
	// Every session truncates /tmp/modcraft_game.log (prior session → .prev).
	modcraft::GameLogger::instance().init(/*echoStdout=*/logOnly);
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

	modcraft::pythonBridge().init("python");

	modcraft::Game game;
	if (!game.init(argc, argv)) return 1;
	game.run();
	game.shutdown();

	modcraft::pythonBridge().shutdown();
	return 0;
}

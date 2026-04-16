#include "client/game.h"
#include "client/game_logger.h"
#include "server/python_bridge.h"
#include <cstring>
#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include <csignal>
#include <execinfo.h>

static void crashHandler(int sig) {
	fprintf(stderr, "\n[CRASH] Signal %d (%s)\n", sig, strsignal(sig));
	void* frames[32];
	int n = backtrace(frames, 32);
	backtrace_symbols_fd(frames, n, 2);
	FILE* f = fopen("/tmp/civcraft_crash.log", "w");
	if (f) {
		fprintf(f, "Signal %d (%s)\n", sig, strsignal(sig));
		char** syms = backtrace_symbols(frames, n);
		if (syms) {
			for (int i = 0; i < n; i++) fprintf(f, "  %s\n", syms[i]);
			free(syms);
		}
		fclose(f);
		fprintf(stderr, "[CRASH] Backtrace written to /tmp/civcraft_crash.log\n");
	}
	_exit(1);
}
#endif

int main(int argc, char** argv) {
	setvbuf(stdout, nullptr, _IONBF, 0); // unbuffered stdout
	setvbuf(stderr, nullptr, _IONBF, 0);

#ifndef __EMSCRIPTEN__
	signal(SIGSEGV, crashHandler);
	signal(SIGABRT, crashHandler);
#endif

	bool logOnly = false;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("CivCraft — voxel game\n\n"
			       "Usage: %s [options]\n"
			       "  --skip-menu       Start game directly (skip menu)\n"
			       "  --log-only        Headless: no window, stream WoW-style events\n"
			       "                    to stdout and /tmp/civcraft_game.log\n"
			       "  --host HOST       Connect to remote server\n"
			       "  --port PORT       Server port (default 7777)\n"
			       "  --help, -h        Show this help\n", argv[0]);
			return 0;
		}
		if (strcmp(argv[i], "--log-only") == 0) logOnly = true;
	}

	// Set up the persistent event log BEFORE any subsystem might want to emit.
	// Every session truncates /tmp/civcraft_game.log (prior session → .prev).
	civcraft::GameLogger::instance().init(/*echoStdout=*/logOnly);
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

	civcraft::pythonBridge().init("python");

	civcraft::Game game;
	if (!game.init(argc, argv)) return 1;
	game.run();
	game.shutdown();

	civcraft::pythonBridge().shutdown();
	return 0;
}

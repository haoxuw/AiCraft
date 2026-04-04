/**
 * Network client — full game experience connecting to a remote server.
 *
 * Usage: ./agentworld-client [--host HOST] [--port PORT]
 *        Default: 127.0.0.1:7777
 */

#include "client/game.h"
#include "server/python_bridge.h"
#include <csignal>
#include <cstdio>
#include <execinfo.h>

// Crash handler: dump backtrace to file + stderr on segfault
static void crashHandler(int sig) {
	fprintf(stderr, "\n[CRASH] Signal %d (%s)\n", sig, strsignal(sig));

	void* frames[32];
	int n = backtrace(frames, 32);
	backtrace_symbols_fd(frames, n, 2); // dump to stderr

	// Also write to log file
	FILE* f = fopen("/tmp/agentworld_crash.log", "w");
	if (f) {
		fprintf(f, "Signal %d (%s)\n", sig, strsignal(sig));
		char** syms = backtrace_symbols(frames, n);
		if (syms) {
			for (int i = 0; i < n; i++) fprintf(f, "  %s\n", syms[i]);
			free(syms);
		}
		fclose(f);
		fprintf(stderr, "[CRASH] Backtrace written to /tmp/agentworld_crash.log\n");
	}

	_exit(1);
}

int main(int argc, char** argv) {
	setvbuf(stdout, nullptr, _IONBF, 0); // unbuffered stdout for crash debugging
	setvbuf(stderr, nullptr, _IONBF, 0);

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("AgentWorld — network client\n\n"
			       "Usage: %s [options]\n"
			       "  --host HOST       Server address (default 127.0.0.1)\n"
			       "  --port PORT       Server port (default 7777)\n"
			       "  --help, -h        Show this help\n", argv[0]);
			return 0;
		}
	}

	signal(SIGSEGV, crashHandler);
	signal(SIGABRT, crashHandler);

	agentworld::pythonBridge().init("python");

	agentworld::Game game;
	if (!game.init(argc, argv)) return 1;
	game.run();
	game.shutdown();

	agentworld::pythonBridge().shutdown();
	return 0;
}

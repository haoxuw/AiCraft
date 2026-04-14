// LifeCraft — entry point. Wires argv into AppOptions and runs the
// state machine defined in client/app.{h,cpp}.
//
// Controls & features: see client/app.cpp and docs/00_OVERVIEW.md.

#include "LifeCraft/client/app.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
	civcraft::lifecraft::AppOptions opts;

	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--autotest")) {
			opts.autotest = true;
		} else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) {
			opts.seed = (uint32_t)std::strtoul(argv[++i], nullptr, 10);
		} else if (!std::strcmp(argv[i], "--autotest-seconds") && i + 1 < argc) {
			opts.autotest_seconds = std::atoi(argv[++i]);
		} else if (!std::strcmp(argv[i], "--play-screenshot") && i + 1 < argc) {
			opts.play_screenshot_path = argv[++i];
		} else if (!std::strcmp(argv[i], "--help")) {
			std::printf("lifecraft options:\n"
			            "  --autotest             accelerated headless match, logs to /tmp/lifecraft_autotest.log\n"
			            "  --seed N               RNG seed\n"
			            "  --autotest-seconds N   autotest duration (default 30)\n"
			            "  --play-screenshot PATH render PLAYING for ~1s then dump PPM\n");
			return 0;
		}
	}

	civcraft::lifecraft::App app;
	if (!app.init(opts)) {
		std::fprintf(stderr, "LifeCraft init failed\n");
		return 1;
	}
	app.run();
	app.shutdown();
	return 0;
}

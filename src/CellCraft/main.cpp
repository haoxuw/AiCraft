// CellCraft — entry point. Wires argv into AppOptions and runs the
// state machine defined in client/app.{h,cpp}.

#include "CellCraft/client/app.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <glm/glm.hpp>

int main(int argc, char** argv) {
	civcraft::cellcraft::AppOptions opts;

	auto match = [&](const char* s, const char* a, const char* b = nullptr) {
		return !std::strcmp(s, a) || (b && !std::strcmp(s, b));
	};

	for (int i = 1; i < argc; ++i) {
		const char* a = argv[i];
		if (!std::strcmp(a, "--autotest")) {
			opts.autotest = true;
		} else if (!std::strcmp(a, "--seed") && i + 1 < argc) {
			opts.seed = (uint32_t)std::strtoul(argv[++i], nullptr, 10);
		} else if (!std::strcmp(a, "--autotest-seconds") && i + 1 < argc) {
			opts.autotest_seconds = std::atoi(argv[++i]);
		} else if (!std::strcmp(a, "--play-screenshot") && i + 1 < argc) {
			opts.play_screenshot_path = argv[++i];
		} else if (!std::strcmp(a, "--menu-screenshot") && i + 1 < argc) {
			opts.menu_screenshot_path = argv[++i];
		}
		// Starter-picker screenshot — accepts --starter-screenshot,
		// --select-screenshot, and legacy --kid-starter-screenshot.
		else if ((match(a, "--starter-screenshot", "--kid-starter-screenshot")
		          || !std::strcmp(a, "--select-screenshot")) && i + 1 < argc) {
			opts.starter_screenshot_path = argv[++i];
		}
		// Lab screenshot (both --lab-screenshot and legacy --kid-lab-screenshot).
		else if (match(a, "--lab-screenshot", "--kid-lab-screenshot") && i + 1 < argc) {
			opts.lab_screenshot_path = argv[++i];
		}
		// Celebrate screenshot.
		else if (match(a, "--celebrate-screenshot", "--kid-celebrate-screenshot") && i + 1 < argc) {
			opts.celebrate_screenshot_path = argv[++i];
		}
		else if (!std::strcmp(a, "--nospeech") || !std::strcmp(a, "--kid-nospeech")) {
			opts.no_speech = true;
		}
		else if (!std::strcmp(a, "--autotest-tier") && i + 1 < argc) {
			opts.autotest_tier = std::atoi(argv[++i]);
		}
		else if (!std::strcmp(a, "--ui-kitchen-sink")) {
			opts.ui_kitchen_sink = true;
		}
		else if (!std::strcmp(a, "--help")) {
			std::printf("cellcraft options:\n"
			            "  --autotest                  accelerated headless match → /tmp/cellcraft_autotest.log\n"
			            "  --seed N                    RNG seed\n"
			            "  --autotest-seconds N        autotest duration (default 30)\n"
			            "  --play-screenshot PATH      render PLAYING for ~1s then dump PPM\n"
			            "  --menu-screenshot PATH      render MAIN_MENU then dump PPM\n"
			            "  --starter-screenshot PATH   render STARTER picker (alias: --select-screenshot)\n"
			            "  --lab-screenshot PATH       render creature lab then dump PPM\n"
			            "  --celebrate-screenshot PATH render celebration then dump PPM\n"
			            "  --nospeech                  disable speech bubbles in lab\n"
			            "  --autotest-tier N           seed player at Tier N (1-5) for bg QA\n"
		            "  --ui-kitchen-sink           render modern UI primitives demo → /tmp/cc_ui_kitchen.ppm\n");
			return 0;
		}
	}

	civcraft::cellcraft::App app;
	if (!app.init(opts)) {
		std::fprintf(stderr, "CellCraft init failed\n");
		return 1;
	}
	app.run();
	app.shutdown();
	return 0;
}

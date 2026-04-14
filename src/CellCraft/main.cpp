// CellCraft — entry point. Wires argv into AppOptions and runs the
// state machine defined in client/app.{h,cpp}.

#include "CellCraft/client/app.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <glm/glm.hpp>

int main(int argc, char** argv) {
	civcraft::cellcraft::AppOptions opts;

	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--autotest")) {
			opts.autotest = true;
		} else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) {
			opts.seed = (uint32_t)std::strtoul(argv[++i], nullptr, 10);
		} else if (!std::strcmp(argv[i], "--autotest-seconds") && i + 1 < argc) {
			opts.autotest_seconds = std::atoi(argv[++i]);
		} else if (!std::strcmp(argv[i], "--play-screenshot") && i + 1 < argc) {
			opts.play_screenshot_path = argv[++i];
		} else if (!std::strcmp(argv[i], "--menu-screenshot") && i + 1 < argc) {
			opts.menu_screenshot_path = argv[++i];
		} else if (!std::strcmp(argv[i], "--select-screenshot") && i + 1 < argc) {
			opts.select_screenshot_path = argv[++i];
		} else if (!std::strcmp(argv[i], "--lab-screenshot") && i + 1 < argc) {
			opts.lab_screenshot_path = argv[++i];
		} else if (!std::strcmp(argv[i], "--lab-cursor") && i + 1 < argc) {
			float x = 0.0f, y = 0.0f;
			if (std::sscanf(argv[++i], "%f,%f", &x, &y) == 2) {
				opts.lab_cursor_px = glm::vec2(x, y);
			}
		} else if (!std::strcmp(argv[i], "--kid-starter-screenshot") && i + 1 < argc) {
			opts.kid_starter_screenshot_path = argv[++i];
		} else if (!std::strcmp(argv[i], "--kid-lab-screenshot") && i + 1 < argc) {
			opts.kid_lab_screenshot_path = argv[++i];
		} else if (!std::strcmp(argv[i], "--kid-celebrate-screenshot") && i + 1 < argc) {
			opts.kid_celebrate_screenshot_path = argv[++i];
		} else if (!std::strcmp(argv[i], "--kid-play-screenshot") && i + 1 < argc) {
			opts.kid_play_screenshot_path = argv[++i];
		} else if (!std::strcmp(argv[i], "--kid-autotest")) {
			opts.kid_autotest = true;
		} else if (!std::strcmp(argv[i], "--kid-nospeech")) {
			opts.kid_no_speech = true;
		} else if (!std::strcmp(argv[i], "--help")) {
			std::printf("cellcraft options:\n"
			            "  --autotest               accelerated headless match, logs to /tmp/cellcraft_autotest.log\n"
			            "  --seed N                 RNG seed\n"
			            "  --autotest-seconds N     autotest duration (default 30)\n"
			            "  --play-screenshot PATH   render PLAYING for ~1s then dump PPM\n"
			            "  --menu-screenshot PATH   render MAIN_MENU then dump PPM\n"
			            "  --select-screenshot PATH render MONSTER_SELECT then dump PPM\n"
			            "  --lab-screenshot    PATH render creature lab then dump PPM\n"
			            "  --lab-cursor    X,Y  force cursor to pixel (X,Y) for lab screenshot\n");
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

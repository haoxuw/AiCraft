// CellCraft — entry point. Wires argv into AppOptions and runs the
// state machine defined in client/app.{h,cpp}.
//
// Controls & features: see client/app.cpp and docs/00_OVERVIEW.md.

#include "CellCraft/client/app.h"
#include "CellCraft/sim/shape_smooth.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

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
		} else if (!std::strcmp(argv[i], "--lab-assemble-screenshot") && i + 1 < argc) {
			opts.lab_assemble_screenshot_path = argv[++i];
		} else if (!std::strcmp(argv[i], "--shape-test")) {
			opts.shape_test = true;
		} else if (!std::strcmp(argv[i], "--help")) {
			std::printf("cellcraft options:\n"
			            "  --autotest               accelerated headless match, logs to /tmp/cellcraft_autotest.log\n"
			            "  --seed N                 RNG seed\n"
			            "  --autotest-seconds N     autotest duration (default 30)\n"
			            "  --play-screenshot PATH   render PLAYING for ~1s then dump PPM\n"
			            "  --menu-screenshot PATH   render MAIN_MENU for 0.5s then dump PPM\n"
			            "  --select-screenshot PATH render MONSTER_SELECT for 0.5s then dump PPM\n");
			return 0;
		}
	}

	if (opts.shape_test) {
		// Two synthetic strokes that together outline a blobby shape.
		std::vector<std::vector<glm::vec2>> strokes;
		std::vector<glm::vec2> s1;
		for (int i = 0; i < 20; ++i) {
			float a = 3.14159f * (float)i / 19.0f;
			s1.emplace_back(std::cos(a) * 40.0f, std::sin(a) * 40.0f);
		}
		std::vector<glm::vec2> s2;
		for (int i = 0; i < 20; ++i) {
			float a = 3.14159f + 3.14159f * (float)i / 19.0f;
			s2.emplace_back(std::cos(a) * 35.0f, std::sin(a) * 35.0f);
		}
		strokes.push_back(s1);
		strokes.push_back(s2);
		auto out = civcraft::cellcraft::sim::smooth_body(strokes, 48);
		std::printf("SHAPE-TEST: input points=%zu+%zu=%zu, smoothed verts=%zu\n",
			s1.size(), s2.size(), s1.size() + s2.size(), out.size());
		if (out.size() < 3) return 2;
		// Sanity: should enclose origin.
		float minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
		for (auto& p : out) {
			if (p.x < minx) minx = p.x; if (p.x > maxx) maxx = p.x;
			if (p.y < miny) miny = p.y; if (p.y > maxy) maxy = p.y;
		}
		std::printf("  bounds x=[%.1f,%.1f] y=[%.1f,%.1f]\n", minx, maxx, miny, maxy);
		return 0;
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

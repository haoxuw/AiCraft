// LifeCraft client — M0 drawing MVP.
//
// Opens a window, renders a procedural chalkboard, and turns mouse-drag
// gestures into feathered chalk strokes. No physics, no networking, no
// server — just the shader beachhead. ESC exits.
//
// Controls:
//   left-click drag     draw a stroke
//   right-click         cycle chalk color
//   C                   clear the board
//   ESC                 exit

#include "LifeCraft/client/chalk_renderer.h"
#include "LifeCraft/client/chalk_stroke.h"
#include "client/window.h"
#include "client/gl.h"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace civcraft;
using namespace civcraft::lifecraft;

// NumptyPhysics-inspired palette. Index 0 is the starting color.
// Colors are chalky — we lift the original pigments toward white so they
// read on the dark board.
static const glm::vec3 PALETTE[] = {
	{0.96f, 0.96f, 0.92f},  // chalk white (default)
	{1.00f, 0.92f, 0.45f},  // chalk yellow
	{0.55f, 0.82f, 1.00f},  // chalk blue
	{0.55f, 0.95f, 0.65f},  // chalk green
	{1.00f, 0.60f, 0.65f},  // chalk pink
	{1.00f, 0.72f, 0.45f},  // chalk orange
};
static constexpr int PALETTE_SIZE = (int)(sizeof(PALETTE) / sizeof(PALETTE[0]));

// Min distance between sampled points. Mouse motion can fire at sub-pixel
// rates; without this, DP-simplify would do more work than needed.
static constexpr float SAMPLE_MIN_DIST = 2.0f;

struct App {
	Window window;
	// ChalkRenderer owns Shader objects whose destructors call glDeleteProgram.
	// Must be destroyed BEFORE window.shutdown() tears down the GL context,
	// so we keep it in a unique_ptr and reset it explicitly before shutdown.
	std::unique_ptr<ChalkRenderer> renderer;

	std::vector<ChalkStroke> strokes;
	ChalkStroke              live;   // in-progress stroke (empty when not drawing)
	bool                     drawing = false;
	int                      color_idx = 0;

	void onMouseButton(int button, int action) {
		if (button == GLFW_MOUSE_BUTTON_LEFT) {
			if (action == GLFW_PRESS) {
				double x, y; glfwGetCursorPos(window.handle(), &x, &y);
				drawing = true;
				live = ChalkStroke{};
				live.color = PALETTE[color_idx];
				live.points.push_back({(float)x, (float)y});
			} else if (action == GLFW_RELEASE) {
				if (drawing && live.points.size() >= 2) {
					live.simplify(1.0f);
					strokes.push_back(std::move(live));
				}
				live = ChalkStroke{};
				drawing = false;
			}
		} else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
			color_idx = (color_idx + 1) % PALETTE_SIZE;
			printf("color → %d\n", color_idx);
		}
	}

	void onMouseMove(double x, double y) {
		if (!drawing) return;
		glm::vec2 p{(float)x, (float)y};
		if (live.points.empty() ||
		    glm::length(p - live.points.back()) >= SAMPLE_MIN_DIST) {
			live.points.push_back(p);
		}
	}

	void onKey(int key, int action) {
		if (action != GLFW_PRESS) return;
		if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(window.handle(), 1);
		else if (key == GLFW_KEY_C) { strokes.clear(); printf("cleared\n"); }
	}
};

// GLFW callbacks bounce through the window user-pointer's App*.
static App* appFromWindow(GLFWwindow* w) {
	// The platform Window sets itself as user pointer. We stash the App on
	// the window too via glfwSetWindowUserPointer *after* init — so we
	// instead use a static. Cheap for a single-window MVP.
	(void)w; return nullptr;
}
static App* g_app = nullptr;

static void cb_mouse (GLFWwindow*, int b, int a, int) { if (g_app) g_app->onMouseButton(b, a); }
static void cb_move  (GLFWwindow*, double x, double y) { if (g_app) g_app->onMouseMove(x, y); }
static void cb_key   (GLFWwindow*, int k, int, int a, int) { if (g_app) g_app->onKey(k, a); }

// Minimal PPM writer — matches CivCraft's debug snapshot format so existing
// PPM viewers / CI scripts work unchanged.
static void writePPM(const char* path, int w, int h, const unsigned char* rgb) {
	FILE* f = fopen(path, "wb");
	if (!f) return;
	fprintf(f, "P6\n%d %d\n255\n", w, h);
	// glReadPixels returns rows bottom-up; flip.
	for (int y = h - 1; y >= 0; --y) fwrite(rgb + y * w * 3, 1, w * 3, f);
	fclose(f);
}

// Pre-populate strokes so --screenshot shows something visual without mouse.
static void addDemoStrokes(App& app, int w, int h) {
	// A wavy horizontal scribble, a circle, and a flourish — exercises the
	// shader's feathering, joints, and grit along the stroke length.
	ChalkStroke s1; s1.color = PALETTE[0];
	for (int i = 0; i <= 100; ++i) {
		float t = i / 100.0f;
		float x = 0.15f * w + t * 0.7f * w;
		float y = 0.3f * h + 40.0f * std::sin(t * 9.0f);
		s1.points.push_back({x, y});
	}
	s1.simplify();
	app.strokes.push_back(s1);

	ChalkStroke s2; s2.color = PALETTE[2];
	for (int i = 0; i <= 64; ++i) {
		float t = (float)i / 64.0f * 6.2831853f;
		s2.points.push_back({0.3f * w + 90.0f * std::cos(t), 0.7f * h + 90.0f * std::sin(t)});
	}
	s2.simplify();
	app.strokes.push_back(s2);

	ChalkStroke s3; s3.color = PALETTE[1];
	for (int i = 0; i <= 120; ++i) {
		float t = i / 120.0f;
		float ang = t * 12.0f;
		float r = 20.0f + t * 80.0f;
		s3.points.push_back({0.72f * w + r * std::cos(ang), 0.65f * h + r * std::sin(ang)});
	}
	s3.simplify();
	app.strokes.push_back(s3);
}

int main(int argc, char** argv) {
	const char* screenshot_path = nullptr;
	bool  demo       = false;
	float time_offset = 0.0f;  // --time seeds glfwGetTime() offset for QA
	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) {
			screenshot_path = argv[++i]; demo = true;
		} else if (!strcmp(argv[i], "--demo")) demo = true;
		else if (!strcmp(argv[i], "--time") && i + 1 < argc) time_offset = (float)atof(argv[++i]);
	}

	App app;
	g_app = &app;

	if (!app.window.init(1600, 900, "LifeCraft — draw")) return 1;

	// Platform Window disables the cursor by default (FPS-style capture).
	// LifeCraft is a 2D drawing game — we need the OS cursor visible.
	glfwSetInputMode(app.window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);

	glfwSetMouseButtonCallback(app.window.handle(), cb_mouse);
	glfwSetCursorPosCallback(app.window.handle(),   cb_move);
	glfwSetKeyCallback(app.window.handle(),         cb_key);

	app.renderer = std::make_unique<ChalkRenderer>();
	if (!app.renderer->init()) {
		fprintf(stderr, "renderer init failed\n");
		return 2;
	}

	if (demo) addDemoStrokes(app, app.window.width(), app.window.height());

	int frames = 0;
	double t0 = glfwGetTime();
	while (!app.window.shouldClose()) {
		app.window.pollEvents();
		int w = app.window.width(), h = app.window.height();
		float t = time_offset + (float)(glfwGetTime() - t0);
		glViewport(0, 0, w, h);
		app.renderer->drawBoard(w, h, t);
		app.renderer->drawStrokes(app.strokes, app.drawing ? &app.live : nullptr, w, h);

		if (screenshot_path && frames == 3) {
			// Wait a few frames for swap-chain to settle, then snap.
			std::vector<unsigned char> buf(w * h * 3);
			glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, buf.data());
			writePPM(screenshot_path, w, h, buf.data());
			printf("wrote %s (%dx%d)\n", screenshot_path, w, h);
			break;
		}
		app.window.swapBuffers();
		++frames;
	}

	// Tear down GL resources (shaders, VBOs) BEFORE killing the GL context.
	app.renderer->shutdown();
	app.renderer.reset();
	app.window.shutdown();
	return 0;
}

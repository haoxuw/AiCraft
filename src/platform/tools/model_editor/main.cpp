// model-editor — standalone box-model viewer / snapshot tool.
//
// Loads a Python model file (artifacts/models/**/*.py) via the same
// tokenizer-based loader the game uses, and renders it with ModelRenderer.
// No world, no server, no full client — just a GLFW window (or offscreen
// FBO for --snapshot mode) and the shared platform renderer code.
//
// Usage:
//   model-editor <model.py>
//       Interactive window. Drag to orbit. Scroll to zoom. Esc to quit.
//
//   model-editor <model.py> --snapshot <outdir> [--size NxN] [--clip NAME]
//       Write 6 PPMs (front/three_q/side/back/top/rts) to outdir and exit.
//
// Shared by ModCraft + EvoCraft: any .py file using the base BoxModel schema
// will load — EvoCraft creature models included.

#include "client/box_model.h"
#include "client/model.h"
#include "client/model_loader.h"
#include "client/shader.h"
#include "client/gl.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;
using modcraft::BoxModel;
using modcraft::AnimState;
using modcraft::ModelRenderer;
using modcraft::Shader;

// ── Camera / framing ─────────────────────────────────────────────────────

struct OrbitCamera {
	float yaw = 35.0f;      // degrees, around +Y
	float pitch = -15.0f;   // degrees
	float dist = 3.5f;      // meters from target
	glm::vec3 target{0.0f, 1.0f, 0.0f};

	glm::mat4 view() const {
		// yaw=0 looks at the model's front (-Z face), increasing yaw orbits
		// counter-clockwise viewed from above. So camera sits at -Z when yaw=0.
		float cy = std::cos(glm::radians(yaw));
		float sy = std::sin(glm::radians(yaw));
		float cp = std::cos(glm::radians(pitch));
		float sp = std::sin(glm::radians(pitch));
		glm::vec3 offset{dist * cp * sy, dist * sp, -dist * cp * cy};
		return glm::lookAt(target + offset, target, glm::vec3(0, 1, 0));
	}
};

static void frameModel(OrbitCamera& cam, const BoxModel& m) {
	// Fit target + distance so the whole model is on-screen. Scan AABB.
	float s = m.modelScale;
	glm::vec3 lo(1e9f), hi(-1e9f);
	for (auto& p : m.parts) {
		glm::vec3 c = p.offset * s;
		glm::vec3 h = p.halfSize * s;
		lo = glm::min(lo, c - h);
		hi = glm::max(hi, c + h);
	}
	glm::vec3 center = 0.5f * (lo + hi);
	glm::vec3 extent = hi - lo;
	float radius = 0.5f * std::max({extent.x, extent.y, extent.z});
	cam.target = glm::vec3(0.0f, center.y, 0.0f);
	cam.dist = std::max(1.0f, radius * 3.2f);
}

// ── PPM writer ───────────────────────────────────────────────────────────

static bool writePPM(const std::string& path, int w, int h,
                     const std::vector<unsigned char>& rgba) {
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f) return false;
	std::fprintf(f, "P6\n%d %d\n255\n", w, h);
	// rgba is bottom-up (glReadPixels). Flip vertically and drop alpha.
	std::vector<unsigned char> row(w * 3);
	for (int y = h - 1; y >= 0; --y) {
		const unsigned char* src = rgba.data() + y * w * 4;
		for (int x = 0; x < w; ++x) {
			row[x * 3 + 0] = src[x * 4 + 0];
			row[x * 3 + 1] = src[x * 4 + 1];
			row[x * 3 + 2] = src[x * 4 + 2];
		}
		std::fwrite(row.data(), 1, row.size(), f);
	}
	std::fclose(f);
	return true;
}

// ── Snapshot: offscreen FBO → 6 PPMs ─────────────────────────────────────

struct SnapAngle {
	const char* suffix;
	float yaw;      // degrees
	float pitch;
	float distMul;  // relative to framed radius
};

static const SnapAngle kAngles[] = {
	{"front",   0.0f,   -10.0f, 1.0f},
	{"three_q", 35.0f,  -15.0f, 1.0f},
	{"side",    90.0f,  -10.0f, 1.0f},
	{"back",    180.0f, -10.0f, 1.0f},
	{"top",     0.0f,   -85.0f, 1.2f},
	{"rts",     45.0f,  -55.0f, 1.4f},
};

static int runSnapshot(const std::string& modelPath, const std::string& outDir,
                       int width, int height, const std::string& clipName) {
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
	GLFWwindow* win = glfwCreateWindow(width, height, "model-editor-snap", nullptr, nullptr);
	if (!win) { std::fprintf(stderr, "glfwCreateWindow failed\n"); return 1; }
	glfwMakeContextCurrent(win);
	if (!gladLoadGL(glfwGetProcAddress)) { std::fprintf(stderr, "gladLoadGL failed\n"); return 1; }

	BoxModel model;
	if (!modcraft::model_loader::loadModelFile(modelPath, model)) {
		std::fprintf(stderr, "failed to load model: %s\n", modelPath.c_str());
		return 1;
	}

	Shader shader;
	if (!shader.loadFromFile("shaders/highlight.vert", "shaders/highlight.frag")) {
		std::fprintf(stderr, "failed to load shaders (cwd must contain shaders/highlight.*)\n");
		return 1;
	}

	ModelRenderer renderer;
	renderer.init(&shader);

	// Offscreen framebuffer with depth.
	GLuint fbo = 0, colorTex = 0, depthRB = 0;
	glGenFramebuffers(1, &fbo);
	glGenTextures(1, &colorTex);
	glGenRenderbuffers(1, &depthRB);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glBindTexture(GL_TEXTURE_2D, colorTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, depthRB);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRB);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		std::fprintf(stderr, "FBO incomplete\n");
		return 1;
	}

	fs::create_directories(outDir);

	OrbitCamera cam;
	frameModel(cam, model);

	AnimState anim{};
	if (!clipName.empty()) anim.currentClip = clipName;

	glm::mat4 proj = glm::perspective(glm::radians(35.0f),
	                                  float(width) / float(height),
	                                  0.1f, 100.0f);

	std::vector<unsigned char> pixels(width * height * 4);

	glViewport(0, 0, width, height);
	glEnable(GL_DEPTH_TEST);

	int idx = 1;
	for (auto& a : kAngles) {
		OrbitCamera c = cam;
		c.yaw = a.yaw;
		c.pitch = a.pitch;
		c.dist *= a.distMul;

		glClearColor(0.18f, 0.20f, 0.24f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glm::mat4 vp = proj * c.view();
		renderer.draw(model, vp, /*feetPos*/ glm::vec3(0), /*yaw*/ 0.0f, anim);

		glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
		glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		char name[256];
		std::snprintf(name, sizeof(name), "%s/debug_%d_%s.ppm", outDir.c_str(), idx, a.suffix);
		if (!writePPM(name, width, height, pixels)) {
			std::fprintf(stderr, "failed to write %s\n", name);
		} else {
			std::printf("wrote %s\n", name);
		}
		++idx;
	}

	renderer.shutdown();
	glDeleteFramebuffers(1, &fbo);
	glDeleteTextures(1, &colorTex);
	glDeleteRenderbuffers(1, &depthRB);
	glfwDestroyWindow(win);
	return 0;
}

// ── Interactive: GLFW window with orbit camera ───────────────────────────

struct InteractState {
	OrbitCamera cam;
	bool dragging = false;
	double lastX = 0, lastY = 0;
};

static void cursorPosCB(GLFWwindow* w, double x, double y) {
	auto* s = static_cast<InteractState*>(glfwGetWindowUserPointer(w));
	if (!s->dragging) { s->lastX = x; s->lastY = y; return; }
	float dx = float(x - s->lastX), dy = float(y - s->lastY);
	s->cam.yaw   -= dx * 0.4f;
	s->cam.pitch -= dy * 0.4f;
	s->cam.pitch = std::clamp(s->cam.pitch, -89.0f, 89.0f);
	s->lastX = x; s->lastY = y;
}
static void mouseBtnCB(GLFWwindow* w, int btn, int action, int /*mods*/) {
	auto* s = static_cast<InteractState*>(glfwGetWindowUserPointer(w));
	if (btn == GLFW_MOUSE_BUTTON_LEFT) {
		s->dragging = (action == GLFW_PRESS);
		glfwGetCursorPos(w, &s->lastX, &s->lastY);
	}
}
static void scrollCB(GLFWwindow* w, double /*xo*/, double yo) {
	auto* s = static_cast<InteractState*>(glfwGetWindowUserPointer(w));
	s->cam.dist *= (yo > 0 ? 0.9f : 1.1f);
	s->cam.dist = std::clamp(s->cam.dist, 0.3f, 50.0f);
}
static void keyCB(GLFWwindow* w, int key, int /*sc*/, int action, int /*mods*/) {
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) glfwSetWindowShouldClose(w, 1);
}

static int runInteractive(const std::string& modelPath, int width, int height,
                          const std::string& clipName) {
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
	GLFWwindow* win = glfwCreateWindow(width, height, "model-editor", nullptr, nullptr);
	if (!win) { std::fprintf(stderr, "glfwCreateWindow failed\n"); return 1; }
	glfwMakeContextCurrent(win);
	glfwSwapInterval(1);
	if (!gladLoadGL(glfwGetProcAddress)) { std::fprintf(stderr, "gladLoadGL failed\n"); return 1; }

	BoxModel model;
	if (!modcraft::model_loader::loadModelFile(modelPath, model)) {
		std::fprintf(stderr, "failed to load model: %s\n", modelPath.c_str());
		return 1;
	}

	Shader shader;
	if (!shader.loadFromFile("shaders/highlight.vert", "shaders/highlight.frag")) {
		std::fprintf(stderr, "failed to load shaders\n");
		return 1;
	}
	ModelRenderer renderer;
	renderer.init(&shader);

	InteractState state;
	frameModel(state.cam, model);
	glfwSetWindowUserPointer(win, &state);
	glfwSetCursorPosCallback(win, cursorPosCB);
	glfwSetMouseButtonCallback(win, mouseBtnCB);
	glfwSetScrollCallback(win, scrollCB);
	glfwSetKeyCallback(win, keyCB);

	AnimState anim{};
	if (!clipName.empty()) anim.currentClip = clipName;

	glEnable(GL_DEPTH_TEST);
	double t0 = glfwGetTime();
	while (!glfwWindowShouldClose(win)) {
		int w, h;
		glfwGetFramebufferSize(win, &w, &h);
		glViewport(0, 0, w, h);
		glClearColor(0.18f, 0.20f, 0.24f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glm::mat4 proj = glm::perspective(glm::radians(35.0f),
		                                  float(w) / float(h ? h : 1),
		                                  0.1f, 100.0f);
		glm::mat4 vp = proj * state.cam.view();

		anim.time = float(glfwGetTime() - t0);
		renderer.draw(model, vp, glm::vec3(0), 0.0f, anim);

		glfwSwapBuffers(win);
		glfwPollEvents();
	}

	renderer.shutdown();
	glfwDestroyWindow(win);
	return 0;
}

// ── Entry ────────────────────────────────────────────────────────────────

static void usage() {
	std::fprintf(stderr,
		"Usage:\n"
		"  model-editor <model.py>                           Interactive viewer\n"
		"  model-editor <model.py> --snapshot <outdir>       Write 6 PPMs and exit\n"
		"      [--size WxH]        default 512x512\n"
		"      [--clip NAME]       play a named animation clip (dance, wave, ...)\n");
}

int main(int argc, char** argv) {
	if (argc < 2) { usage(); return 1; }
	std::string modelPath = argv[1];
	std::string outDir;
	int width = 512, height = 512;
	std::string clipName;

	for (int i = 2; i < argc; ++i) {
		std::string a = argv[i];
		if (a == "--snapshot" && i + 1 < argc) { outDir = argv[++i]; }
		else if (a == "--size" && i + 1 < argc) {
			std::string s = argv[++i];
			auto x = s.find('x');
			if (x != std::string::npos) {
				width  = std::atoi(s.substr(0, x).c_str());
				height = std::atoi(s.substr(x + 1).c_str());
			}
		}
		else if (a == "--clip" && i + 1 < argc) { clipName = argv[++i]; }
		else if (a == "-h" || a == "--help") { usage(); return 0; }
		else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); usage(); return 1; }
	}

	if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }

	int rc = outDir.empty()
		? runInteractive(modelPath, width, height, clipName)
		: runSnapshot(modelPath, outDir, width, height, clipName);

	glfwTerminate();
	return rc;
}

// LifeCraft — top-level application: window, state machine, per-state
// render + input, autotest harness. Keeps menu screens, draw lab, playing
// HUD, and endscreen in one file to avoid ping-pong cross-includes. The
// sim lives in src/LifeCraft/sim — we only CALL it here.
#pragma once

#include <memory>
#include <random>
#include <string>
#include <vector>
#include <unordered_map>

#include <glm/glm.hpp>

#include "LifeCraft/artifacts/monsters/base/prebuilt.h"
#include "LifeCraft/client/app_state.h"
#include "LifeCraft/client/chalk_renderer.h"
#include "LifeCraft/client/chalk_stroke.h"
#include "LifeCraft/client/game_log.h"
#include "LifeCraft/sim/action.h"
#include "LifeCraft/sim/sim.h"
#include "LifeCraft/sim/world.h"
#include "client/text.h"
#include "client/window.h"

struct GLFWwindow;

namespace civcraft::lifecraft {

// Autotest / debug config — set once on startup from argv.
struct AppOptions {
	bool  autotest = false;             // headless-ish accelerated match, writes /tmp/lifecraft_autotest.log
	uint32_t seed = 0xC0FFEEu;
	int   autotest_seconds = 30;
	std::string play_screenshot_path;   // if set: run PLAYING for ~1s then snap PPM + exit
};

// Simple hit-tested button (NDC coordinates, anchored bottom-left).
struct Button {
	float x, y, w, h;
	std::string label;
	bool enabled = true;
};

// Transient floating event shown bottom-left for ~2s.
struct FloatingEvent {
	std::string text;
	glm::vec3   color;
	float       t_left;  // seconds remaining
};

class App {
public:
	bool init(const AppOptions& opts);
	void run();
	void shutdown();

	// Public so file-local GLFW trampolines can dispatch into them.
	void onMouseButton(int button, int action);
	void onMouseMove(double x, double y);
	void onKey(int key, int action);

private:
	// ---- State transitions ---------------------------------------------
	void goToMainMenu();
	void goToMonsterSelect();
	void goToDrawLab();
	void startMatchWithTemplate(const monsters::MonsterTemplate& t);
	void startMatchWithCustomShape(const std::vector<glm::vec2>& local_shape,
	                               glm::vec3 color);
	void goToEndScreen(bool won);

	// ---- Per-state drawing ----------------------------------------------
	void drawLoading(float dt);
	void drawMainMenu(float dt);
	void drawMonsterSelect(float dt);
	void drawDrawLab(float dt);
	void drawPlaying(float dt);
	void drawEndScreen(float dt);

	// ---- Playing helpers -------------------------------------------------
	void stepPlaying(float dt);
	void buildPlayerAction(std::unordered_map<uint32_t, sim::ActionProposal>& actions);
	void buildAIActions   (std::unordered_map<uint32_t, sim::ActionProposal>& actions);
	void drainSimEvents();
	void drawMonsters();
	void drawFood();
	void drawHUD();
	void pushFloating(const std::string& s, glm::vec3 color);

	// ---- Utility ---------------------------------------------------------
	// NDC helpers: screen pixel (x,y top-left origin) → NDC (-1..1, y up).
	glm::vec2 pxToNDC(glm::vec2 px) const;
	glm::vec2 worldToScreen(glm::vec2 w) const;

	bool drawButton(const Button& b);                           // returns true if clicked this frame
	bool pointInButton(glm::vec2 mouse_ndc, const Button& b) const;

	void writePPM(const char* path);

	// ---- Members ---------------------------------------------------------
	AppOptions opts_;
	Window window_;
	std::unique_ptr<ChalkRenderer> renderer_;
	std::unique_ptr<TextRenderer>  text_;
	GameLog log_;

	AppState state_ = AppState::LOADING;
	float state_time_ = 0.0f;

	// shared input state
	glm::vec2 mouse_px_ = glm::vec2(-1.0f);
	bool      mouse_left_down_  = false;
	bool      mouse_right_down_ = false;
	bool      mouse_left_click_ = false;   // one-frame edge trigger
	std::vector<int> keys_pressed_this_frame_; // GLFW_KEY_* pressed since last consume

	// monster select
	std::vector<monsters::MonsterTemplate> prebuilts_;
	int hovered_tile_ = -1;

	// draw lab
	ChalkStroke lab_stroke_;
	bool        lab_drawing_ = false;
	bool        lab_core_placement_mode_ = false;
	glm::vec2   lab_core_px_ = glm::vec2(0.0f);
	bool        lab_core_placed_ = false;
	std::string lab_status_;
	glm::vec3   lab_status_color_ = glm::vec3(1.0f);
	bool        lab_valid_ = false;
	std::vector<glm::vec2> lab_validated_local_; // cached local-space shape (core subtracted)
	glm::vec3 lab_color_ = glm::vec3(0.95f, 0.95f, 0.92f);

	// playing
	sim::World world_;
	std::unique_ptr<sim::Sim> sim_;
	uint32_t player_id_ = 0;
	glm::vec2 camera_world_ = glm::vec2(0.0f);
	float match_time_ = 0.0f;
	int kills_ = 0;
	bool paused_ = false;
	std::vector<FloatingEvent> floaters_;
	bool end_won_ = false;
	float end_biomass_ = 0.0f;
	float end_time_ = 0.0f;
	int   end_kills_ = 0;
	bool ai_plays_player_ = false; // autotest: have the "player" slot hunt too
	std::mt19937 ai_rng_{0x1234u};

	// matches each AI to a fixed behavior choice made at spawn.
	struct AIState { int mode; float wander_heading; float wander_t; };
	std::unordered_map<uint32_t, AIState> ai_states_;
};

} // namespace civcraft::lifecraft

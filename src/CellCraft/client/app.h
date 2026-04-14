// CellCraft — top-level application: window, state machine, per-state
// render + input, autotest harness. The sim lives in src/CellCraft/sim —
// we only CALL it here.
#pragma once

#include <memory>
#include <random>
#include <string>
#include <vector>
#include <unordered_map>

#include <glm/glm.hpp>

#include "CellCraft/artifacts/monsters/base/prebuilt.h"
#include "CellCraft/artifacts/monsters/base/starters.h"
#include "CellCraft/client/app_state.h"
#include "CellCraft/client/chalk_renderer.h"
#include "CellCraft/client/chalk_stroke.h"
#include "CellCraft/client/creature_lab.h"
#include "CellCraft/client/game_log.h"
#include "CellCraft/client/name_generator.h"
#include "CellCraft/client/post_fx.h"
#include "CellCraft/client/ambient_particles.h"
#include "CellCraft/client/screen_shake.h"
#include "CellCraft/sim/action.h"
#include "CellCraft/sim/part.h"
#include "CellCraft/sim/sim.h"
#include "CellCraft/sim/world.h"
#include "client/text.h"
#include "client/window.h"

struct GLFWwindow;

namespace civcraft::cellcraft {

// Autotest / debug config — set once on startup from argv.
struct AppOptions {
	bool  autotest = false;             // headless accelerated match
	uint32_t seed = 0xC0FFEEu;
	int   autotest_seconds = 30;
	std::string play_screenshot_path;
	std::string menu_screenshot_path;
	std::string starter_screenshot_path;
	std::string lab_screenshot_path;
	std::string celebrate_screenshot_path;
	bool        no_speech = false;
};

// Short-lived chalk-stroke particle for bites/kills/pickups.
struct Particle {
	glm::vec2 pos_a, pos_b;
	glm::vec3 color;
	float     half_width;
	float     t_left;
	float     t_max;
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
	float       t_left;
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
	void goToStarter();
	void goToLab(monsters::StarterKind kind);
	void goToCelebrate();
	void startMatchWithTemplate(const monsters::MonsterTemplate& t);
	void startMatchFromLab();
	void goToEndScreen(bool won);

	// ---- Menu background simulation (screensaver) -----------------------
	void initMenuSim();
	void stepMenuSim(float dt);

	// ---- Per-state drawing ----------------------------------------------
	void drawLoading(float dt);
	void drawMainMenu(float dt);
	void drawStarter(float dt);
	void drawLab(float dt);
	void drawCelebrate(float dt);
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
	glm::vec2 pxToNDC(glm::vec2 px) const;
	glm::vec2 worldToScreen(glm::vec2 w) const;

	bool drawButton(const Button& b);
	bool pointInButton(glm::vec2 mouse_ndc, const Button& b) const;

	void writePPM(const char* path);

	// ---- Members ---------------------------------------------------------
	AppOptions opts_;
	Window window_;
	std::unique_ptr<ChalkRenderer> renderer_;
	std::unique_ptr<TextRenderer>  text_;
	std::unique_ptr<PostFX>        post_fx_;
	AmbientParticles               ambient_;
	ScreenShake                    shake_;
	GameLog log_;

	AppState state_ = AppState::LOADING;
	float state_time_ = 0.0f;

	// shared input state
	glm::vec2 mouse_px_ = glm::vec2(-1.0f);
	bool      mouse_left_down_  = false;
	bool      mouse_right_down_ = false;
	bool      mouse_left_click_ = false;
	bool      mouse_right_click_ = false;
	std::vector<int> keys_pressed_this_frame_;

	// prebuilt monsters (for AI opponents in matches).
	std::vector<monsters::MonsterTemplate> prebuilts_;

	// The creature lab + the name chosen for the current creature.
	CreatureLab lab_;
	std::string creature_name_;
	float    celebrate_t_ = 0.0f;

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
	bool ai_plays_player_ = false;
	std::mt19937 ai_rng_{0x1234u};

	struct AIState {
		int   mode;
		float wander_heading;
		float wander_t;
		float ai_timer;
		int   last_choice;
		float last_heading;
		float last_thrust;
		float split_cooldown;
	};
	std::unordered_map<uint32_t, AIState> ai_states_;

	std::vector<Particle> particles_;
	void emitBiteParticles(glm::vec2 world_pos, glm::vec3 color);
	void emitKillParticles(glm::vec2 world_pos, glm::vec3 color);
	void emitPickupParticles(glm::vec2 world_pos, glm::vec3 color);
	void updateAndDrawParticles(float dt);

	// Match flow.
	float pre_match_t_ = 0.0f;
	bool  pre_match_active_ = false;

	// Autotest extras.
	int   autotest_shot_idx_ = 0;
	float autotest_shot_times_[3] = {2.0f, 0.0f, 0.0f};
	bool  autotest_shot_taken_[3] = {false, false, false};
	int   autotest_bites_ = 0;
	int   autotest_pickups_ = 0;
	std::string autotest_prebuilt_id_;
};

} // namespace civcraft::cellcraft

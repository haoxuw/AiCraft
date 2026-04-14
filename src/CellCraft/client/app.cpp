// CellCraft — app implementation. See app.h.
//
// Design notes:
//  - The state machine dispatches draw + input each frame. Per-state
//    input edges (click, key press) are consumed inside that state's
//    drawX() because that's where hit-tests already live.
//  - All screen-space rendering uses the chalk ribbon renderer for
//    polylines and the platform TextRenderer for text / filled rects.
//  - Open design choices (see task prompt):
//      * If a shape is modified mid-match (GROW), stats are recomputed
//        live inside sim via scale_shape().
//      * Multi-unit command: V1 only controls player_id_. SPLIT spawns
//        are owned by the player but run AI. TODO real RTS selection.
//      * If core isn't placed in LAB, USE auto-places it at centroid.
//      * Custom monster is session-memory only; no disk persistence.

#include "CellCraft/client/app.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>

#include "CellCraft/client/part_render.h"
#include "CellCraft/sim/part.h"
#include "CellCraft/sim/part_stats.h"
#include "CellCraft/sim/shape_validate.h"
#include "CellCraft/sim/tuning.h"
#include "client/gl.h"

namespace civcraft::cellcraft {

// GLFW user pointer → App. Single-window app, so a file-local is fine.
static App* g_app = nullptr;

// Static trampolines calling into the App instance.
static void final_cb_mouse(GLFWwindow*, int b, int a, int)      { if (g_app) g_app->onMouseButton(b, a); }
static void final_cb_move (GLFWwindow*, double x, double y)     { if (g_app) g_app->onMouseMove(x, y); }
static void final_cb_key  (GLFWwindow*, int k, int, int a, int) { if (g_app) g_app->onKey(k, a); }

// ---- Input handlers ----------------------------------------------------
void App::onMouseButton(int button, int action) {
	if (button == GLFW_MOUSE_BUTTON_LEFT) {
		if (action == GLFW_PRESS)   { mouse_left_down_ = true;  mouse_left_click_ = true; }
		else                        { mouse_left_down_ = false; }
	} else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
		if (action == GLFW_PRESS) { mouse_right_down_ = true; mouse_right_click_ = true; }
		else                      { mouse_right_down_ = false; }
	}
}

void App::onMouseMove(double x, double y) {
	mouse_px_ = glm::vec2((float)x, (float)y);
}

void App::onKey(int key, int action) {
	if (action != GLFW_PRESS) return;
	keys_pressed_this_frame_.push_back(key);
	if (key == GLFW_KEY_F2) {
		// quick manual screenshot
		writePPM("/tmp/cellcraft_screenshot.ppm");
	}
}

// ========================================================================
// Init / run / shutdown
// ========================================================================

bool App::init(const AppOptions& opts) {
	opts_ = opts;
	g_app = this;

	int w = 1280, h = 800;
	if (glfwInit()) {
		if (GLFWmonitor* m = glfwGetPrimaryMonitor()) {
			if (const GLFWvidmode* vm = glfwGetVideoMode(m)) {
				w = (int)(vm->width  * 0.85f);
				h = (int)(vm->height * 0.85f);
			}
		}
	}
	if (!window_.init(w, h, "CellCraft")) return false;
	glfwSetInputMode(window_.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	glfwSetMouseButtonCallback(window_.handle(), final_cb_mouse);
	glfwSetCursorPosCallback  (window_.handle(), final_cb_move);
	glfwSetKeyCallback        (window_.handle(), final_cb_key);

	renderer_ = std::make_unique<ChalkRenderer>();
	if (!renderer_->init()) return false;
	text_ = std::make_unique<TextRenderer>();
	if (!text_->init("shaders")) return false;

	log_.open();
	prebuilts_ = monsters::getPrebuiltMonsters();
	lab_.init(&window_, renderer_.get(), text_.get());
	lab_.set_speech_enabled(!opts_.no_speech);

	if (opts_.autotest || !opts_.play_screenshot_path.empty()) {
		world_.rng.seed(opts_.seed);
		ai_rng_.seed(opts_.seed + 1);
		state_ = AppState::MAIN_MENU;  // temporary — startMatchWithTemplate will change it
		ai_plays_player_ = opts_.autotest;
		int idx = opts_.autotest ? 0 : 1;
		autotest_prebuilt_id_ = prebuilts_[idx].id;
		startMatchWithTemplate(prebuilts_[idx]);
	} else if (!opts_.menu_screenshot_path.empty()) {
		state_ = AppState::MAIN_MENU;
	} else if (!opts_.starter_screenshot_path.empty()) {
		state_ = AppState::STARTER;
	} else if (!opts_.lab_screenshot_path.empty()) {
		std::mt19937 rng(opts_.seed);
		auto t = monsters::makeStarter(monsters::StarterKind::SPIKY, rng);
		creature_name_ = generateName(rng);
		lab_.load_starter(t.cell, t.parts, t.color, creature_name_);
		state_ = AppState::LAB;
	} else if (!opts_.celebrate_screenshot_path.empty()) {
		std::mt19937 rng(opts_.seed);
		auto t = monsters::makeStarter(monsters::StarterKind::SPIKY, rng);
		creature_name_ = generateName(rng);
		lab_.load_starter(t.cell, t.parts, t.color, creature_name_);
		state_ = AppState::CELEBRATE;
		celebrate_t_ = 0.6f;
	} else {
		state_ = AppState::LOADING;
	}
	return true;
}

void App::shutdown() {
	log_.close();
	if (text_)     { text_->shutdown();     text_.reset(); }
	if (renderer_) { renderer_->shutdown(); renderer_.reset(); }
	window_.shutdown();
	g_app = nullptr;
}

void App::run() {
	// --- autotest loop: accelerated headless-style run ---
	if (opts_.autotest) {
		FILE* log = std::fopen("/tmp/cellcraft_autotest.log", "w");
		if (!log) { std::fprintf(stderr, "couldn't open autotest log\n"); return; }
		std::fprintf(log, "# CellCraft autotest  seed=%u  seconds=%d\n",
			opts_.seed, opts_.autotest_seconds);

		// Schedule screenshot times: 2s, 50%, 90% of match duration (or at death).
		autotest_shot_times_[0] = 2.0f;
		autotest_shot_times_[1] = 0.50f * (float)opts_.autotest_seconds;
		autotest_shot_times_[2] = 0.90f * (float)opts_.autotest_seconds;

		const float dt = 1.0f / 60.0f;
		int total_ticks = opts_.autotest_seconds * 60;
		float player_survived_s = 0.0f;
		std::string outcome = "TIE";

		for (int t = 0; t < total_ticks; ++t) {
			float pre_dt = dt;
			stepPlaying(pre_dt);

			// Render + shoot requested autotest frames.
			if (autotest_shot_idx_ < 3 && match_time_ >= autotest_shot_times_[autotest_shot_idx_]
			    && !autotest_shot_taken_[autotest_shot_idx_]) {
				window_.pollEvents();
				int fw, fh; glfwGetFramebufferSize(window_.handle(), &fw, &fh);
				glViewport(0, 0, fw, fh);
				renderer_->drawBoard(fw, fh, (float)glfwGetTime());
				drawFood();
				drawMonsters();
				updateAndDrawParticles(0.0f);
				drawHUD();
				window_.swapBuffers();
				char path[64];
				std::snprintf(path, sizeof(path), "/tmp/autotest_%d.ppm",
					autotest_shot_idx_ + 1);
				writePPM(path);
				autotest_shot_taken_[autotest_shot_idx_] = true;
				++autotest_shot_idx_;
			}

			if (world_.get(player_id_)) player_survived_s = match_time_;

			if (t % 60 == 0) {
				int sec = t / 60;
				std::fprintf(log, "[t=%3ds] ", sec);
				for (auto& [id, m] : world_.monsters) {
					std::fprintf(log, "#%u(hp=%.1f/bm=%.1f) ", id, m.hp, m.biomass);
				}
				std::fprintf(log, "\n");
				std::fflush(log);
			}
			if (state_ == AppState::END_SCREEN) {
				// Take remaining screenshots at death if we haven't.
				while (autotest_shot_idx_ < 3) {
					window_.pollEvents();
					int fw, fh; glfwGetFramebufferSize(window_.handle(), &fw, &fh);
					glViewport(0, 0, fw, fh);
					renderer_->drawBoard(fw, fh, (float)glfwGetTime());
					drawFood();
					drawMonsters();
					updateAndDrawParticles(0.0f);
					drawHUD();
					window_.swapBuffers();
					char path[64];
					std::snprintf(path, sizeof(path), "/tmp/autotest_%d.ppm",
						autotest_shot_idx_ + 1);
					writePPM(path);
					autotest_shot_taken_[autotest_shot_idx_] = true;
					++autotest_shot_idx_;
				}
				outcome = end_won_ ? "WIN" : "LOSE";
				std::fprintf(log, "# match ended at t=%d ticks (%.2fs)  won=%d\n",
					t, t * dt, end_won_ ? 1 : 0);
				break;
			}
		}
		if (outcome == "TIE" && world_.get(player_id_)) outcome = "WIN";
		// If we exited before END_SCREEN, snapshot player state for the summary.
		float final_bm = end_biomass_;
		if (state_ != AppState::END_SCREEN) {
			if (const sim::Monster* pm = world_.get(player_id_)) final_bm = pm->biomass;
			end_kills_ = kills_;
		}
		std::fprintf(log, "# final  biomass=%.1f  kills=%d  time=%.2f  won=%d\n",
			final_bm, end_kills_, end_time_, end_won_ ? 1 : 0);
		std::fclose(log);
		std::printf("AUTOTEST seed=%u outcome=%s player_survived=%.1fs biomass_final=%.1f kills=%d events=%d\n",
			opts_.seed, outcome.c_str(), player_survived_s,
			final_bm, end_kills_, autotest_bites_ + autotest_pickups_);
		return;
	}

	// --- play_screenshot mode: render a few frames of PLAYING, snap PPM, exit ---
	if (!opts_.play_screenshot_path.empty()) {
		double t0 = glfwGetTime();
		const double limit = 1.2;
		while (!window_.shouldClose() && (glfwGetTime() - t0) < limit) {
			window_.pollEvents();
			float dt = 1.0f / 60.0f;
			stepPlaying(dt);
			int w, h; glfwGetFramebufferSize(window_.handle(), &w, &h);
			glViewport(0, 0, w, h);
			renderer_->drawBoard(w, h, (float)(glfwGetTime() - t0));
			drawFood();
			drawMonsters();
			updateAndDrawParticles(dt);
			drawHUD();
			window_.swapBuffers();
		}
		writePPM(opts_.play_screenshot_path.c_str());
		return;
	}

	// --- one-shot screenshot modes: render a state for ~0.5s then dump PPM.
	auto run_one_shot = [&](const std::string& out, AppState st, float dur) {
		state_ = st;
		double t0 = glfwGetTime();
		while (!window_.shouldClose() && (glfwGetTime() - t0) < dur) {
			window_.pollEvents();
			float dt = 1.0f / 60.0f;
			state_time_ += dt;
			int w, h; glfwGetFramebufferSize(window_.handle(), &w, &h);
			glViewport(0, 0, w, h);
			renderer_->drawBoard(w, h, (float)glfwGetTime());
			switch (st) {
			case AppState::MAIN_MENU: drawMainMenu(dt); break;
			case AppState::STARTER:   drawStarter(dt);  break;
			case AppState::LAB:       drawLab(dt);      break;
			case AppState::CELEBRATE: drawCelebrate(dt); break;
			default: break;
			}
			mouse_left_click_ = false;
			mouse_right_click_ = false;
			keys_pressed_this_frame_.clear();
			window_.swapBuffers();
		}
		writePPM(out.c_str());
	};
	if (!opts_.starter_screenshot_path.empty()) {
		run_one_shot(opts_.starter_screenshot_path, AppState::STARTER, 0.5);
		return;
	}
	if (!opts_.lab_screenshot_path.empty()) {
		run_one_shot(opts_.lab_screenshot_path, AppState::LAB, 0.5);
		return;
	}
	if (!opts_.celebrate_screenshot_path.empty()) {
		run_one_shot(opts_.celebrate_screenshot_path, AppState::CELEBRATE, 0.5);
		return;
	}
	if (!opts_.menu_screenshot_path.empty()) {
		run_one_shot(opts_.menu_screenshot_path, AppState::MAIN_MENU, 0.5);
		return;
	}

	// --- normal interactive loop ---
	double prev = glfwGetTime();
	while (!window_.shouldClose()) {
		window_.pollEvents();
		double now = glfwGetTime();
		float dt = (float)(now - prev);
		if (dt > 0.1f) dt = 0.1f;
		prev = now;
		state_time_ += dt;

		int w, h; glfwGetFramebufferSize(window_.handle(), &w, &h);
		glViewport(0, 0, w, h);
		renderer_->drawBoard(w, h, (float)now);

		switch (state_) {
		case AppState::LOADING:    drawLoading(dt);   break;
		case AppState::MAIN_MENU:  drawMainMenu(dt);  break;
		case AppState::STARTER:    drawStarter(dt);   break;
		case AppState::LAB:        drawLab(dt);       break;
		case AppState::CELEBRATE:  drawCelebrate(dt); break;
		case AppState::PLAYING:    drawPlaying(dt);   break;
		case AppState::END_SCREEN: drawEndScreen(dt); break;
		}
		if (state_ == AppState::PLAYING) updateAndDrawParticles(dt);

		// consume edge triggers at end of frame
		mouse_left_click_ = false;
		mouse_right_click_ = false;
		keys_pressed_this_frame_.clear();

		window_.swapBuffers();
	}
}

// ========================================================================
// State transitions
// ========================================================================

void App::goToMainMenu() {
	state_ = AppState::MAIN_MENU;
	state_time_ = 0.0f;
	initMenuSim();
}
void App::startMatchWithTemplate(const monsters::MonsterTemplate& t) {
	world_ = sim::World{};
	world_.rng.seed(opts_.seed);
	world_.map_radius = 1500.0f;
	ai_states_.clear();
	floaters_.clear();
	match_time_ = 0.0f;
	kills_ = 0;
	paused_ = false;
	autotest_prebuilt_id_ = t.id;

	sim::Monster p = monsters::makeMonsterFromTemplate(t, /*owner=*/1,
		glm::vec2(0.0f, 0.0f), 0.0f);
	// Give the player a modest starting biomass bonus so they aren't one-shot
	// by the first AI they bump — tuning choice for early-game feel.
	p.biomass *= 2.0f;
	p.hp_max   = p.biomass * sim::HP_PER_BIOMASS;
	p.hp       = p.hp_max;
	player_id_ = world_.spawn_monster(std::move(p));

	// 5 AIs at pentagon; mix: 2 hunters, 2 feeders, 1 aggressive_hunter.
	float R = world_.map_radius * 0.5f;
	const int N_AI = 5;
	// mode assignment order (shuffled-ish): hunter, feeder, hunter, feeder, aggressive.
	const int modes[N_AI] = { 1, 2, 1, 2, 3 };
	for (int i = 0; i < N_AI; ++i) {
		float a = 6.28318530718f * float(i) / float(N_AI);
		glm::vec2 pos(std::cos(a) * R, std::sin(a) * R);
		const auto& tmpl = prebuilts_[i % prebuilts_.size()];
		sim::Monster m = monsters::makeMonsterFromTemplate(tmpl,
			/*owner=*/100 + i, pos, std::atan2(-pos.y, -pos.x));
		uint32_t id = world_.spawn_monster(std::move(m));
		AIState s;
		s.mode = modes[i];
		s.wander_heading = std::atan2(-pos.y, -pos.x);
		s.wander_t = 0.0f;
		s.ai_timer = 0.0f;
		s.last_choice = 0;
		s.last_heading = s.wander_heading;
		s.last_thrust = 0.3f;
		s.split_cooldown = 8.0f + (float)(ai_rng_() % 100) / 20.0f;
		ai_states_[id] = s;
	}

	world_.scatter_food(30);

	sim_ = std::make_unique<sim::Sim>(&world_);
	state_ = AppState::PLAYING;
	state_time_ = 0.0f;
	particles_.clear();
	pre_match_t_ = 0.0f;
	pre_match_active_ = !opts_.autotest; // skip countdown during autotest for deterministic timing
	autotest_shot_idx_ = 0;
	for (int i = 0; i < 3; ++i) autotest_shot_taken_[i] = false;
	autotest_bites_ = autotest_pickups_ = 0;
	log_.write("MATCH", "start — player#" + std::to_string(player_id_) +
		" shape=" + t.id);
}

void App::goToEndScreen(bool won) {
	end_won_ = won;
	end_time_ = match_time_;
	end_kills_ = kills_;
	const sim::Monster* pm = world_.get(player_id_);
	end_biomass_ = pm ? pm->biomass : 0.0f;
	state_ = AppState::END_SCREEN;
	state_time_ = 0.0f;
	log_.write("MATCH", won ? "ended: WON" : "ended: ELIMINATED");
}

// ========================================================================
// Input helpers
// ========================================================================

glm::vec2 App::pxToNDC(glm::vec2 px) const {
	int w, h; glfwGetFramebufferSize(window_.handle(), &w, &h);
	// GLFW cursor is in window-logical pixels. If FB and window differ (HiDPI)
	// we'd need scaling; for our targets they match.
	int ww, wh; glfwGetWindowSize(window_.handle(), &ww, &wh);
	float xn = (px.x / (float)ww) * 2.0f - 1.0f;
	float yn = 1.0f - (px.y / (float)wh) * 2.0f;
	(void)w;(void)h;
	return glm::vec2(xn, yn);
}

glm::vec2 App::worldToScreen(glm::vec2 wpos) const {
	int w, h; glfwGetFramebufferSize(window_.handle(), &w, &h);
	glm::vec2 rel = wpos - camera_world_;
	return glm::vec2(rel.x + w * 0.5f, -rel.y + h * 0.5f);
}

bool App::pointInButton(glm::vec2 mouse_ndc, const Button& b) const {
	return mouse_ndc.x >= b.x && mouse_ndc.x <= b.x + b.w
	    && mouse_ndc.y >= b.y && mouse_ndc.y <= b.y + b.h;
}

bool App::drawButton(const Button& b) {
	glm::vec2 m = pxToNDC(mouse_px_);
	bool hover = pointInButton(m, b) && b.enabled;
	glm::vec4 bg = b.enabled
		? (hover ? glm::vec4(0.22f, 0.22f, 0.25f, 0.9f) : glm::vec4(0.10f, 0.10f, 0.12f, 0.8f))
		: glm::vec4(0.06f, 0.06f, 0.06f, 0.7f);
	text_->drawRect(b.x, b.y, b.w, b.h, bg);
	// chalk-outlined border: draw 4 thin rects
	glm::vec4 edge(0.85f, 0.85f, 0.80f, b.enabled ? 0.95f : 0.4f);
	float t = 0.003f;
	text_->drawRect(b.x, b.y, b.w, t, edge);
	text_->drawRect(b.x, b.y + b.h - t, b.w, t, edge);
	text_->drawRect(b.x, b.y, t, b.h, edge);
	text_->drawRect(b.x + b.w - t, b.y, t, b.h, edge);
	// centered label
	float label_w = (float)b.label.size() * 0.018f * 1.1f; // approx, matches buildTextVerts
	float tx = b.x + (b.w - label_w) * 0.5f;
	float ty = b.y + (b.h - 0.032f * 1.1f) * 0.5f;
	glm::vec4 col = b.enabled ? glm::vec4(0.97f, 0.97f, 0.92f, 1.0f)
	                          : glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
	text_->drawText(b.label, tx, ty, 1.1f, col, window_.aspectRatio());
	return hover && mouse_left_click_;
}

// ========================================================================
// LOADING
// ========================================================================

void App::drawLoading(float) {
	text_->drawTitle("CELLCRAFT", -0.26f, 0.05f, 3.0f,
		glm::vec4(1.0f, 1.0f, 0.95f, 1.0f), window_.aspectRatio());

	float total = 0.8f;
	float frac = std::min(1.0f, state_time_ / total);
	float bw = 0.5f, bh = 0.02f;
	float bx = -bw * 0.5f, by = -0.12f;
	text_->drawRect(bx, by, bw, bh, glm::vec4(0.15f, 0.15f, 0.15f, 0.8f));
	text_->drawRect(bx, by, bw * frac, bh, glm::vec4(0.85f, 0.85f, 0.75f, 1.0f));
	text_->drawText("LOADING...", -0.08f, -0.2f, 0.9f,
		glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), window_.aspectRatio());

	if (state_time_ >= total) goToMainMenu();
}

// ========================================================================
// MAIN_MENU
// ========================================================================

// Spawn a handful of AI monsters with no player. They wander/graze and
// act as a live screensaver behind the main-menu panel. Reuses the same
// World/Sim as gameplay so visuals stay consistent.
void App::initMenuSim() {
	world_ = sim::World{};
	world_.rng.seed(opts_.seed ^ 0xA11CE5Du);
	world_.map_radius = 1500.0f;
	ai_states_.clear();
	floaters_.clear();
	particles_.clear();
	player_id_ = 0;           // no player in screensaver
	paused_ = false;
	pre_match_active_ = false;
	camera_world_ = glm::vec2(0.0f);

	// 8 AIs sprinkled around the camera. All mode-2 (feeders) so they graze
	// peacefully rather than hunt — low-action background. Spawn radii are
	// 1px = 1 world-unit, so 300–520 keeps them visibly on-screen around
	// the centered menu panel (which covers ~±300×±245 px at 1080p).
	const int N = 8;
	for (int i = 0; i < N; ++i) {
		float a = 6.28318530718f * float(i) / float(N);
		float r = 320.0f + 60.0f * ((i * 37 % 7) / 6.0f);
		glm::vec2 pos(std::cos(a) * r, std::sin(a) * r);
		const auto& tmpl = prebuilts_[i % prebuilts_.size()];
		sim::Monster m = monsters::makeMonsterFromTemplate(tmpl,
			/*owner=*/200 + i, pos, std::atan2(-pos.y, -pos.x));
		uint32_t id = world_.spawn_monster(std::move(m));
		AIState s;
		s.mode = 2;            // feeder
		s.wander_heading = std::atan2(-pos.y, -pos.x);
		s.wander_t = 0.0f;
		s.ai_timer = 0.0f;
		s.last_choice = 0;
		s.last_heading = s.wander_heading;
		s.last_thrust = 0.2f;
		s.split_cooldown = 1e9f;   // disable splits in the screensaver
		ai_states_[id] = s;
	}
	world_.scatter_food(60);       // lots of food, nobody starves
	sim_ = std::make_unique<sim::Sim>(&world_);

	// Warm the sim so the screen isn't static at t=0 (also helps menu
	// screenshots look alive).
	std::unordered_map<uint32_t, sim::ActionProposal> actions;
	for (int step = 0; step < 60; ++step) {
		actions.clear();
		buildAIActions(actions);
		sim_->tick(1.0f / 60.0f, actions);
	}
}

// Screensaver tick: AI-only, no player, no win/lose, no event pump.
void App::stepMenuSim(float dt) {
	if (!sim_) return;
	// Slow-mo keeps it visually calm behind the menu text.
	float sdt = std::min(dt, 1.0f / 60.0f) * 0.6f;
	std::unordered_map<uint32_t, sim::ActionProposal> actions;
	buildAIActions(actions);
	sim_->tick(sdt, actions);

	// Respawn food if it gets low — screensaver should never go barren.
	if ((int)world_.food.size() < 30) world_.scatter_food(15);

	// Slow camera drift for parallax — a gentle lissajous-ish orbit.
	float t = (float)glfwGetTime();
	camera_world_ = glm::vec2(std::sin(t * 0.05f) * 200.0f,
	                          std::cos(t * 0.037f) * 150.0f);
}

void App::drawMainMenu(float dt) {
	// Lazy init — when state_ was forced to MAIN_MENU without goToMainMenu
	// (e.g. --menu-screenshot path).
	if (!sim_ || world_.monsters.empty()) initMenuSim();

	// 1) Live screensaver behind everything.
	stepMenuSim(dt);
	drawFood();
	drawMonsters();
	updateAndDrawParticles(dt);

	float aspect = window_.aspectRatio();

	// 2) Dim overlay so menu text reads against busy sim.
	text_->drawRect(-1.0f, -1.0f, 2.0f, 2.0f, glm::vec4(0.0f, 0.0f, 0.0f, 0.45f));

	// 3) Centered panel (RA1-inspired): semi-opaque slab with double chalk
	//    border + corner ticks.
	float px = -0.34f, py = -0.48f, pw = 0.68f, ph = 0.96f;
	text_->drawRect(px, py, pw, ph, glm::vec4(0.04f, 0.05f, 0.06f, 0.88f));
	auto chalkBorder = [&](float x, float y, float w, float h, float t, glm::vec4 c) {
		text_->drawRect(x, y,         w, t, c);
		text_->drawRect(x, y + h - t, w, t, c);
		text_->drawRect(x, y,         t, h, c);
		text_->drawRect(x + w - t, y, t, h, c);
	};
	chalkBorder(px, py, pw, ph, 0.004f, glm::vec4(0.92f, 0.90f, 0.78f, 0.95f));
	chalkBorder(px + 0.012f, py + 0.012f, pw - 0.024f, ph - 0.024f, 0.002f,
		glm::vec4(0.70f, 0.68f, 0.58f, 0.8f));
	// Corner tick marks — small L-brackets outside the outer border.
	float tk = 0.03f, tt = 0.005f;
	glm::vec4 tkc(0.95f, 0.75f, 0.30f, 1.0f);   // warm amber, RA1 vibe
	text_->drawRect(px - 0.01f,       py + ph - tt,     tk, tt, tkc);
	text_->drawRect(px - 0.01f,       py + ph - tk,     tt, tk, tkc);
	text_->drawRect(px + pw + 0.01f - tk, py + ph - tt, tk, tt, tkc);
	text_->drawRect(px + pw + 0.01f - tt, py + ph - tk, tt, tk, tkc);
	text_->drawRect(px - 0.01f,       py,               tk, tt, tkc);
	text_->drawRect(px - 0.01f,       py,               tt, tk, tkc);
	text_->drawRect(px + pw + 0.01f - tk, py,           tk, tt, tkc);
	text_->drawRect(px + pw + 0.01f - tt, py,           tt, tk, tkc);

	// 4) Stenciled title — double-draw with an offset shadow.
	glm::vec4 shadow(0.95f, 0.45f, 0.10f, 0.7f);
	glm::vec4 chalk (1.0f, 1.0f, 0.95f, 1.0f);
	text_->drawTitle("CELLCRAFT", -0.26f + 0.008f, 0.30f - 0.008f, 3.0f, shadow, aspect);
	text_->drawTitle("CELLCRAFT", -0.26f,          0.30f,          3.0f, chalk,  aspect);
	text_->drawText ("// CHALKBOARD SURVIVAL //", -0.24f, 0.18f, 1.0f,
		glm::vec4(0.85f, 0.82f, 0.70f, 0.95f), aspect);

	// 5) Buttons (inside panel). Single unified flow: PLAY → starter picker.
	Button play { -0.22f,  0.05f, 0.44f, 0.08f, "PLAY" };
	Button quit { -0.22f, -0.08f, 0.44f, 0.08f, "QUIT" };

	if (drawButton(play)) goToStarter();
	if (drawButton(quit)) glfwSetWindowShouldClose(window_.handle(), 1);

	// 6) Hint line at panel bottom.
	text_->drawText("v0.1  //  CLICK OR ESC",
		-0.14f, -0.43f, 0.7f,
		glm::vec4(0.7f, 0.7f, 0.6f, 0.8f), aspect);
}


// ========================================================================
// PLAYING
// ========================================================================

void App::buildPlayerAction(std::unordered_map<uint32_t, sim::ActionProposal>& actions) {
	sim::Monster* p = world_.get(player_id_);
	if (!p) return;

	if (ai_plays_player_) {
		// Autotest player: prefer food until well-fed, then hunt nearest.
		// EYES widens the feed→hunt fallback threshold (perception_mult).
		const float perc = p->part_effect.perception_mult;
		glm::vec2 target = p->core_pos;
		float best = 1e9f;
		bool feeding = p->biomass < 50.0f;
		if (feeding) {
			for (auto& f : world_.food) {
				float d = glm::length(f.pos - p->core_pos);
				if (d < best) { best = d; target = f.pos; }
			}
		}
		if (!feeding || best > 600.0f * perc) {
			best = 1e9f;
			for (auto& [id, m] : world_.monsters) {
				if (id == player_id_ || !m.alive) continue;
				float d = glm::length(m.core_pos - p->core_pos);
				if (d < best) { best = d; target = m.core_pos; }
			}
		}
		glm::vec2 d = target - p->core_pos;
		float heading = (glm::length(d) > 1e-3f) ? std::atan2(d.y, d.x) : p->heading;
		actions[player_id_] = sim::ActionProposal::move(heading, 1.0f);
		return;
	}

	// Interactive: aim at mouse (world coord), thrust from left-click-held.
	int w, h; glfwGetFramebufferSize(window_.handle(), &w, &h);
	// Mouse px → world (camera is locked to player core).
	int ww, wh; glfwGetWindowSize(window_.handle(), &ww, &wh);
	glm::vec2 mpx_fb = glm::vec2(mouse_px_.x * (float)w / (float)ww,
	                              mouse_px_.y * (float)h / (float)wh);
	glm::vec2 mrel = glm::vec2(mpx_fb.x - w * 0.5f, -(mpx_fb.y - h * 0.5f));
	glm::vec2 world_pt = camera_world_ + mrel;
	glm::vec2 d = world_pt - p->core_pos;
	float heading = (glm::length(d) > 1e-3f) ? std::atan2(d.y, d.x) : p->heading;
	float thrust  = mouse_left_down_ ? 1.0f : 0.3f;
	actions[player_id_] = sim::ActionProposal::move(heading, thrust);

	// Handle 1=SPLIT, 2=GROW keypresses.
	for (int key : keys_pressed_this_frame_) {
		if (key == GLFW_KEY_1) {
			if (p->biomass >= 20.0f) actions[player_id_] = sim::ActionProposal::split(20.0f);
		} else if (key == GLFW_KEY_2) {
			if (p->biomass >= 15.0f) actions[player_id_] = sim::ActionProposal::grow(1.1f);
		} else if (key == GLFW_KEY_P) {
			paused_ = !paused_;
		} else if (key == GLFW_KEY_ESCAPE) {
			goToMainMenu();
			return;
		}
	}
}

void App::buildAIActions(std::unordered_map<uint32_t, sim::ActionProposal>& actions) {
	std::uniform_real_distribution<float> ang_drift(-0.4f, 0.4f);
	const float DT_FIXED = 1.0f / 60.0f;

	// Count SPLIT-descendants per owner to throttle runaway splitting.
	std::unordered_map<uint32_t, int> owner_count;
	for (auto& [oid, om] : world_.monsters) {
		if (!om.alive) continue;
		++owner_count[om.owner_id];
	}

	for (auto& [id, m] : world_.monsters) {
		if (id == player_id_) continue;
		if (!m.alive) continue;

		// Player-owned spawned units (from SPLIT): simple flocking — head toward player.
		if (m.owner_id == 1) {
			sim::Monster* p = world_.get(player_id_);
			if (p) {
				glm::vec2 d = p->core_pos - m.core_pos;
				float h = (glm::length(d) > 1e-3f) ? std::atan2(d.y, d.x) : m.heading;
				actions[id] = sim::ActionProposal::move(h, 0.7f);
			} else {
				actions[id] = sim::ActionProposal::move(m.heading, 0.2f);
			}
			continue;
		}

		auto it = ai_states_.find(id);
		if (it == ai_states_.end()) {
			AIState s; s.mode = 1; s.wander_heading = m.heading; s.wander_t = 0.0f;
			s.ai_timer = 0.0f; s.last_choice = 0; s.last_heading = m.heading;
			s.last_thrust = 0.3f; s.split_cooldown = 8.0f;
			ai_states_[id] = s;
			it = ai_states_.find(id);
		}
		AIState& s = it->second;

		s.ai_timer -= DT_FIXED;
		s.split_cooldown -= DT_FIXED;

		// Occasional SPLIT when biomass high. Cap units per owner for perf.
		if (s.split_cooldown <= 0.0f && m.biomass > 60.0f
		    && owner_count[m.owner_id] < 3) {
			actions[id] = sim::ActionProposal::split(20.0f);
			s.split_cooldown = 10.0f;
			continue;
		}

		// Refresh high-level decision every 0.25s; otherwise reuse cached.
		if (s.ai_timer <= 0.0f) {
			s.ai_timer = 0.25f;

			// Survey nearby: nearest threat (bigger), nearest prey (smaller or ignored),
			// nearest food.
			float best_prey_d = 1e9f;
			glm::vec2 prey_pos(0.0f);
			bool have_prey = false;
			float nearest_threat_d = 1e9f;
			glm::vec2 threat_pos(0.0f);
			bool have_threat = false;
			float best_food_d = 1e9f;
			glm::vec2 food_pos(0.0f);
			bool have_food = false;

			// EYES widens hunt/flee/feed search radii (perception_mult).
			const float perc = m.part_effect.perception_mult;
			const float threat_r = 250.0f * perc;
			const float hunt_r   = 600.0f * perc;
			const float food_r   = 300.0f * perc;

			for (auto& [oid, om] : world_.monsters) {
				if (oid == id || !om.alive) continue;
				if (om.owner_id == m.owner_id) continue;
				float d = glm::length(om.core_pos - m.core_pos);
				float ratio = om.biomass / std::max(1.0f, m.biomass);
				// Threat: > 1.3× our biomass within threat_r.
				if (d < threat_r && ratio > 1.3f && d < nearest_threat_d) {
					nearest_threat_d = d; threat_pos = om.core_pos; have_threat = true;
				}
				// Prey: unless aggressive mode, ignore > 1.5× us. Gate by hunt_r.
				bool can_target = (s.mode == 3) || (ratio <= 1.5f);
				if (can_target && d < hunt_r && d < best_prey_d) {
					best_prey_d = d; prey_pos = om.core_pos; have_prey = true;
				}
			}
			for (auto& f : world_.food) {
				float d = glm::length(f.pos - m.core_pos);
				if (d < food_r && d < best_food_d) { best_food_d = d; food_pos = f.pos; have_food = true; }
			}

			// Priority: flee > feed(only feeder-leaning/no-threat) > hunt > wander.
			// mode 0=wander, 1=hunter, 2=feeder, 3=aggressive_hunter
			int choice = 0;
			glm::vec2 target = m.core_pos;
			float    thrust = 0.3f;

			if (have_threat) {
				// Flee opposite direction.
				glm::vec2 away = m.core_pos - threat_pos;
				if (glm::length(away) < 1e-3f) away = glm::vec2(1.0f, 0.0f);
				target = m.core_pos + glm::normalize(away) * 300.0f;
				thrust = 1.0f;
				choice = 2;
			} else if (s.mode == 2 && have_food && best_food_d < food_r) {
				target = food_pos;
				thrust = 0.6f;
				choice = 3;
			} else if (have_prey && best_prey_d < hunt_r) {
				target = prey_pos;
				thrust = (best_prey_d < 80.0f) ? 1.0f
				       : (best_prey_d < 220.0f) ? 0.7f : 0.9f;
				choice = 1;
			} else if (s.mode == 2 && have_food) {
				target = food_pos;
				thrust = 0.6f;
				choice = 3;
			} else {
				// Wander: drift heading slowly.
				s.wander_heading += ang_drift(ai_rng_) * 0.3f;
				target = m.core_pos + glm::vec2(std::cos(s.wander_heading),
				                                std::sin(s.wander_heading)) * 100.0f;
				thrust = 0.3f;
				choice = 0;
			}

			glm::vec2 d = target - m.core_pos;
			s.last_heading = (glm::length(d) > 1e-3f) ? std::atan2(d.y, d.x) : m.heading;
			s.last_thrust = thrust;
			s.last_choice = choice;
		}

		actions[id] = sim::ActionProposal::move(s.last_heading, s.last_thrust);
	}
}

void App::drainSimEvents() {
	auto events = sim_->drain_events();
	for (auto& e : events) {
		char buf[256];
		switch (e.kind) {
		case sim::EventKind::BITE:
			std::snprintf(buf, sizeof(buf), "#%u → #%u  -%.1fhp", e.actor, e.target, e.amount);
			log_.write("BITE", buf);
			emitBiteParticles(e.pos, glm::vec3(1.0f, 0.25f, 0.25f));
			++autotest_bites_;
			break;
		case sim::EventKind::KILL: {
			std::snprintf(buf, sizeof(buf), "#%u killed #%u  +%.1fbm", e.actor, e.target, e.amount);
			log_.write("KILL", buf);
			glm::vec3 col(1.0f, 0.5f, 0.4f);
			if (const sim::Monster* am = world_.get(e.actor)) col = am->color;
			emitKillParticles(e.pos, col);
			if (e.actor == player_id_) {
				++kills_;
				pushFloating("KILL!", glm::vec3(1.0f, 0.6f, 0.6f));
			}
			break;
		}
		case sim::EventKind::PICKUP:
			std::snprintf(buf, sizeof(buf), "#%u picked up +%.1fbm", e.actor, e.amount);
			log_.write("PICKUP", buf);
			emitPickupParticles(e.pos, glm::vec3(0.9f, 1.0f, 0.6f));
			++autotest_pickups_;
			if (e.actor == player_id_) {
				pushFloating("+" + std::to_string((int)std::round(e.amount)) + " bm",
					glm::vec3(0.7f, 1.0f, 0.7f));
			}
			break;
		case sim::EventKind::SPAWN:
			std::snprintf(buf, sizeof(buf), "#%u spawned from #%u (%.1fbm)", e.actor, e.target, e.amount);
			log_.write("SPAWN", buf);
			break;
		case sim::EventKind::DEATH:
			std::snprintf(buf, sizeof(buf), "#%u died (%.1fbm → dust)", e.target, e.amount);
			log_.write("DEATH", buf);
			break;
		case sim::EventKind::GROW:
			std::snprintf(buf, sizeof(buf), "#%u grew x%.2f", e.actor, e.amount);
			log_.write("GROW", buf);
			break;
		}
	}
}

void App::pushFloating(const std::string& s, glm::vec3 c) {
	FloatingEvent fe; fe.text = s; fe.color = c; fe.t_left = 2.0f;
	floaters_.push_back(fe);
	if (floaters_.size() > 8) floaters_.erase(floaters_.begin());
}

void App::stepPlaying(float dt) {
	if (!sim_) return;
	if (paused_) return;

	// Pre-match countdown — freeze the sim and show "3..2..1..GO!".
	if (pre_match_active_) {
		pre_match_t_ += dt;
		// Still follow camera; still draw.
		if (sim::Monster* p = world_.get(player_id_)) camera_world_ = p->core_pos;
		if (pre_match_t_ >= 3.0f) pre_match_active_ = false;
		return;
	}

	match_time_ += dt;
	if (!floaters_.empty()) {
		for (auto& f : floaters_) f.t_left -= dt;
		floaters_.erase(std::remove_if(floaters_.begin(), floaters_.end(),
			[](const FloatingEvent& f){ return f.t_left <= 0.0f; }), floaters_.end());
	}

	std::unordered_map<uint32_t, sim::ActionProposal> actions;
	buildPlayerAction(actions);
	buildAIActions(actions);

	sim_->tick(dt, actions);
	drainSimEvents();

	// Camera follows player (or stays where it was if dead).
	if (sim::Monster* p = world_.get(player_id_)) {
		camera_world_ = p->core_pos;
	}

	// Win / lose conditions
	bool player_alive = world_.get(player_id_) != nullptr;
	int enemies = 0;
	float our_biomass = player_alive ? world_.get(player_id_)->biomass : 0.0f;
	float enemy_max_biomass = 0.0f;
	for (auto& [id, m] : world_.monsters) {
		if (id == player_id_) continue;
		if (!m.alive) continue;
		if (m.owner_id == 1) continue; // our split-spawns
		++enemies;
		if (m.biomass > enemy_max_biomass) enemy_max_biomass = m.biomass;
	}
	if (!player_alive) { goToEndScreen(false); return; }
	if (match_time_ >= 300.0f) { goToEndScreen(true); return; }
	if (enemies <= 1 && our_biomass > enemy_max_biomass && our_biomass > 40.0f) {
		goToEndScreen(true);
		return;
	}
}

void App::emitBiteParticles(glm::vec2 p, glm::vec3 color) {
	std::uniform_real_distribution<float> a(0.0f, 6.28318530718f);
	std::uniform_real_distribution<float> r(4.0f, 14.0f);
	for (int i = 0; i < 5; ++i) {
		float ang = a(ai_rng_);
		float len = r(ai_rng_);
		glm::vec2 d(std::cos(ang) * len, std::sin(ang) * len);
		Particle pa;
		pa.pos_a = p;
		pa.pos_b = p + d;
		pa.color = color;
		pa.half_width = 2.0f;
		pa.t_left = 0.4f;
		pa.t_max  = 0.4f;
		particles_.push_back(pa);
	}
}

void App::emitKillParticles(glm::vec2 p, glm::vec3 color) {
	std::uniform_real_distribution<float> a(0.0f, 6.28318530718f);
	std::uniform_real_distribution<float> r(10.0f, 35.0f);
	for (int i = 0; i < 14; ++i) {
		float ang = a(ai_rng_);
		float len = r(ai_rng_);
		glm::vec2 d(std::cos(ang) * len, std::sin(ang) * len);
		Particle pa;
		pa.pos_a = p;
		pa.pos_b = p + d;
		pa.color = color;
		pa.half_width = 3.0f;
		pa.t_left = 0.7f;
		pa.t_max  = 0.7f;
		particles_.push_back(pa);
	}
}

void App::emitPickupParticles(glm::vec2 p, glm::vec3 color) {
	std::uniform_real_distribution<float> a(0.0f, 6.28318530718f);
	for (int i = 0; i < 3; ++i) {
		float ang = a(ai_rng_);
		glm::vec2 d(std::cos(ang) * 6.0f, std::sin(ang) * 6.0f);
		Particle pa;
		pa.pos_a = p - d;
		pa.pos_b = p + d;
		pa.color = color;
		pa.half_width = 1.8f;
		pa.t_left = 0.35f;
		pa.t_max  = 0.35f;
		particles_.push_back(pa);
	}
}

void App::updateAndDrawParticles(float dt) {
	if (particles_.empty()) return;
	int w, h; glfwGetFramebufferSize(window_.handle(), &w, &h);
	std::vector<ChalkStroke> strokes;
	strokes.reserve(particles_.size());
	for (auto& p : particles_) {
		p.t_left -= dt;
		if (p.t_left <= 0.0f) continue;
		float k = std::max(0.0f, p.t_left / std::max(0.001f, p.t_max));
		ChalkStroke s;
		s.color = p.color * k;
		s.half_width = p.half_width * (0.6f + 0.4f * k);
		s.points = { worldToScreen(p.pos_a), worldToScreen(p.pos_b) };
		strokes.push_back(std::move(s));
	}
	// GC dead particles.
	particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
		[](const Particle& p){ return p.t_left <= 0.0f; }), particles_.end());
	if (!strokes.empty()) renderer_->drawStrokes(strokes, nullptr, w, h);
}

void App::drawMonsters() {
	int w, h; glfwGetFramebufferSize(window_.handle(), &w, &h);
	std::vector<ChalkStroke> strokes;
	strokes.reserve(world_.monsters.size() * 2);

	// Arena boundary — a thin chalk ring around map_radius so players can see
	// where they're clamped. Drawn as a ~64-segment polyline.
	{
		ChalkStroke ring;
		ring.color = glm::vec3(0.55f, 0.55f, 0.55f);
		ring.half_width = 1.5f;
		const int N = 72;
		for (int i = 0; i <= N; ++i) {
			float a = 6.28318530718f * float(i) / float(N);
			glm::vec2 wpos(std::cos(a) * world_.map_radius,
			               std::sin(a) * world_.map_radius);
			ring.points.push_back(worldToScreen(wpos));
		}
		strokes.push_back(std::move(ring));
	}

	for (auto& [id, m] : world_.monsters) {
		if (!m.alive) continue;
		auto wp = sim::transform_to_world(m.shape, m.core_pos, m.heading);
		bool is_player = (id == player_id_);

		ChalkStroke s;
		// Player glows slightly brighter than AI.
		s.color = is_player ? glm::min(m.color + glm::vec3(0.10f), glm::vec3(1.0f))
		                    : m.color;
		s.half_width = is_player ? 4.5f : 3.2f;
		for (auto& v : wp) s.points.push_back(worldToScreen(v));
		s.points.push_back(s.points.front());
		strokes.push_back(std::move(s));

		// Low-HP hurt flash: jittered lighter overlay when < 30%.
		float hp_frac = m.hp / std::max(1.0f, m.hp_max);
		if (hp_frac < 0.30f) {
			std::uniform_real_distribution<float> j(-2.0f, 2.0f);
			ChalkStroke hurt;
			hurt.color = glm::min(m.color + glm::vec3(0.30f, 0.08f, 0.08f), glm::vec3(1.0f));
			hurt.half_width = 2.2f;
			for (auto& v : wp) {
				glm::vec2 sp = worldToScreen(v);
				sp.x += j(ai_rng_);
				sp.y += j(ai_rng_);
				hurt.points.push_back(sp);
			}
			hurt.points.push_back(hurt.points.front());
			strokes.push_back(std::move(hurt));
		}

		// Core crosshair
		glm::vec2 cp = worldToScreen(m.core_pos);
		ChalkStroke c1, c2;
		c1.color = {1.0f, 1.0f, 1.0f}; c1.half_width = 1.8f; c2 = c1;
		c1.points = {{cp.x - 4.0f, cp.y}, {cp.x + 4.0f, cp.y}};
		c2.points = {{cp.x, cp.y - 4.0f}, {cp.x, cp.y + 4.0f}};
		strokes.push_back(std::move(c1));
		strokes.push_back(std::move(c2));

		// Parts on top — world-space transform.
		if (!m.parts.empty()) {
			float ch = std::cos(m.heading), sh = std::sin(m.heading);
			glm::vec2 core_screen = worldToScreen(m.core_pos);
			auto local_to_screen = [&](glm::vec2 v) {
				glm::vec2 wv(ch * v.x - sh * v.y, sh * v.x + ch * v.y);
				return worldToScreen(m.core_pos + wv);
			};
			appendPartStrokes(m.parts, m.color, local_to_screen, 1.0f,
				(float)glfwGetTime(), strokes);
			(void)core_screen;
		}

		// Poison radius faint ring.
		if (m.part_effect.poison_dps > 0.0f) {
			ChalkStroke ring;
			ring.color = glm::vec3(0.4f, 0.9f, 0.4f);
			ring.half_width = 1.2f;
			const int N = 36;
			for (int i = 0; i <= N; ++i) {
				float a = 6.28318530718f * float(i) / float(N);
				glm::vec2 wp(m.core_pos.x + std::cos(a) * m.part_effect.poison_radius,
				             m.core_pos.y + std::sin(a) * m.part_effect.poison_radius);
				ring.points.push_back(worldToScreen(wp));
			}
			strokes.push_back(std::move(ring));
		}
	}
	renderer_->drawStrokes(strokes, nullptr, w, h);
}

void App::drawFood() {
	int w, h; glfwGetFramebufferSize(window_.handle(), &w, &h);
	std::vector<ChalkStroke> strokes;
	strokes.reserve(world_.food.size());
	for (auto& f : world_.food) {
		glm::vec2 sp = worldToScreen(f.pos);
		if (sp.x < -50.0f || sp.x > w + 50.0f) continue;
		if (sp.y < -50.0f || sp.y > h + 50.0f) continue;
		ChalkStroke s;
		s.color = {0.9f, 1.0f, 0.6f};
		s.half_width = 2.2f;
		// A short 2-point stroke (dot-ish).
		s.points = {{sp.x - 1.0f, sp.y}, {sp.x + 1.0f, sp.y}};
		strokes.push_back(std::move(s));
	}
	renderer_->drawStrokes(strokes, nullptr, w, h);
}

void App::drawHUD() {
	float aspect = window_.aspectRatio();
	sim::Monster* p = world_.get(player_id_);

	// HP bar top-center
	float hw = 0.5f, hh = 0.02f;
	float hx = -hw * 0.5f, hy = 0.92f;
	text_->drawRect(hx, hy, hw, hh, glm::vec4(0.1f, 0.1f, 0.1f, 0.8f));
	if (p) {
		float frac = p->hp / std::max(1.0f, p->hp_max);
		text_->drawRect(hx, hy, hw * frac, hh,
			glm::vec4(0.9f, 0.4f, 0.4f, 1.0f));
	}

	// Biomass / kills / time
	char line[128];
	std::snprintf(line, sizeof(line), "BIOMASS %.0f   KILLS %d   TIME %d:%02d",
		p ? p->biomass : 0.0f, kills_,
		(int)match_time_ / 60, (int)match_time_ % 60);
	text_->drawText(line, -0.5f, 0.85f, 0.9f,
		glm::vec4(1.0f, 1.0f, 0.95f, 1.0f), aspect);

	if (paused_) {
		text_->drawTitle("PAUSED", -0.15f, 0.0f, 2.5f,
			glm::vec4(1.0f, 1.0f, 0.7f, 1.0f), aspect);
		text_->drawText("PRESS P TO RESUME", -0.2f, -0.1f, 1.0f,
			glm::vec4(0.85f, 0.85f, 0.85f, 1.0f), aspect);
	}

	if (pre_match_active_) {
		int n = 3 - (int)pre_match_t_;
		if (n > 0) {
			char c[2] = { (char)('0' + n), 0 };
			text_->drawTitle(c, -0.04f, 0.0f, 4.5f,
				glm::vec4(1.0f, 0.95f, 0.4f, 1.0f), aspect);
		} else {
			text_->drawTitle("GO!", -0.09f, 0.0f, 4.5f,
				glm::vec4(0.6f, 1.0f, 0.6f, 1.0f), aspect);
		}
	}

	// Controls hint bottom
	text_->drawText("MOUSE AIM  LCLICK THRUST  1 SPLIT  2 GROW  P PAUSE  ESC MENU",
		-0.7f, -0.96f, 0.7f,
		glm::vec4(0.7f, 0.7f, 0.7f, 0.7f), aspect);

	// Floating events bottom-left
	for (size_t i = 0; i < floaters_.size(); ++i) {
		auto& f = floaters_[i];
		float alpha = std::min(1.0f, f.t_left / 2.0f);
		text_->drawText(f.text, -0.9f, -0.7f + (float)i * 0.06f, 1.0f,
			glm::vec4(f.color, alpha), aspect);
	}

	// Minimap top-right
	int w, h; glfwGetFramebufferSize(window_.handle(), &w, &h);
	float mm_size_ndc = 0.2f;
	float mm_x = 0.78f, mm_y = 0.68f;
	text_->drawRect(mm_x, mm_y, mm_size_ndc, mm_size_ndc,
		glm::vec4(0.05f, 0.05f, 0.05f, 0.85f));
	float R = world_.map_radius;
	for (auto& [id, m] : world_.monsters) {
		if (!m.alive) continue;
		glm::vec4 c = (id == player_id_)
			? glm::vec4(1.0f, 1.0f, 0.3f, 1.0f)
			: (m.owner_id == 1 ? glm::vec4(0.5f, 1.0f, 0.5f, 1.0f)
			                   : glm::vec4(m.color, 1.0f));
		float nx = m.core_pos.x / R * 0.5f + 0.5f;
		float ny = m.core_pos.y / R * 0.5f + 0.5f;
		float dx = mm_x + nx * mm_size_ndc - 0.006f;
		float dy = mm_y + ny * mm_size_ndc - 0.006f;
		text_->drawRect(dx, dy, 0.012f, 0.012f, c);
	}
	(void)w; (void)h;
}

void App::drawPlaying(float dt) {
	stepPlaying(dt);
	if (state_ != AppState::PLAYING) return; // stepPlaying may have transitioned
	drawFood();
	drawMonsters();
	drawHUD();
}

// ========================================================================
// END_SCREEN
// ========================================================================

void App::drawEndScreen(float dt) {
	{
		float aspect = window_.aspectRatio();
		text_->drawRect(-1.0f, -1.0f, 2.0f, 2.0f, glm::vec4(0.04f, 0.04f, 0.06f, 0.92f));
		const char* banner = end_won_ ? "YOU WON!" : "SO CLOSE!";
		text_->drawTitle(banner, -0.30f, 0.45f, 2.8f,
			end_won_ ? glm::vec4(0.55f, 1.0f, 0.55f, 1.0f)
			         : glm::vec4(1.0f, 0.85f, 0.45f, 1.0f),
			aspect);

		// Creature wiggle / slump animation in center.
		state_time_ += 0.0f; // already accumulated by main loop
		int w_px, h_px; glfwGetFramebufferSize(window_.handle(), &w_px, &h_px);
		float t = state_time_;
		float scale = 1.0f, rot = 0.0f;
		if (end_won_) {
			scale = 1.0f + 0.10f * std::sin(t * 6.0f);
			rot   = std::sin(t * 5.0f) * (20.0f * 3.14159f / 180.0f);
		} else {
			scale = 1.0f - 0.05f;
			rot   = -8.0f * 3.14159f / 180.0f;
		}
		auto poly = sim::cellToPolygon(lab_.cell(), 1);
		float cx = w_px * 0.5f, cy = h_px * 0.55f;
		float pxu = 2.4f * scale;
		ChalkStroke outline; outline.color = lab_.color(); outline.half_width = 5.0f;
		float c = std::cos(rot), si = std::sin(rot);
		for (auto& v : poly) {
			glm::vec2 r(c * v.x - si * v.y, si * v.x + c * v.y);
			outline.points.push_back({cx + r.x * pxu, cy - r.y * pxu});
		}
		outline.points.push_back(outline.points.front());
		std::vector<ChalkStroke> ss{outline};
		auto local_to_screen = [&](glm::vec2 v) {
			glm::vec2 r(c * v.x - si * v.y, si * v.x + c * v.y);
			return glm::vec2(cx + r.x * pxu, cy - r.y * pxu);
		};
		appendPartStrokes(lab_.parts(), lab_.color(), local_to_screen, pxu,
		                  (float)glfwGetTime(), ss);
		renderer_->drawStrokes(ss, nullptr, w_px, h_px);

		// Confetti (win only).
		if (end_won_ && particles_.empty() && t < 0.05f) {
			std::uniform_real_distribution<float> a(0.0f, 6.28318530718f);
			std::uniform_real_distribution<float> r(40.0f, 250.0f);
			for (int i = 0; i < 60; ++i) {
				float ang = a(ai_rng_), len = r(ai_rng_);
				Particle pa;
				pa.pos_a = glm::vec2(std::cos(ang), std::sin(ang)) * len;
				pa.pos_b = pa.pos_a + glm::vec2(std::cos(ang), std::sin(ang)) * 14.0f;
				pa.color = lab_.color();
				pa.half_width = 3.0f;
				pa.t_left = 2.0f; pa.t_max = 2.0f;
				particles_.push_back(pa);
			}
		}
		camera_world_ = glm::vec2(0.0f);
		updateAndDrawParticles(dt);

		// Buttons
		Button again { -0.32f, -0.55f, 0.30f, 0.12f, "PLAY AGAIN!" };
		Button menu  {  0.02f, -0.55f, 0.30f, 0.12f, "MENU" };
		if (drawButton(again)) { goToStarter(); return; }
		if (drawButton(menu))  { goToMainMenu(); return; }
		return;
	}
}

// ========================================================================
// KID MODE — starter picker, kid lab, celebration
// ========================================================================

void App::goToStarter() {
	state_ = AppState::STARTER;
	state_time_ = 0.0f;
}

void App::goToLab(monsters::StarterKind kind) {
	std::mt19937 rng((uint32_t)(opts_.seed ^ (uint32_t)std::time(nullptr)));
	auto t = monsters::makeStarter(kind, rng);
	creature_name_ = generateName(rng);
	lab_.load_starter(t.cell, t.parts, t.color, creature_name_);
	state_ = AppState::LAB;
	state_time_ = 0.0f;
}

void App::goToCelebrate() {
	state_ = AppState::CELEBRATE;
	state_time_ = 0.0f;
	celebrate_t_ = 0.0f;
}

void App::startMatchFromLab() {
	monsters::MonsterTemplate t;
	t.id    = "custom:lab";
	t.name  = lab_.name();
	t.cell  = lab_.cell();
	t.parts = lab_.parts();
	t.color = lab_.color();
	t.initial_biomass = 28.0f;
	startMatchWithTemplate(t);
}

void App::drawStarter(float dt) {
	(void)dt;
	float aspect = window_.aspectRatio();
	text_->drawRect(-1.0f, -1.0f, 2.0f, 2.0f, glm::vec4(0.04f, 0.04f, 0.06f, 1.0f));
	text_->drawTitle("WHAT DO YOU WANT TO BUILD?", -0.55f, 0.78f, 1.6f,
		glm::vec4(1.0f, 0.95f, 0.75f, 1.0f), aspect);

	int w_px, h_px; glfwGetFramebufferSize(window_.handle(), &w_px, &h_px);
	const int n = 6;
	const monsters::StarterKind kinds[6] = {
		monsters::StarterKind::SPIKY,
		monsters::StarterKind::SQUISHY,
		monsters::StarterKind::ZOOMY,
		monsters::StarterKind::TOUGH,
		monsters::StarterKind::RANDOM,
		monsters::StarterKind::PLAIN,
	};

	float tile_w = 0.30f, tile_h = 0.50f;
	float pad_x = 0.05f, pad_y = 0.08f;
	float total_w = tile_w * 3 + pad_x * 2;
	float total_h = tile_h * 2 + pad_y;
	float x0 = -total_w * 0.5f;
	float y0 = -total_h * 0.5f - 0.10f;

	std::mt19937 preview_rng(0xBEEF);

	for (int i = 0; i < n; ++i) {
		int col = i % 3, row = i / 3;
		Button tile { x0 + col * (tile_w + pad_x),
		              y0 + (1 - row) * (tile_h + pad_y),
		              tile_w, tile_h, "" };
		bool hover = pointInButton(pxToNDC(mouse_px_), tile);
		glm::vec4 bg = hover ? glm::vec4(0.20f, 0.25f, 0.20f, 0.95f)
		                     : glm::vec4(0.10f, 0.12f, 0.10f, 0.92f);
		text_->drawRect(tile.x, tile.y, tile.w, tile.h, bg);
		// Border
		glm::vec4 edge(0.92f, 0.88f, 0.70f, 1.0f);
		float t_w = 0.005f;
		text_->drawRect(tile.x, tile.y, tile.w, t_w, edge);
		text_->drawRect(tile.x, tile.y + tile.h - t_w, tile.w, t_w, edge);
		text_->drawRect(tile.x, tile.y, t_w, tile.h, edge);
		text_->drawRect(tile.x + tile.w - t_w, tile.y, t_w, tile.h, edge);

		// Render preview creature (chalk strokes).
		auto tmpl = monsters::makeStarter(kinds[i], preview_rng);
		float cx = (tile.x + tile.w * 0.5f + 1.0f) * 0.5f * (float)w_px;
		float cy = (1.0f - (tile.y + tile.h * 0.55f + 1.0f) * 0.5f) * (float)h_px;
		auto preview_shape = tmpl.shape();
		float max_r = 0.0f;
		for (auto& v : preview_shape) max_r = std::max(max_r, glm::length(v));
		float k = (max_r > 0.0f) ? (90.0f / max_r) : 1.0f;
		ChalkStroke outline;
		outline.color = tmpl.color;
		outline.half_width = 4.0f;
		for (auto& v : preview_shape) outline.points.push_back({cx + v.x * k, cy - v.y * k});
		outline.points.push_back(outline.points.front());
		std::vector<ChalkStroke> tmp{outline};
		renderer_->drawStrokes(tmp, nullptr, w_px, h_px);
		auto local_to_screen = [&](glm::vec2 v) { return glm::vec2(cx + v.x * k, cy - v.y * k); };
		std::vector<ChalkStroke> ps;
		appendPartStrokes(tmpl.parts, tmpl.color, local_to_screen, k,
		                  (float)glfwGetTime(), ps);
		if (!ps.empty()) renderer_->drawStrokes(ps, nullptr, w_px, h_px);

		// Title + tagline
		text_->drawText(monsters::starterName(kinds[i]),
			tile.x + 0.02f, tile.y + 0.04f, 1.7f,
			glm::vec4(tmpl.color.r, tmpl.color.g, tmpl.color.b, 1.0f), aspect);
		text_->drawText(monsters::starterTagline(kinds[i]),
			tile.x + 0.02f, tile.y + 0.005f, 0.9f,
			glm::vec4(0.85f, 0.82f, 0.70f, 1.0f), aspect);

		if (hover && mouse_left_click_) {
			goToLab(kinds[i]);
			return;
		}
	}

	Button back { -0.10f, -0.92f, 0.20f, 0.07f, "MENU" };
	if (drawButton(back)) goToMainMenu();
}

void App::drawLab(float dt) {
	LabInput in;
	in.mouse_px = mouse_px_;
	in.mouse_left_down  = mouse_left_down_;
	in.mouse_right_down = mouse_right_down_;
	in.mouse_left_click  = mouse_left_click_;
	in.mouse_right_click = mouse_right_click_;
	in.keys_pressed = keys_pressed_this_frame_;
	LabOutcome outc = lab_.update(dt, in);
	if (outc == LabOutcome::USE) {
		creature_name_ = lab_.name();
		goToCelebrate();
	} else if (outc == LabOutcome::BACK) {
		goToMainMenu();
	}
}

void App::drawCelebrate(float dt) {
	celebrate_t_ += dt;
	float aspect = window_.aspectRatio();
	int w_px, h_px; glfwGetFramebufferSize(window_.handle(), &w_px, &h_px);

	text_->drawRect(-1.0f, -1.0f, 2.0f, 2.0f, glm::vec4(0.0f, 0.0f, 0.0f, 0.65f));

	float t = celebrate_t_;
	// Animation: grow 1.0 → 1.4 over 0.5s, wobble ±20° next 0.5s, shrink to 1.0
	float scale = 1.0f, rot = 0.0f;
	if (t < 0.5f) {
		float k = t / 0.5f;
		scale = 1.0f + 0.4f * k;
	} else if (t < 1.0f) {
		scale = 1.4f;
		rot = std::sin((t - 0.5f) * 25.0f) * (20.0f * 3.14159f / 180.0f);
	} else if (t < 1.6f) {
		float k = (t - 1.0f) / 0.6f;
		scale = 1.4f - 0.4f * k;
	} else {
		scale = 1.0f;
	}

	// Render creature in center.
	auto poly = sim::cellToPolygon(lab_.cell(), 1);
	float cx = w_px * 0.5f, cy = h_px * 0.5f;
	float pxu = 3.0f * scale;
	ChalkStroke outline;
	outline.color = lab_.color();
	outline.half_width = 5.5f;
	float c = std::cos(rot), si = std::sin(rot);
	for (auto& v : poly) {
		glm::vec2 r(c * v.x - si * v.y, si * v.x + c * v.y);
		outline.points.push_back({cx + r.x * pxu, cy - r.y * pxu});
	}
	outline.points.push_back(outline.points.front());
	std::vector<ChalkStroke> ss{outline};
	auto local_to_screen = [&](glm::vec2 v) {
		glm::vec2 r(c * v.x - si * v.y, si * v.x + c * v.y);
		return glm::vec2(cx + r.x * pxu, cy - r.y * pxu);
	};
	appendPartStrokes(lab_.parts(), lab_.color(), local_to_screen, pxu,
	                  (float)glfwGetTime(), ss);

	// Confetti particles around the creature.
	if (particles_.empty() && t < 0.05f) {
		std::uniform_real_distribution<float> a(0.0f, 6.28318530718f);
		std::uniform_real_distribution<float> r(40.0f, 220.0f);
		glm::vec2 wp(0.0f);
		for (int i = 0; i < 40; ++i) {
			float ang = a(ai_rng_), len = r(ai_rng_);
			Particle pa;
			pa.pos_a = wp + glm::vec2(std::cos(ang), std::sin(ang)) * len;
			pa.pos_b = pa.pos_a + glm::vec2(std::cos(ang), std::sin(ang)) * 12.0f;
			pa.color = lab_.color();
			pa.half_width = 3.0f;
			pa.t_left = 1.4f;
			pa.t_max  = 1.4f;
			particles_.push_back(pa);
		}
	}
	// Render particles relative to screen-center (treat as world=screen here).
	camera_world_ = glm::vec2(0.0f);
	updateAndDrawParticles(dt);

	renderer_->drawStrokes(ss, nullptr, w_px, h_px);

	// Banner
	std::string banner = "MEET " + creature_name_ + "!";
	float w_chars = (float)banner.size() * 0.018f * 2.4f;
	text_->drawText(banner, -w_chars * 0.5f, 0.55f, 2.4f,
		glm::vec4(1.0f, 0.95f, 0.55f, 1.0f), aspect);

	// Skip-on-click; auto-advance after 2s.
	if (t >= 2.0f || mouse_left_click_) {
		particles_.clear();
		startMatchFromLab();
	}
}

// ========================================================================
// PPM writer
// ========================================================================

void App::writePPM(const char* path) {
	int w, h; glfwGetFramebufferSize(window_.handle(), &w, &h);
	std::vector<unsigned char> buf(w * h * 3);
	glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, buf.data());
	FILE* f = std::fopen(path, "wb");
	if (!f) return;
	std::fprintf(f, "P6\n%d %d\n255\n", w, h);
	for (int y = h - 1; y >= 0; --y) std::fwrite(buf.data() + y * w * 3, 1, w * 3, f);
	std::fclose(f);
	std::printf("wrote %s (%dx%d)\n", path, w, h);
}

} // namespace civcraft::cellcraft

// LifeCraft — app implementation. See app.h.
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
//      * If core isn't placed in DRAW_LAB, USE auto-places it at centroid.
//      * Custom monster is session-memory only; no disk persistence.

#include "LifeCraft/client/app.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "LifeCraft/client/part_render.h"
#include "LifeCraft/sim/part.h"
#include "LifeCraft/sim/part_stats.h"
#include "LifeCraft/sim/shape_smooth.h"
#include "LifeCraft/sim/shape_validate.h"
#include "LifeCraft/sim/tuning.h"
#include "client/gl.h"

namespace civcraft::lifecraft {

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
		writePPM("/tmp/lifecraft_screenshot.ppm");
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
	if (!window_.init(w, h, "LifeCraft")) return false;
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

	if (opts_.autotest || !opts_.play_screenshot_path.empty()) {
		// jump straight into PLAYING with stinger vs 4 AIs, seeded.
		world_.rng.seed(opts_.seed);
		ai_rng_.seed(opts_.seed + 1);
		state_ = AppState::MAIN_MENU;  // temporary — startMatchWithTemplate will change it
		ai_plays_player_ = opts_.autotest; // player auto-hunts for autotest
		// autotest → stinger (pointy attacker); screenshot → blob for visibility.
		int idx = opts_.autotest ? 0 : 1;
		autotest_prebuilt_id_ = prebuilts_[idx].id;
		startMatchWithTemplate(prebuilts_[idx]);
	} else if (!opts_.menu_screenshot_path.empty()) {
		state_ = AppState::MAIN_MENU;
	} else if (!opts_.select_screenshot_path.empty()) {
		state_ = AppState::MONSTER_SELECT;
	} else if (!opts_.lab_screenshot_path.empty()
	        || !opts_.lab_assemble_screenshot_path.empty()) {
		state_ = AppState::DRAW_LAB;
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
		FILE* log = std::fopen("/tmp/lifecraft_autotest.log", "w");
		if (!log) { std::fprintf(stderr, "couldn't open autotest log\n"); return; }
		std::fprintf(log, "# LifeCraft autotest  seed=%u  seconds=%d\n",
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

	// --- lab screenshots: either DRAWING with preseeded strokes, or ASSEMBLING
	// with a finalized body and preplaced parts + mirror on.
	if (!opts_.lab_screenshot_path.empty() || !opts_.lab_assemble_screenshot_path.empty()) {
		bool assemble = !opts_.lab_assemble_screenshot_path.empty();
		const char* out_path = assemble ? opts_.lab_assemble_screenshot_path.c_str()
		                                : opts_.lab_screenshot_path.c_str();
		state_ = AppState::DRAW_LAB;
		int fw, fh; glfwGetFramebufferSize(window_.handle(), &fw, &fh);
		// Seed some squiggly strokes forming a blob in the canvas.
		float cx = (float)fw * 0.50f;
		float cy = (float)fh * 0.55f;
		{
			ChalkStroke s1;
			s1.color = glm::vec3(0.95f, 0.95f, 0.92f);
			s1.half_width = 3.0f;
			for (int i = 0; i < 36; ++i) {
				float a = 6.28318530718f * (float)i / 35.0f;
				float r = 140.0f + 12.0f * std::cos(a * 4.0f);
				s1.points.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r * 0.75f});
			}
			LabInput seed;
			// Feed one big stroke as a "finalized" stroke via update() is awkward;
			// easier to poke lab_ internals through its public seed API. We use
			// reset() + a small helper: simulate by doing dummy updates.
			// Instead: we inject by calling update() with the seeded stroke — the
			// simplest route is to keep a private seed function; to avoid adding
			// one, run a short simulated drag:
			lab_.reset();
			// Fake a drag: press, move through each point, release.
			LabInput p = seed;
			p.mouse_px = s1.points.front();
			p.mouse_left_down = true;
			p.mouse_left_click = true;
			lab_.update(1.0f / 60.0f, p);
			p.mouse_left_click = false;
			for (auto& pt : s1.points) {
				p.mouse_px = pt;
				lab_.update(1.0f / 60.0f, p);
			}
			// release
			p.mouse_left_down = false;
			lab_.update(1.0f / 60.0f, p);
		}
		if (assemble) {
			// Finalize by synthesizing a click on FINALIZE — easier: call update
			// enough times, and tell it "left-click at the finalize button"...
			// Actually simplest: expose by adding a test-only helper. We do the
			// minimum hack: render a frame first so buttons exist in-screen-space,
			// then click at the approximate FINALIZE position. The button rect
			// is centered at canvas_cx, bottom row.
			int w2, h2; glfwGetFramebufferSize(window_.handle(), &w2, &h2);
			float bottom_bar = (float)h2 * 0.10f;
			float cw = (float)w2 * 0.64f;
			float canvas_cx2 = (float)w2 * 0.18f + cw * 0.5f;
			float bw = std::min(180.0f, cw * 0.18f);
			float gap = 10.0f;
			float total = bw * 3.0f + gap * 2.0f;
			float bx0 = canvas_cx2 - total * 0.5f;
			float by = (float)h2 - bottom_bar + 8.0f;
			float bh = bottom_bar - 16.0f;
			// FINALIZE is index 2.
			glm::vec2 click_pt(bx0 + 2 * (bw + gap) + bw * 0.5f, by + bh * 0.5f);
			LabInput p;
			p.mouse_px = click_pt;
			p.mouse_left_click = true;
			lab_.update(1.0f / 60.0f, p);
			// Now place some parts by selecting the palette then clicking canvas.
			p.mouse_left_click = false;
			// Helper to click a pixel.
			auto click = [&](glm::vec2 at) {
				LabInput q; q.mouse_px = at; q.mouse_left_click = true;
				lab_.update(1.0f / 60.0f, q);
			};
			// Mirror on.
			{
				LabInput q; q.mouse_px = click_pt; q.keys_pressed = { GLFW_KEY_M };
				lab_.update(1.0f / 60.0f, q);
			}
			// Select SPIKE (first palette row).
			float left_w = (float)w2 * 0.18f;
			float y0 = (float)h2 * 0.08f + 120.0f;
			float row_h = 50.0f;
			auto select_row = [&](int idx) {
				float rx = 6.0f + 0.5f * (left_w - 12.0f);
				float ry = y0 + (float)idx * row_h + (row_h - 4.0f) * 0.5f;
				click({rx, ry});
			};
			select_row(0); // SPIKE
			click({canvas_cx2 + 60.0f, (float)h2 * 0.55f - 20.0f});
			select_row(5); // CILIA
			click({canvas_cx2 - 80.0f, (float)h2 * 0.55f});
			select_row(6); // HORN
			click({canvas_cx2 + 100.0f, (float)h2 * 0.55f + 30.0f});
			select_row(7); // REGEN
			click({canvas_cx2 - 40.0f, (float)h2 * 0.55f + 40.0f});
		}
		// Render a few frames.
		double t0 = glfwGetTime();
		while (!window_.shouldClose() && (glfwGetTime() - t0) < 0.5) {
			window_.pollEvents();
			float dt = 1.0f / 60.0f;
			int w, h; glfwGetFramebufferSize(window_.handle(), &w, &h);
			glViewport(0, 0, w, h);
			renderer_->drawBoard(w, h, (float)glfwGetTime());
			drawDrawLab(dt);
			mouse_left_click_ = false;
			mouse_right_click_ = false;
			keys_pressed_this_frame_.clear();
			window_.swapBuffers();
		}
		writePPM(out_path);
		return;
	}

	// --- menu / select screenshot modes ---
	if (!opts_.menu_screenshot_path.empty() || !opts_.select_screenshot_path.empty()) {
		const char* out_path = !opts_.menu_screenshot_path.empty()
			? opts_.menu_screenshot_path.c_str()
			: opts_.select_screenshot_path.c_str();
		double t0 = glfwGetTime();
		const double limit = 0.5;
		while (!window_.shouldClose() && (glfwGetTime() - t0) < limit) {
			window_.pollEvents();
			float dt = 1.0f / 60.0f;
			state_time_ += dt;
			int w, h; glfwGetFramebufferSize(window_.handle(), &w, &h);
			glViewport(0, 0, w, h);
			renderer_->drawBoard(w, h, (float)glfwGetTime());
			if (state_ == AppState::MAIN_MENU)           drawMainMenu(dt);
			else if (state_ == AppState::MONSTER_SELECT) drawMonsterSelect(dt);
			mouse_left_click_ = false;
			keys_pressed_this_frame_.clear();
			window_.swapBuffers();
		}
		writePPM(out_path);
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
		case AppState::LOADING:        drawLoading(dt);        break;
		case AppState::MAIN_MENU:      drawMainMenu(dt);       break;
		case AppState::MONSTER_SELECT: drawMonsterSelect(dt);  break;
		case AppState::DRAW_LAB:       drawDrawLab(dt);        break;
		case AppState::PLAYING:        drawPlaying(dt);        break;
		case AppState::END_SCREEN:     drawEndScreen(dt);      break;
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

void App::goToMainMenu()       { state_ = AppState::MAIN_MENU;      state_time_ = 0.0f; }
void App::goToMonsterSelect()  { state_ = AppState::MONSTER_SELECT; state_time_ = 0.0f; }
void App::goToDrawLab() {
	state_ = AppState::DRAW_LAB;
	state_time_ = 0.0f;
	lab_.reset();
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

void App::startMatchWithCustomShape(const std::vector<glm::vec2>& local_shape,
                                    glm::vec3 color) {
	monsters::MonsterTemplate t;
	t.id = "custom:lab";
	t.name = "Custom";
	t.shape = local_shape;
	t.color = color;
	t.initial_biomass = 25.0f;
	t.parts = lab_.parts();
	startMatchWithTemplate(t);
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
	text_->drawTitle("LIFECRAFT", -0.26f, 0.05f, 3.0f,
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

void App::drawMainMenu(float) {
	text_->drawTitle("LIFECRAFT", -0.26f, 0.45f, 3.0f,
		glm::vec4(1.0f, 1.0f, 0.95f, 1.0f), window_.aspectRatio());
	text_->drawText("CHALKBOARD SURVIVAL", -0.22f, 0.30f, 1.0f,
		glm::vec4(0.8f, 0.8f, 0.75f, 0.9f), window_.aspectRatio());

	Button play { -0.22f,  0.05f, 0.44f, 0.12f, "PLAY" };
	Button lab  { -0.22f, -0.12f, 0.44f, 0.12f, "MONSTER LAB" };
	Button quit { -0.22f, -0.29f, 0.44f, 0.12f, "QUIT" };

	if (drawButton(play)) goToMonsterSelect();
	if (drawButton(lab))  goToDrawLab();
	if (drawButton(quit)) glfwSetWindowShouldClose(window_.handle(), 1);
}

// ========================================================================
// MONSTER_SELECT
// ========================================================================

static void drawStatBar(TextRenderer& text, float x, float y, float w,
                        const char* label, float frac, glm::vec4 col) {
	text.drawText(label, x, y, 0.7f, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), 1.0f);
	float bar_y = y - 0.012f;
	text.drawRect(x, bar_y, w, 0.008f, glm::vec4(0.15f, 0.15f, 0.15f, 0.8f));
	text.drawRect(x, bar_y, w * std::max(0.0f, std::min(1.0f, frac)), 0.008f, col);
}

// Normalize a stat value in [min,max] to [0,1] for a meter bar.
static float norm01(float v, float lo, float hi) {
	return std::max(0.0f, std::min(1.0f, (v - lo) / std::max(1e-3f, (hi - lo))));
}

void App::drawMonsterSelect(float) {
	text_->drawTitle("SELECT MONSTER", -0.3f, 0.72f, 1.8f,
		glm::vec4(1.0f, 1.0f, 0.95f, 1.0f), window_.aspectRatio());

	int n = (int)prebuilts_.size();
	float tile_w = 0.36f, tile_h = 0.8f;
	float total_w = tile_w * n + 0.04f * (n - 1);
	float x0 = -total_w * 0.5f;
	float y0 = -0.30f;

	int w_px, h_px; glfwGetFramebufferSize(window_.handle(), &w_px, &h_px);

	for (int i = 0; i < n; ++i) {
		Button tile { x0 + i * (tile_w + 0.04f), y0, tile_w, tile_h, "" };
		bool hover = pointInButton(pxToNDC(mouse_px_), tile);
		glm::vec4 bg = hover ? glm::vec4(0.18f, 0.18f, 0.22f, 0.9f)
		                     : glm::vec4(0.08f, 0.08f, 0.10f, 0.8f);
		text_->drawRect(tile.x, tile.y, tile.w, tile.h, bg);

		const auto& m = prebuilts_[i];
		// Title
		text_->drawText(m.name, tile.x + 0.03f, tile.y + tile.h - 0.08f, 1.3f,
			glm::vec4(m.color.r, m.color.g, m.color.b, 1.0f),
			window_.aspectRatio());

		// Shape preview — render as chalk strokes in screen-space (pixel coords
		// relative to the tile center). Convert tile NDC to pixels.
		float cx = (tile.x + tile.w * 0.5f + 1.0f) * 0.5f * (float)w_px;
		float cy = (1.0f - (tile.y + tile.h * 0.55f + 1.0f) * 0.5f) * (float)h_px;
		ChalkStroke outline;
		outline.color = m.color;
		outline.half_width = 3.5f;
		// Scale shape so its max radius fills ~80px.
		float max_r = 0.0f;
		for (auto& v : m.shape) max_r = std::max(max_r, glm::length(v));
		float k = (max_r > 0.0f) ? (80.0f / max_r) : 1.0f;
		for (auto& v : m.shape) outline.points.push_back({cx + v.x * k, cy - v.y * k});
		outline.points.push_back(outline.points.front());
		// Draw preview stroke. We need to switch rendering modes within the
		// same frame; just call drawStrokes with a one-off vector.
		std::vector<ChalkStroke> tmp{outline};
		renderer_->drawStrokes(tmp, nullptr, w_px, h_px);

		// Core marker
		ChalkStroke cross1, cross2;
		cross1.color = {1.0f, 1.0f, 1.0f}; cross1.half_width = 2.0f;
		cross2 = cross1;
		cross1.points = {{cx - 5.0f, cy}, {cx + 5.0f, cy}};
		cross2.points = {{cx, cy - 5.0f}, {cx, cy + 5.0f}};
		std::vector<ChalkStroke> cx_strokes{cross1, cross2};
		renderer_->drawStrokes(cx_strokes, nullptr, w_px, h_px);

		// Parts preview.
		if (!m.parts.empty()) {
			auto local_to_screen = [&](glm::vec2 v) {
				return glm::vec2(cx + v.x * k, cy - v.y * k);
			};
			std::vector<ChalkStroke> part_strokes;
			appendPartStrokes(m.parts, m.color, local_to_screen, k,
				(float)glfwGetTime(), part_strokes);
			if (!part_strokes.empty())
				renderer_->drawStrokes(part_strokes, nullptr, w_px, h_px);
		}

		// Stats — compute via a throwaway Monster.
		sim::Monster probe = monsters::makeMonsterFromTemplate(m, 0, glm::vec2(0.0f), 0.0f);
		float sx = tile.x + 0.03f;
		float sy = tile.y + 0.16f;
		drawStatBar(*text_, sx, sy,        tile.w - 0.06f, "SPEED",
			norm01(probe.move_speed, sim::MOVE_MIN, sim::MOVE_MAX),
			glm::vec4(0.55f, 0.82f, 1.0f, 1.0f));
		drawStatBar(*text_, sx, sy - 0.05f, tile.w - 0.06f, "TURN",
			norm01(probe.turn_speed, sim::TURN_MIN, sim::TURN_MAX),
			glm::vec4(0.55f, 1.0f, 0.65f, 1.0f));
		drawStatBar(*text_, sx, sy - 0.10f, tile.w - 0.06f, "MASS",
			norm01(probe.mass, 0.0f, 200.0f),
			glm::vec4(1.0f, 0.72f, 0.45f, 1.0f));

		if (hover && mouse_left_click_) {
			startMatchWithTemplate(m);
			return;
		}
	}

	Button custom { -0.22f, -0.78f, 0.44f, 0.10f, "DRAW YOUR OWN" };
	if (drawButton(custom)) goToDrawLab();
	Button back   {  0.60f, -0.90f, 0.35f, 0.08f, "BACK" };
	if (drawButton(back))   goToMainMenu();
}

// ========================================================================
// DRAW_LAB — delegated to LabScreen.
// ========================================================================

void App::drawDrawLab(float dt) {
	LabInput in;
	in.mouse_px         = mouse_px_;
	in.mouse_left_down  = mouse_left_down_;
	in.mouse_right_down = mouse_right_down_;
	in.mouse_left_click  = mouse_left_click_;
	in.mouse_right_click = mouse_right_click_;
	in.keys_pressed = keys_pressed_this_frame_;

	LabOutcome outc = lab_.update(dt, in);
	switch (outc) {
	case LabOutcome::NONE: break;
	case LabOutcome::USE:
		startMatchWithCustomShape(lab_.local_shape(), lab_.color());
		break;
	case LabOutcome::BACK:
		goToMainMenu();
		break;
	}
}

// ========================================================================
// PLAYING
// ========================================================================

void App::buildPlayerAction(std::unordered_map<uint32_t, sim::ActionProposal>& actions) {
	sim::Monster* p = world_.get(player_id_);
	if (!p) return;

	if (ai_plays_player_) {
		// Autotest player: prefer food until well-fed, then hunt nearest.
		glm::vec2 target = p->core_pos;
		float best = 1e9f;
		bool feeding = p->biomass < 50.0f;
		if (feeding) {
			for (auto& f : world_.food) {
				float d = glm::length(f.pos - p->core_pos);
				if (d < best) { best = d; target = f.pos; }
			}
		}
		if (!feeding || best > 600.0f) {
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

			for (auto& [oid, om] : world_.monsters) {
				if (oid == id || !om.alive) continue;
				if (om.owner_id == m.owner_id) continue;
				float d = glm::length(om.core_pos - m.core_pos);
				float ratio = om.biomass / std::max(1.0f, m.biomass);
				// Threat: > 1.3× our biomass within 250.
				if (d < 250.0f && ratio > 1.3f && d < nearest_threat_d) {
					nearest_threat_d = d; threat_pos = om.core_pos; have_threat = true;
				}
				// Prey: unless aggressive mode, ignore > 1.5× us.
				bool can_target = (s.mode == 3) || (ratio <= 1.5f);
				if (can_target && d < best_prey_d) {
					best_prey_d = d; prey_pos = om.core_pos; have_prey = true;
				}
			}
			for (auto& f : world_.food) {
				float d = glm::length(f.pos - m.core_pos);
				if (d < best_food_d) { best_food_d = d; food_pos = f.pos; have_food = true; }
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
			} else if (s.mode == 2 && have_food && best_food_d < 300.0f) {
				target = food_pos;
				thrust = 0.6f;
				choice = 3;
			} else if (have_prey && best_prey_d < 600.0f) {
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

void App::drawEndScreen(float) {
	float aspect = window_.aspectRatio();
	text_->drawTitle(end_won_ ? "YOU SURVIVED" : "ELIMINATED",
		end_won_ ? -0.28f : -0.22f, 0.3f, 2.2f,
		end_won_ ? glm::vec4(0.5f, 1.0f, 0.5f, 1.0f)
		         : glm::vec4(1.0f, 0.4f, 0.4f, 1.0f),
		aspect);

	char line[160];
	const char* outcome_word = end_won_ ? "SURVIVED" : "DIED";
	std::snprintf(line, sizeof(line), "OUTCOME  %s", outcome_word);
	text_->drawText(line, -0.16f, 0.16f, 1.1f, glm::vec4(1,1,1,1), aspect);
	std::snprintf(line, sizeof(line), "BIOMASS  %.0f", end_biomass_);
	text_->drawText(line, -0.16f, 0.08f, 1.1f, glm::vec4(1,1,1,1), aspect);
	std::snprintf(line, sizeof(line), "KILLS    %d", end_kills_);
	text_->drawText(line, -0.16f, 0.00f, 1.1f, glm::vec4(1,1,1,1), aspect);
	std::snprintf(line, sizeof(line), "SURVIVED %d:%02d",
		(int)end_time_/60, (int)end_time_ % 60);
	text_->drawText(line, -0.16f, -0.08f, 1.1f, glm::vec4(1,1,1,1), aspect);
	if (!autotest_prebuilt_id_.empty()) {
		std::snprintf(line, sizeof(line), "SHAPE    %s", autotest_prebuilt_id_.c_str());
		text_->drawText(line, -0.16f, -0.16f, 1.0f,
			glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), aspect);
	}

	Button re { -0.30f, -0.28f, 0.28f, 0.10f, "REMATCH" };
	Button mm {  0.02f, -0.28f, 0.28f, 0.10f, "MENU" };
	if (drawButton(re)) goToMonsterSelect();
	if (drawButton(mm)) goToMainMenu();
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

} // namespace civcraft::lifecraft

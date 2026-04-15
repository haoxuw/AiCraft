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
#include "CellCraft/client/ui_button.h"
#include "CellCraft/client/ui_modern.h"
#include "CellCraft/client/ui_text.h"
#include "CellCraft/client/ui_theme.h"
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
	fill_renderer_ = std::make_unique<CellFillRenderer>();
	if (!fill_renderer_->init()) return false;
	text_ = std::make_unique<TextRenderer>();
	if (!text_->init("shaders")) return false;
	// Load TTF SDF fonts for modern UI (Inter + Audiowide). Failure is
	// non-fatal — ui_modern falls back to the bitmap renderer.
	ui::modern::initFonts("fonts", "shaders");
	post_fx_ = std::make_unique<PostFX>();
	if (!post_fx_->init()) {
		std::fprintf(stderr, "PostFX init failed — continuing without post-fx\n");
		post_fx_.reset();
	}
	ambient_.init(opts_.seed);
	bg_layer_.init(opts_.seed ^ 0xB6E7u);

	log_.open();
	prebuilts_ = monsters::getPrebuiltMonsters();
	lab_.init(&window_, renderer_.get(), fill_renderer_.get(), text_.get());
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
	} else if (!opts_.end_screenshot_path.empty()) {
		std::mt19937 rng(opts_.seed);
		auto t = monsters::makeStarter(monsters::StarterKind::SPIKY, rng);
		creature_name_ = generateName(rng);
		lab_.load_starter(t.cell, t.parts, t.color, creature_name_);
		// Fake metrics for the end-screen screenshot.
		end_kills_   = 3;
		end_biomass_ = 142.0f;
		end_time_    = 87.0f;
		end_won_     = false;  // "YOU DIED" path — more common outcome
		state_ = AppState::END_SCREEN;
	} else {
		state_ = AppState::LOADING;
	}
	return true;
}

void App::shutdown() {
	log_.close();
	ui::modern::shutdownFonts();
	if (post_fx_)  { post_fx_->shutdown();  post_fx_.reset(); }
	if (text_)     { text_->shutdown();     text_.reset(); }
	if (fill_renderer_) { fill_renderer_->shutdown(); fill_renderer_.reset(); }
	if (renderer_) { renderer_->shutdown(); renderer_.reset(); }
	window_.shutdown();
	g_app = nullptr;
}

void App::run() {
	// --- ui kitchen sink: skip all menus, render design-system demo, dump PPM, exit ---
	if (opts_.ui_kitchen_sink) {
		// Render a few frames so any post-fx settles, then snapshot.
		for (int frame = 0; frame < 3; ++frame) {
			window_.pollEvents();
			int w, h; glfwGetFramebufferSize(window_.handle(), &w, &h);
			glViewport(0, 0, w, h);
			// Don't draw the chalk board — the demo paints its own scrim.
			glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			drawUiKitchenSink();
			window_.swapBuffers();
		}
		writePPM("/tmp/cc_ui_kitchen.ppm");
		return;
	}

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
				ambient_.update(0.0f, fw, fh);
				bool use_fx = (bool)post_fx_;
				if (use_fx) post_fx_->begin(fw, fh);
				glViewport(0, 0, fw, fh);
				renderer_->drawBoard(fw, fh, (float)glfwGetTime());
				ambient_.draw(renderer_.get(), fw, fh);
				{ int pt = 1; if (const sim::Monster* pm = world_.get(player_id_)) pt = pm->tier;
					bg_layer_.setPlayerTier(pt, world_.map_radius);
					bg_layer_.update(0.0f, world_.map_radius);
					bg_layer_.draw(renderer_.get(), fw, fh,
						[this](glm::vec2 wp){ return worldToScreen(wp); }, camera_world_, (float)glfwGetTime(), fill_renderer_.get()); }
				drawFood();
				drawMonsters();
				updateAndDrawParticles(0.0f);
				drawHUD();
				if (use_fx) post_fx_->render_to_default(fw, fh, (float)glfwGetTime());
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
					ambient_.update(0.0f, fw, fh);
					bool use_fx2 = (bool)post_fx_;
					if (use_fx2) post_fx_->begin(fw, fh);
					glViewport(0, 0, fw, fh);
					renderer_->drawBoard(fw, fh, (float)glfwGetTime());
					ambient_.draw(renderer_.get(), fw, fh);
					{ int pt = 1; if (const sim::Monster* pm = world_.get(player_id_)) pt = pm->tier;
						bg_layer_.setPlayerTier(pt, world_.map_radius);
						bg_layer_.update(0.0f, world_.map_radius);
						bg_layer_.draw(renderer_.get(), fw, fh,
							[this](glm::vec2 wp){ return worldToScreen(wp); }, camera_world_, (float)glfwGetTime(), fill_renderer_.get()); }
					drawFood();
					drawMonsters();
					updateAndDrawParticles(0.0f);
					drawHUD();
					if (use_fx2) post_fx_->render_to_default(fw, fh, (float)glfwGetTime());
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
			shake_.update(dt);
			int w, h; glfwGetFramebufferSize(window_.handle(), &w, &h);
			glViewport(0, 0, w, h);
			ambient_.update(dt, w, h);
			bool use_fx = (bool)post_fx_;
			if (use_fx) post_fx_->begin(w, h);
			glViewport(0, 0, w, h);
			renderer_->drawBoard(w, h, (float)(glfwGetTime() - t0));
			ambient_.draw(renderer_.get(), w, h);
			{ int pt = 1; if (const sim::Monster* pm = world_.get(player_id_)) pt = pm->tier;
				bg_layer_.setPlayerTier(pt, world_.map_radius);
				bg_layer_.update(dt, world_.map_radius);
				bg_layer_.draw(renderer_.get(), w, h,
					[this](glm::vec2 wp){ return worldToScreen(wp); }, camera_world_, (float)glfwGetTime(), fill_renderer_.get()); }
			drawFood();
			drawMonsters();
			updateAndDrawParticles(dt);
			drawHUD();
			if (use_fx) post_fx_->render_to_default(w, h, (float)(glfwGetTime() - t0));
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
			state_time_ += dt; ui_frame_dt_ = dt;
			ui_fx_.tick(dt);
			int w, h; glfwGetFramebufferSize(window_.handle(), &w, &h);
			glViewport(0, 0, w, h);
			ambient_.update(dt, w, h);
			bool use_fx = (bool)post_fx_;
			if (use_fx) post_fx_->begin(w, h);
			glViewport(0, 0, w, h);
			renderer_->drawBoard(w, h, (float)glfwGetTime());
			ambient_.draw(renderer_.get(), w, h);
			switch (st) {
			case AppState::MAIN_MENU: drawMainMenu(dt); break;
			case AppState::STARTER:   drawStarter(dt);  break;
			case AppState::LAB:       drawLab(dt);      break;
			case AppState::CELEBRATE: drawCelebrate(dt); break;
			case AppState::END_SCREEN: drawEndScreen(dt); break;
			default: break;
			}
			if (use_fx) post_fx_->render_to_default(w, h, (float)glfwGetTime());
			ui_fx_.draw(text_.get());
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
	if (!opts_.end_screenshot_path.empty()) {
		run_one_shot(opts_.end_screenshot_path, AppState::END_SCREEN, 0.5);
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
		ui_frame_dt_ = dt;
		ui_fx_.tick(dt);
		updateTransition(dt);
		// Input blocked during the dark portion of the crossfade so you can't
		// click through a scene that's about to disappear.
		const bool input_blocked = scene_transition_alpha_ > 0.85f;
		if (input_blocked) {
			mouse_left_click_ = false;
			mouse_right_click_ = false;
			keys_pressed_this_frame_.clear();
		}

		int w, h; glfwGetFramebufferSize(window_.handle(), &w, &h);
		glViewport(0, 0, w, h);

		// Update low-HP vignette driver.
		if (post_fx_) {
			float low = 0.0f;
			if (state_ == AppState::PLAYING) {
				if (const sim::Monster* pm = world_.get(player_id_)) {
					float hp_frac = pm->hp_max > 0.0f ? pm->hp / pm->hp_max : 1.0f;
					if (hp_frac < 0.35f) low = (0.35f - hp_frac) / 0.35f;
				}
			}
			post_fx_->low_hp = low;
		}
		shake_.update(dt);
		ambient_.update(dt, w, h);

		// Scene pass → offscreen HDR buffer if PostFX available.
		bool use_fx = (bool)post_fx_;
		if (use_fx) post_fx_->begin(w, h);
		glViewport(0, 0, w, h);
		renderer_->drawBoard(w, h, (float)now);
		ambient_.draw(renderer_.get(), w, h);

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

		// Composite bloom + vignette + low-HP overlay to default FB.
		if (use_fx) post_fx_->render_to_default(w, h, (float)now);

		// UI particles: drawn AFTER composite so they always pop on top
		// (bloom won't eat them, and they read as crisp click feedback).
		ui_fx_.draw(text_.get());

		// Scene-transition fade overlay — drawn last so nothing else shines
		// through the black wash.
		drawTransitionOverlay();

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

// ---- Scene transitions -------------------------------------------------
// Simple fade-from-black reveal: on state change, snap alpha to 1.0; the
// next 400ms the overlay fades 1→0 via smoothstep. Input is suppressed
// while alpha is high enough to obscure the scene (>0.85).
void App::beginTransition(AppState target) {
	transition_target_ = target;
	transitioning_ = true;
	transition_t_ = 0.0f;
	scene_transition_alpha_ = 1.0f;
}

void App::updateTransition(float dt) {
	if (!transitioning_) return;
	const float DUR = 0.40f;
	transition_t_ += dt;
	float u = std::min(1.0f, transition_t_ / DUR);
	float s = u * u * (3.0f - 2.0f * u);  // smoothstep
	scene_transition_alpha_ = 1.0f - s;
	if (transition_t_ >= DUR) {
		scene_transition_alpha_ = 0.0f;
		transitioning_ = false;
	}
}

void App::drawTransitionOverlay() {
	if (scene_transition_alpha_ <= 0.001f) return;
	float a = std::min(1.0f, scene_transition_alpha_);
	text_->drawRect(-1.0f, -1.0f, 2.0f, 2.0f, glm::vec4(0.0f, 0.0f, 0.0f, a));
}

void App::goToMainMenu() {
	state_ = AppState::MAIN_MENU;
	state_time_ = 0.0f;
	initMenuSim();
	beginTransition(AppState::MAIN_MENU);
}
void App::startMatchWithTemplate(const monsters::MonsterTemplate& t) {
	world_ = sim::World{};
	world_.rng.seed(opts_.seed);
	world_.map_radius = 1500.0f;
	ai_states_.clear();
	floaters_.clear();
	match_time_ = 0.0f;
	match_start_time_ = (float)glfwGetTime();
	kills_ = 0;
	paused_ = false;
	autotest_prebuilt_id_ = t.id;

	sim::Monster p = monsters::makeMonsterFromTemplate(t, /*owner=*/1,
		glm::vec2(0.0f, 0.0f), 0.0f);
	// Give the player a modest starting biomass bonus so they aren't one-shot
	// by the first AI they bump — tuning choice for early-game feel.
	p.biomass *= 2.0f;
	// Optional: force a starting Tier so QA can screenshot the APEX background
	// without grinding. We bump lifetime_biomass past the requested tier's
	// threshold and scale the body by the matching size multiplier — same
	// path Sim takes on natural tier-ups.
	if (opts_.autotest_tier >= 1 && opts_.autotest_tier <= sim::TIER_COUNT) {
		int tt = opts_.autotest_tier;
		p.lifetime_biomass = sim::TIER_THRESHOLDS[tt] + 1.0f;
		p.tier             = tt;
		if (tt > 1) {
			p.scale_shape(sim::TIER_SIZE_MULTS[tt] / sim::TIER_SIZE_MULTS[1]);
			p.biomass = std::max(p.biomass, sim::TIER_THRESHOLDS[tt] + 1.0f);
		}
	}
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

	world_.scatter_food(16);

	sim_ = std::make_unique<sim::Sim>(&world_);
	state_ = AppState::PLAYING;
	beginTransition(AppState::PLAYING);
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
	beginTransition(AppState::END_SCREEN);
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
	glm::vec2 sh  = shake_.offset();
	return glm::vec2(rel.x + sh.x + w * 0.5f, -rel.y + sh.y + h * 0.5f);
}

bool App::pointInButton(glm::vec2 mouse_ndc, const Button& b) const {
	return mouse_ndc.x >= b.x && mouse_ndc.x <= b.x + b.w
	    && mouse_ndc.y >= b.y && mouse_ndc.y <= b.y + b.h;
}

bool App::drawButton(const Button& b) {
	glm::vec2 m = pxToNDC(mouse_px_);
	bool hover = pointInButton(m, b) && b.enabled;
	bool clicked = hover && mouse_left_click_;

	// Key by label for persistent anim state. Note: identical labels
	// across screens share state, which is fine for this UI.
	ui::AnimState& a = ui_anim_.get(std::string("btn:") + b.label);
	a.tick(ui_frame_dt_, hover, clicked);

	ui::PillButton pb;
	pb.x = b.x; pb.y = b.y; pb.w = b.w; pb.h = b.h;
	pb.label = b.label; pb.enabled = b.enabled; pb.hovered = hover;
	pb.label_scale = 1.1f;

	// Semantic role by label.
	if (b.label == "LET'S GO!" || b.label == "PLAY AGAIN!") {
		pb.top = ui::BTN_GO_TOP; pb.bottom = ui::BTN_GO_BOTTOM;
		pb.label_fill = ui::TEXT_LIGHT;
	} else if (b.label == "QUIT" || b.label == "MENU" || b.label == "BACK") {
		pb.top = ui::BTN_NEUTRAL_TOP; pb.bottom = ui::BTN_NEUTRAL_BOTTOM;
		pb.label_fill = ui::TEXT_DARK;
	} else {
		pb.top = ui::BTN_PRIMARY_TOP; pb.bottom = ui::BTN_PRIMARY_BOTTOM;
		pb.label_fill = ui::TEXT_LIGHT;
	}

	// Idle bobble on the big call-to-action buttons.
	bool bobble = (b.label == "LET'S GO!" || b.label == "PLAY AGAIN!" ||
	               b.label == "PLAY");
	pb.scale = a.scale(bobble);
	float dy = a.idle_offset(bobble);
	pb.y += dy;

	ui::drawPill(text_.get(), pb, window_.aspectRatio());

	// Sparkle burst on click — centered on the button in the button's
	// accent color.
	if (clicked) {
		glm::vec2 cen(b.x + b.w * 0.5f, b.y + b.h * 0.5f + a.idle_offset(bobble));
		glm::vec3 spk(pb.top.r, pb.top.g, pb.top.b);
		ui_fx_.sparkle(cen, spk, ai_rng_, 9);
	}
	return clicked;
}

// ========================================================================
// LOADING
// ========================================================================

void App::drawLoading(float) {
	glm::vec4 shadow = ui::ACCENT_GOLD_WARM; shadow.a = 0.8f;
	ui::drawOutlinedTitle(text_.get(), "CELLCRAFT", -0.26f, 0.05f, 3.0f,
	                      ui::TEXT_DARK, shadow, window_.aspectRatio());

	float total = 0.8f;
	float frac = std::min(1.0f, state_time_ / total);
	float bw = 0.5f, bh = 0.02f;
	float bx = -bw * 0.5f, by = -0.12f;
	text_->drawRect(bx, by, bw, bh, glm::vec4(0.85f, 0.82f, 0.76f, 0.9f));
	text_->drawRect(bx, by, bw * frac, bh, ui::ACCENT_GOLD_WARM);
	text_->drawText("LOADING...", -0.08f, -0.2f, 0.9f, ui::TEXT_MUTED, window_.aspectRatio());

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

	// Keep the screensaver sim ticking so returning to gameplay feels alive,
	// but DON'T draw the cream creatures — the modern menu is a dark canvas.
	stepMenuSim(dt);

	float aspect = window_.aspectRatio();
	namespace m = ui::modern;
	int W, H; glfwGetFramebufferSize(window_.handle(), &W, &H);
	m::beginFrame(text_.get(), W, H);

	// 1) Dark charcoal gradient scrim — wipes out the chalkboard + cream
	//    ambient motes that the outer loop already drew.
	m::drawScrim(0, 0, W, H, m::SURFACE_BG_TOP, m::SURFACE_BG_BOTTOM);

	// 2) Subtle cyan-tinted ambient dots drifting across the dark canvas.
	//    Deterministic per-frame via a menu-local RNG so the build is cheap
	//    without touching the shared AmbientParticles.
	{
		static unsigned rs = 0xCAFEC01Du;
		static float motes[60][2];   // x, y in pixels
		static float mvel[60][2];
		static bool  seeded = false;
		auto r01 = [&]() {
			rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
			return (rs & 0xFFFFFF) / float(0x1000000);
		};
		if (!seeded) {
			for (int i = 0; i < 60; ++i) {
				motes[i][0] = r01() * W;
				motes[i][1] = r01() * H;
				float a = r01() * 6.28318f;
				float sp = 6.0f + r01() * 10.0f;
				mvel[i][0] = std::cos(a) * sp;
				mvel[i][1] = std::sin(a) * sp;
			}
			seeded = true;
		}
		glm::vec4 dot = m::ACCENT_CYAN; dot.a = 0.30f;
		for (int i = 0; i < 60; ++i) {
			motes[i][0] += mvel[i][0] * dt;
			motes[i][1] += mvel[i][1] * dt;
			if (motes[i][0] < -4)  motes[i][0] = (float)W + 4;
			if (motes[i][0] > W+4) motes[i][0] = -4;
			if (motes[i][1] < -4)  motes[i][1] = (float)H + 4;
			if (motes[i][1] > H+4) motes[i][1] = -4;
			int x = (int)motes[i][0], y = (int)motes[i][1];
			m::drawRoundedRect(x, y, 2, 2, 1, dot);
		}
	}

	// 3) Centered content column, ~560px wide.
	const int COL_W = 560;
	int col_x = (W - COL_W) / 2;
	// Rough block height for vertical centering: title(~150) + divider gap
	// + tagline(~28) + gap + 3 buttons * 60 + 2 gaps * 12 = ~420.
	const int BLOCK_H = 420;
	int col_y = (H - BLOCK_H) / 2;

	// Title row — keep the chalk identity. Rendered via drawOutlinedTitle in
	// NDC space. Original menu used (-0.26, 0.30, scale=3.0); those numbers
	// produce a visually-centered "CELLCRAFT" so we reuse them but with a
	// cyan-glow shadow instead of gold to sit on the dark scrim.
	{
		float title_scale = 3.0f;
		float tx = -0.26f;
		// Title baseline in NDC — place it above the button block.
		// Buttons sit at col_y + 210px; we want the title centered roughly
		// 120 px above, converted to NDC offset.
		int title_py = col_y + 20;
		// drawOutlinedTitle expects a baseline-ish y in NDC (y-up). Convert
		// pixel-top + a font-cap offset.
		float ty = 1.0f - (float)(title_py + 60) * 2.0f / (float)H;
		glm::vec4 shadow = m::ACCENT_CYAN_DEEP; shadow.a = 0.70f;
		ui::drawOutlinedTitle(text_.get(), "CELLCRAFT", tx, ty, title_scale,
			glm::vec4(0.96f, 0.98f, 1.0f, 1.0f), shadow, aspect);
	}

	// Thin cyan divider under the title.
	int div_y = col_y + 175;
	m::drawDivider(col_x + COL_W / 2 - 80, div_y, 160,
		m::DividerAxis::HORIZONTAL, m::ACCENT_CYAN);

	// Tagline in modern label style.
	{
		int tag_y = div_y + m::SPACE_MD;
		int tw = m::measureTextPx("SCRIBBLE SURVIVAL", m::TYPE_LABEL);
		m::drawTextLabel(col_x + (COL_W - tw) / 2, tag_y,
			"SCRIBBLE SURVIVAL", m::TEXT_SECONDARY);
	}

	// Buttons stack.
	glm::vec2 mouse_fb = mouse_px_;
	{
		int ww, wh; glfwGetWindowSize(window_.handle(), &ww, &wh);
		if (ww > 0 && wh > 0) {
			mouse_fb.x = mouse_px_.x * (float)W / (float)ww;
			mouse_fb.y = mouse_px_.y * (float)H / (float)wh;
		}
	}
	auto in_rect = [&](int x, int y, int w, int h) {
		return mouse_fb.x >= x && mouse_fb.x <= x + w
		    && mouse_fb.y >= y && mouse_fb.y <= y + h;
	};

	int btn_y  = col_y + 210;
	const int BTN_H_LG  = 60;
	const int BTN_H_SM  = 48;
	const int GAP       = 12;

	// PLAY (primary)
	{
		bool hov = in_rect(col_x, btn_y, COL_W, BTN_H_LG);
		bool prs = hov && mouse_left_down_;
		bool clk = hov && mouse_left_click_;
		m::buttonPrimary(col_x, btn_y, COL_W, BTN_H_LG, "PLAY", hov, prs);
		if (clk) goToStarter();
	}
	btn_y += BTN_H_LG + GAP;

	// OPTIONS (ghost, disabled-ish — no-op for now but visually present)
	{
		bool hov = in_rect(col_x, btn_y, COL_W, BTN_H_SM);
		bool prs = hov && mouse_left_down_;
		m::buttonGhost(col_x, btn_y, COL_W, BTN_H_SM, "OPTIONS", hov, prs);
		// No action wired yet — commit 2 is visual reskin only.
	}
	btn_y += BTN_H_SM + GAP;

	// QUIT (ghost)
	{
		bool hov = in_rect(col_x, btn_y, COL_W, BTN_H_SM);
		bool prs = hov && mouse_left_down_;
		bool clk = hov && mouse_left_click_;
		m::buttonGhost(col_x, btn_y, COL_W, BTN_H_SM, "QUIT", hov, prs);
		if (clk) glfwSetWindowShouldClose(window_.handle(), 1);
	}

	// Footer version string, bottom-center.
	{
		const std::string v = "v0.1  \xB7  CHALK ON INK";
		int tw = m::measureTextPx(v, m::TYPE_CAPTION);
		m::drawTextModern((W - tw) / 2, H - 28, v, m::TYPE_CAPTION,
			m::TEXT_MUTED);
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
			// Kick the camera a bit if the player is the killer OR target.
			if (e.actor == player_id_ || e.target == player_id_) {
				shake_.add(e.target == player_id_ ? 9.0f : 6.0f, 0.15f);
			} else {
				shake_.add(2.5f, 0.10f);
			}
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
		case sim::EventKind::TIER_UP: {
			int nt = (int)e.amount;
			std::snprintf(buf, sizeof(buf), "#%u → tier %d (%s)",
				e.actor, nt, sim::tierName(nt));
			log_.write("TIER_UP", buf);
			// Sparkle ring at the creature's position for a silent celebration.
			glm::vec3 col(1.0f, 0.9f, 0.4f);
			if (const sim::Monster* am = world_.get(e.actor)) col = am->color;
			emitKillParticles(e.pos, col);
			if (e.actor == player_id_) {
				pushFloating(std::string("TIER ") + std::to_string(nt) + "!",
					glm::vec3(1.0f, 0.9f, 0.3f));
			}
			break;
		}
		case sim::EventKind::POISON_HIT:
		case sim::EventKind::VENOM_HIT:
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

	// Phase 1.1: organic body fill pass — runs BEFORE the chalk outline so
	// the hand-drawn edge draws cleanly on top. Each cell is fan-triangulated
	// from its centroid with a membrane→cytoplasm radial gradient.
	{
		std::vector<glm::vec2> poly_px;
		float t_now = (float)glfwGetTime();
		for (auto& [id, m] : world_.monsters) {
			if (!m.alive) continue;
			auto wp = sim::transform_to_world(m.shape, m.core_pos, m.heading);
			poly_px.clear();
			poly_px.reserve(wp.size());
			for (auto& v : wp) poly_px.push_back(worldToScreen(v));
			fill_renderer_->drawFill(poly_px, m.color, m.part_effect.diet,
				static_cast<float>(id), t_now, w, h);
		}
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
	strokes.reserve(world_.food.size() * 220);

	const float TWO_PI = 6.28318530718f;
	const float t = (float)glfwGetTime();

	// Green palette: two shades in the same family so MEAT vs PLANT still
	// reads at a glance, but the board no longer has competing hues.
	const glm::vec3 PLANT_GREEN = {0.420f, 0.796f, 0.310f};  // bright chartreuse
	const glm::vec3 MEAT_GREEN  = {0.180f, 0.560f, 0.270f};  // deeper forest green

	auto hash01 = [](uint32_t s) {
		s ^= s >> 16; s *= 0x7feb352dU;
		s ^= s >> 15; s *= 0x846ca68bU;
		s ^= s >> 16;
		return float(s & 0xFFFFFF) / float(0x1000000);
	};

	// Bevel colors — darker shadow + primary body + bright highlight gives a
	// chunky "extruded glass" read without needing a real 3D shader. Each arm
	// is drawn three times (shadow offset, body, highlight offset).
	const glm::vec3 PLANT_SHADOW   = {0.180f, 0.420f, 0.140f};
	const glm::vec3 PLANT_HIGHLITE = {0.780f, 0.960f, 0.650f};
	const glm::vec3 MEAT_SHADOW    = {0.070f, 0.280f, 0.130f};
	const glm::vec3 MEAT_HIGHLITE  = {0.560f, 0.870f, 0.520f};
	// Pure white specular so the "glass" reads as catching light.
	const glm::vec3 SPECULAR       = {0.98f, 1.00f, 0.92f};

	for (auto& f : world_.food) {
		glm::vec2 sp = worldToScreen(f.pos);
		if (sp.x < -80.0f || sp.x > w + 80.0f) continue;
		if (sp.y < -80.0f || sp.y > h + 80.0f) continue;

		float bm = std::max(0.0f, f.biomass);
		float bm_t  = std::min(1.0f, std::max(0.0f, (bm - 4.0f) / 11.0f));
		// 2.2× larger overall → flakes are a real focal point.
		float base_r = 30.0f + bm_t * 16.0f;
		float phase  = hash01(f.seed) * TWO_PI;

		float spin_sign = (hash01(f.seed ^ 0x13572468u) < 0.5f) ? -1.0f : 1.0f;
		float spin = spin_sign * t * 0.35f + phase;

		const bool is_meat = (f.type == sim::FoodType::MEAT);
		const glm::vec3 col_body = is_meat ? MEAT_GREEN     : PLANT_GREEN;
		const glm::vec3 col_shad = is_meat ? MEAT_SHADOW    : PLANT_SHADOW;
		const glm::vec3 col_high = is_meat ? MEAT_HIGHLITE  : PLANT_HIGHLITE;
		// Thick body; shadow wider underneath; highlight thin on top.
		const float hw_body = is_meat ? 4.2f : 3.6f;
		const float hw_shad = hw_body * 1.55f;
		const float hw_high = hw_body * 0.50f;
		const int   arms    = 6;

		// Light from upper-left: shadow falls down-right, highlight up-left.
		// Offsets scale with flake size so bevel depth stays proportional.
		const float bevel = base_r * 0.10f;
		const glm::vec2 shad_off( bevel,  bevel);
		const glm::vec2 high_off(-bevel * 0.55f, -bevel * 0.55f);

		auto rot = [&](float lx, float ly, glm::vec2 off = glm::vec2(0.0f)) {
			float c = std::cos(spin), s = std::sin(spin);
			return glm::vec2(sp.x + off.x + c * lx - s * ly,
			                 sp.y + off.y + s * lx + c * ly);
		};

		// Build a pass of strokes at a given offset, color, and width.
		// Three passes = shadow (behind), body, highlight (on top).
		// Geometry: filled hexagonal core + 6 radial arms, each with primary
		// V-branch, secondary V-branch, trident tip, and chevrons along the
		// shaft. Inner "star of David" overlay connects alternating arms.
		auto buildFlake = [&](glm::vec2 off, glm::vec3 col, float hw, float scale) {
			// --- Filled hexagonal core (disc + outline). The disc is a
			// wide-ribbon short segment; fully opaque from the chalk pipeline's
			// perspective. Outline is drawn on top for a crisp edge.
			{
				float hr = base_r * 0.38f * scale;
				// Filled disk: a 2-point segment with huge half-width renders as
				// a solid cap-to-cap capsule. Make it essentially a point.
				ChalkStroke fill;
				fill.color = col;
				fill.half_width = hr * 1.05f;
				glm::vec2 c = rot(0.0f, 0.0f, off);
				fill.points = { {c.x - 0.5f, c.y - 0.5f}, {c.x + 0.5f, c.y + 0.5f} };
				strokes.push_back(std::move(fill));

				// Crisp hex outline.
				ChalkStroke hex;
				hex.color = col;
				hex.half_width = hw;
				hex.points.reserve(arms + 1);
				for (int i = 0; i <= arms; ++i) {
					float a = TWO_PI * float(i) / float(arms);
					hex.points.push_back(rot(std::cos(a) * hr, std::sin(a) * hr, off));
				}
				strokes.push_back(std::move(hex));
			}

			// --- Star of David (two overlapping triangles) at mid radius.
			// Adds the recognizable "dense snowflake core" look.
			{
				const float sr = base_r * 0.58f * scale;
				for (int pass = 0; pass < 2; ++pass) {
					ChalkStroke tri;
					tri.color = col;
					tri.half_width = hw * 0.85f;
					float a0 = (pass == 0) ? 0.0f : (TWO_PI / 6.0f);
					tri.points.reserve(4);
					for (int i = 0; i <= 3; ++i) {
						float a = a0 + TWO_PI * float(i % 3) / 3.0f;
						tri.points.push_back(rot(std::cos(a) * sr, std::sin(a) * sr, off));
					}
					strokes.push_back(std::move(tri));
				}
			}

			const float arm_len  = base_r * scale;
			const float branch_r = base_r * 0.36f * scale;
			for (int i = 0; i < arms; ++i) {
				float a = TWO_PI * float(i) / float(arms);
				float cx = std::cos(a), cy = std::sin(a);
				float nx = -cy, ny = cx;

				// Main arm — thicker than before for a heavier body.
				ChalkStroke arm;
				arm.color = col;
				arm.half_width = hw * 1.15f;
				arm.points.push_back(rot(0.0f, 0.0f, off));
				arm.points.push_back(rot(cx * arm_len, cy * arm_len, off));
				strokes.push_back(std::move(arm));

				// Primary V-branch at 40% along the arm.
				{
					float u = 0.40f;
					float bx = cx * arm_len * u;
					float by = cy * arm_len * u;
					for (int s = -1; s <= 1; s += 2) {
						ChalkStroke br;
						br.color = col;
						br.half_width = hw * 0.95f;
						br.points.push_back(rot(bx, by, off));
						br.points.push_back(rot(
							bx + (cx * 0.55f + nx * 0.95f * s) * branch_r,
							by + (cy * 0.55f + ny * 0.95f * s) * branch_r, off));
						strokes.push_back(std::move(br));
					}
				}

				// Secondary V-branch at 65% — longer, heavier, signature detail.
				{
					float u = 0.65f;
					float bx = cx * arm_len * u;
					float by = cy * arm_len * u;
					float br_len = branch_r * 1.15f;
					for (int s = -1; s <= 1; s += 2) {
						ChalkStroke br;
						br.color = col;
						br.half_width = hw * 1.0f;
						glm::vec2 p0 = rot(bx, by, off);
						glm::vec2 p1 = rot(bx + (cx * 0.45f + nx * 1.0f * s) * br_len,
						                   by + (cy * 0.45f + ny * 1.0f * s) * br_len, off);
						br.points = {p0, p1};
						strokes.push_back(std::move(br));

						// Tertiary mini-fork on the secondary branch.
						glm::vec2 mid = (p0 + p1) * 0.5f;
						glm::vec2 tip = p1;
						glm::vec2 dir = tip - mid;
						glm::vec2 perp(-dir.y, dir.x);
						float tl = 0.55f;
						ChalkStroke t1, t2;
						t1.color = t2.color = col;
						t1.half_width = t2.half_width = hw * 0.75f;
						t1.points = { tip, tip + dir * 0.0f + perp * tl - dir * 0.25f };
						t2.points = { tip, tip + dir * 0.0f - perp * tl - dir * 0.25f };
						strokes.push_back(std::move(t1));
						strokes.push_back(std::move(t2));
					}
				}

				// Chevron along the shaft — small perpendicular ticks at 25% / 55%.
				for (float u : {0.25f, 0.55f}) {
					float bx = cx * arm_len * u;
					float by = cy * arm_len * u;
					ChalkStroke ch;
					ch.color = col;
					ch.half_width = hw * 0.65f;
					float tl = base_r * 0.09f * scale;
					ch.points = {
						rot(bx + nx * tl, by + ny * tl, off),
						rot(bx - nx * tl, by - ny * tl, off),
					};
					strokes.push_back(std::move(ch));
				}

				// Trident tip — center point + two splayed points at 45°.
				{
					glm::vec2 tip  = rot(cx * arm_len,        cy * arm_len,        off);
					glm::vec2 mid1 = rot(cx * arm_len * 0.78f + nx * base_r * 0.22f * scale,
					                     cy * arm_len * 0.78f + ny * base_r * 0.22f * scale, off);
					glm::vec2 mid2 = rot(cx * arm_len * 0.78f - nx * base_r * 0.22f * scale,
					                     cy * arm_len * 0.78f - ny * base_r * 0.22f * scale, off);
					glm::vec2 base = rot(cx * arm_len * 0.60f,
					                     cy * arm_len * 0.60f, off);
					ChalkStroke dia;
					dia.color = col;
					dia.half_width = hw * 0.95f;
					dia.points = {base, mid1, tip, mid2, base};
					strokes.push_back(std::move(dia));

					// Filled wedge at the very tip for a beefier termination.
					ChalkStroke cap;
					cap.color = col;
					cap.half_width = base_r * 0.08f * scale;
					glm::vec2 tip_in = rot(cx * arm_len * 0.92f, cy * arm_len * 0.92f, off);
					cap.points = {tip_in, tip};
					strokes.push_back(std::move(cap));
				}
			}

			// Rotated inner ring of shorter arms (30° offset) — now always on,
			// not just for MEAT, so PLANT also reads as a complex flake.
			{
				const float inner_len = base_r * 0.50f * scale;
				for (int i = 0; i < arms; ++i) {
					float a = TWO_PI * (float(i) + 0.5f) / float(arms);
					float cx = std::cos(a), cy = std::sin(a);
					float nx = -cy, ny = cx;
					ChalkStroke arm;
					arm.color = col;
					arm.half_width = hw * 0.85f;
					arm.points.push_back(rot(0.0f, 0.0f, off));
					arm.points.push_back(rot(cx * inner_len, cy * inner_len, off));
					strokes.push_back(std::move(arm));

					// Tiny cross-tick on each inner arm.
					float tl = base_r * 0.08f * scale;
					ChalkStroke cross;
					cross.color = col;
					cross.half_width = hw * 0.55f;
					float u = 0.75f;
					float bx = cx * inner_len * u;
					float by = cy * inner_len * u;
					cross.points = {
						rot(bx + nx * tl, by + ny * tl, off),
						rot(bx - nx * tl, by - ny * tl, off),
					};
					strokes.push_back(std::move(cross));
				}
			}

			// MEAT: extra outer dodecagonal ring connecting arm tips for a
			// heavier, "crystallized" look.
			if (is_meat) {
				ChalkStroke ring;
				ring.color = col;
				ring.half_width = hw * 0.70f;
				const int K = 12;
				float rr = base_r * 0.88f * scale;
				ring.points.reserve(K + 1);
				for (int i = 0; i <= K; ++i) {
					float a = TWO_PI * float(i) / float(K);
					ring.points.push_back(rot(std::cos(a) * rr, std::sin(a) * rr, off));
				}
				strokes.push_back(std::move(ring));
			}
		};

		// 1) Shadow pass: thick, dark, offset down-right.
		buildFlake(shad_off, col_shad, hw_shad, 1.0f);
		// 2) Body pass: primary color at normal width.
		buildFlake(glm::vec2(0.0f), col_body, hw_body, 1.0f);
		// 3) Highlight pass: thin bright line, offset up-left, slightly shorter
		//    so it sits just inside the body edge → reads as lit top surface.
		buildFlake(high_off, col_high, hw_high, 0.92f);

		// 4) Specular "sparkle" dot near the upper-left, just off center, to
		//    sell the "glass caught the light" look.
		{
			ChalkStroke sp_dot;
			sp_dot.color = SPECULAR;
			sp_dot.half_width = base_r * 0.14f;
			glm::vec2 c = rot(-base_r * 0.16f, -base_r * 0.16f);
			sp_dot.points = { {c.x - 0.4f, c.y - 0.4f}, {c.x + 0.4f, c.y + 0.4f} };
			strokes.push_back(std::move(sp_dot));
		}
	}
	renderer_->drawStrokes(strokes, nullptr, w, h);
}

void App::drawHUD() {
	float aspect = window_.aspectRatio();
	sim::Monster* p = world_.get(player_id_);

	// ================================================================
	// Modern HUD — glass overlays over the chalk playfield.
	// Keep countdown/paused/floaters as chalk-era titles (diegetic),
	// but reskin the data readouts with ui_modern primitives.
	// ================================================================
	namespace m = ui::modern;
	int W, H; glfwGetFramebufferSize(window_.handle(), &W, &H);
	m::beginFrame(text_.get(), W, H);

	const int pad = m::SPACE_XL;           // 24 px
	const int tier = p ? p->tier : 1;
	const float biomass       = p ? p->biomass : 0.0f;
	const float lifetime_bm   = p ? p->lifetime_biomass : 0.0f;
	const float hp_frac       = p ? (p->hp / std::max(1.0f, p->hp_max)) : 0.0f;
	const bool  low_hp        = p && hp_frac < 0.30f;

	// Tier accent: cyan for low tiers, amber for mid, amber+glow for APEX.
	glm::vec4 tier_accent = (tier >= 3) ? m::ACCENT_AMBER : m::ACCENT_CYAN;

	// -------------------------- Top-left glass bar -------------------
	{
		const int bx = pad, by = pad;
		const int bw = 360, bh = 64;
		m::drawSoftShadow(bx, by, bw, bh, m::RADIUS_LG, 16, 0.30f);
		m::drawGlassPanel(bx, by, bw, bh, m::RADIUS_LG);

		int cx = bx + m::SPACE_LG;
		int cy_mid = by + bh / 2;

		// Tier pill badge — pulse red at low HP as a warning.
		char tier_label[48];
		std::snprintf(tier_label, sizeof(tier_label), "T%d - %s",
			tier, sim::tierName(tier));
		glm::vec4 pill_accent = tier_accent;
		glm::vec4 pill_bg     = m::SURFACE_PANEL_HI;
		glm::vec4 pill_fg     = m::TEXT_PRIMARY;
		if (low_hp) {
			float pulse = 0.5f + 0.5f * std::sin((float)glfwGetTime() * 6.0f);
			pill_accent = m::ACCENT_DANGER;
			pill_bg     = glm::vec4(0.30f + 0.10f * pulse, 0.08f, 0.08f, 0.85f);
		}
		m::drawPillBadge(cx, cy_mid - (m::TYPE_LABEL + 12) / 2,
			tier_label, pill_fg, pill_bg, pill_accent);
		// Advance cx by a stable width so layout doesn't shift with text.
		int pill_w = m::measureTextPx(tier_label, m::TYPE_LABEL) + m::SPACE_LG * 2;
		cx += pill_w + m::SPACE_LG;

		// Divider.
		m::drawDivider(cx, by + m::SPACE_SM, bh - m::SPACE_SM * 2,
			m::DividerAxis::VERTICAL, m::STROKE_SUBTLE);
		cx += m::SPACE_LG;

		// Biomass block: icon + big number + label.
		m::drawTextModern(cx, by + 10, "o", m::TYPE_TITLE_SM,
			m::ACCENT_SUCCESS, m::Align::LEFT);
		char bm_num[24]; std::snprintf(bm_num, sizeof(bm_num), "%.0f", biomass);
		m::drawTextModern(cx + 18, by + 10, bm_num, m::TYPE_TITLE_SM,
			m::TEXT_PRIMARY, m::Align::LEFT);
		m::drawTextLabel (cx, by + bh - m::TYPE_LABEL - 8,
			"BIOMASS", m::TEXT_SECONDARY);
		cx += 110;

		m::drawDivider(cx, by + m::SPACE_SM, bh - m::SPACE_SM * 2,
			m::DividerAxis::VERTICAL, m::STROKE_SUBTLE);
		cx += m::SPACE_LG;

		// Match timer: "()" clock fallback + MM:SS.
		m::drawTextModern(cx, by + 10, "()", m::TYPE_TITLE_SM,
			m::ACCENT_CYAN_GLOW, m::Align::LEFT);
		char t_num[16];
		int secs = (int)match_time_;
		std::snprintf(t_num, sizeof(t_num), "%d:%02d", secs / 60, secs % 60);
		m::drawTextModern(cx + 28, by + 10, t_num, m::TYPE_TITLE_SM,
			m::TEXT_PRIMARY, m::Align::LEFT);
		m::drawTextLabel (cx, by + bh - m::TYPE_LABEL - 8,
			"TIME", m::TEXT_SECONDARY);
	}

	// -------------------------- Top-right glass card -----------------
	{
		const int cw = 280, ch = 96;
		const int cx = W - pad - cw, cy = pad;
		m::drawSoftShadow(cx, cy, cw, ch, m::RADIUS_LG, 16, 0.30f);
		m::drawGlassPanel(cx, cy, cw, ch, m::RADIUS_LG);

		const bool at_apex = tier >= sim::TIER_COUNT;
		const int ring_cx = cx + 8 + 36;
		const int ring_cy = cy + ch / 2;
		if (at_apex) {
			// Amber "APEX" pill replaces the ring.
			m::drawPillBadge(cx + 16, cy + ch / 2 - 10,
				"APEX", m::TEXT_ON_ACCENT, m::ACCENT_AMBER, m::ACCENT_AMBER_DEEP);
			m::drawTextLabel(cx + 110, cy + 18,
				"MAX TIER REACHED", m::TEXT_SECONDARY);
			m::drawTextModern(cx + 110, cy + 36, "FULL GROWN",
				m::TYPE_BODY, m::TEXT_PRIMARY);
		} else {
			const float next_thresh = sim::TIER_THRESHOLDS[tier + 1];
			const float prev_thresh = sim::TIER_THRESHOLDS[tier];
			float frac = (lifetime_bm - prev_thresh)
				/ std::max(1.0f, next_thresh - prev_thresh);
			if (frac < 0.0f) frac = 0.0f;
			if (frac > 1.0f) frac = 1.0f;
			m::drawRingProgress(ring_cx, ring_cy, 32, 6, frac,
				tier_accent, m::TRACK_BG);
			char pct[8]; std::snprintf(pct, sizeof(pct), "%d%%",
				(int)(frac * 100.0f + 0.5f));
			m::drawTextModern(ring_cx, ring_cy - m::TYPE_TITLE_SM / 2 + 2,
				pct, m::TYPE_TITLE_SM, m::TEXT_PRIMARY, m::Align::CENTER);

			int tx = cx + 90;
			m::drawTextLabel(tx, cy + 14, "TO NEXT TIER", m::TEXT_SECONDARY);
			m::drawTextModern(tx, cy + 14 + m::TYPE_LABEL + 6,
				sim::tierName(tier + 1), m::TYPE_BODY, m::TEXT_PRIMARY);
			char rem[32];
			float left = std::max(0.0f, next_thresh - lifetime_bm);
			std::snprintf(rem, sizeof(rem), "%.0f left", left);
			m::drawTextModern(tx, cy + 14 + m::TYPE_LABEL + 6 + m::TYPE_BODY + 4,
				rem, m::TYPE_CAPTION, m::TEXT_SECONDARY);
		}
	}

	// -------------------------- Bottom-center hint -------------------
	{
		const bool apex_full = (tier >= sim::TIER_COUNT)
			&& biomass >= sim::TIER_THRESHOLDS[sim::TIER_COUNT];
		if (!apex_full) {
			const char* hint = "WASD MOVE  -  EAT FOOD  -  GROW";
			glm::vec4 hint_bg = m::SURFACE_PANEL;
			glm::vec4 hint_fg = m::TEXT_SECONDARY;
			glm::vec4 hint_accent = glm::vec4(0.0f);
			if (low_hp) {
				hint = "HP LOW - FIND FOOD";
				hint_fg = m::TEXT_PRIMARY;
				hint_accent = m::ACCENT_DANGER;
			}
			int tw = m::measureTextPx(hint, m::TYPE_LABEL);
			int pw = tw + m::SPACE_LG * 2;
			int ph = 28;
			int px = W / 2 - pw / 2;
			int py = H - pad - ph;
			m::drawRoundedRect(px, py, pw, ph, m::RADIUS_PILL,
				hint_bg, m::STROKE_SUBTLE, 1);
			m::drawTextLabel(W / 2 - tw / 2, py + (ph - m::TYPE_LABEL) / 2 + 2,
				hint, hint_fg);
			if (hint_accent.a > 0.0f) {
				m::drawRoundedRect(px, py + ph - 3, pw, 2, m::RADIUS_PILL,
					hint_accent);
			}
		}
	}

	// -------------------------- Bottom-right icon stack --------------
	{
		const int bsz = 40;
		int bx = W - pad - bsz;
		int by = H - pad - bsz;
		m::buttonIcon(bx, by, bsz, "F2", "Screenshot", false, false);
		by -= bsz + m::SPACE_SM;
		m::buttonIcon(bx, by, bsz, "M", "Mute", false, false);
		by -= bsz + m::SPACE_SM;
		m::buttonIcon(bx, by, bsz, "II", "Pause", false, false);
	}

	// -------------------------- Countdown + paused (diegetic) --------
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

	// Floating events bottom-left (kept in the chalk style — they're
	// transient combat flavour and read better as chalk text).
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
	text_->drawRect(mm_x, mm_y, mm_size_ndc, mm_size_ndc, ui::CARD_FILL);
	// Minimap border — gold accent matches card style.
	{
		glm::vec4 bc = ui::ACCENT_GOLD_WARM;
		float t = 0.003f;
		text_->drawRect(mm_x, mm_y, mm_size_ndc, t, bc);
		text_->drawRect(mm_x, mm_y + mm_size_ndc - t, mm_size_ndc, t, bc);
		text_->drawRect(mm_x, mm_y, t, mm_size_ndc, bc);
		text_->drawRect(mm_x + mm_size_ndc - t, mm_y, t, mm_size_ndc, bc);
	}
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
	// Background silhouettes — draw between board (already painted by main
	// loop) and gameplay. Reads player Tier so the "looming" set scales with
	// progression: bigger cells at low tiers, fish/turtles/jellies at APEX.
	int w, h; glfwGetFramebufferSize(window_.handle(), &w, &h);
	int player_tier = 1;
	if (const sim::Monster* pm = world_.get(player_id_)) player_tier = pm->tier;
	bg_layer_.setPlayerTier(player_tier, world_.map_radius);
	bg_layer_.update(dt, world_.map_radius);
	bg_layer_.draw(renderer_.get(), w, h,
		[this](glm::vec2 wp){ return worldToScreen(wp); },
		camera_world_, (float)glfwGetTime(), fill_renderer_.get());
	drawFood();
	drawMonsters();
	drawHUD();
}

// ========================================================================
// END_SCREEN
// ========================================================================

void App::drawEndScreen(float dt) {
	(void)dt;
	namespace m = ui::modern;
	int W, H; glfwGetFramebufferSize(window_.handle(), &W, &H);
	m::beginFrame(text_.get(), W, H);

	// Dark scrim.
	m::drawScrim(0, 0, W, H, m::SURFACE_BG_TOP, m::SURFACE_BG_BOTTOM);

	// Big title — red for death, amber for apex. Real Audiowide + neon glow.
	{
		const char* title = end_won_ ? "APEX REACHED" : "YOU DIED";
		glm::vec4 col = end_won_ ? m::ACCENT_AMBER : m::ACCENT_DANGER;
		m::drawTextRole(W / 2, 80, title, m::Role::HERO_NEON, col,
			m::Align::CENTER);
	}

	// Center card with the 4 stat rings.
	const int CARD_W = 760, CARD_H = 320;
	int card_x = (W - CARD_W) / 2;
	int card_y = (H - CARD_H) / 2 + 20;
	m::drawSoftShadow(card_x, card_y + 10, CARD_W, CARD_H, m::RADIUS_LG, 28, 0.45f);
	m::drawGlassPanel(card_x, card_y, CARD_W, CARD_H, m::RADIUS_LG);

	// 4 ring-style stat slots.
	const char* labels[4] = { "KILLS", "BIOMASS", "TIER", "TIME" };
	char v0[16], v1[16], v2[16], v3[16];
	std::snprintf(v0, sizeof(v0), "%d", end_kills_);
	std::snprintf(v1, sizeof(v1), "%.0f", end_biomass_);
	// Tier is roughly log-ish of biomass; show a 1-5 bucket for the screenshot.
	int tier_shown = 1;
	if (end_biomass_ >   50.0f) tier_shown = 2;
	if (end_biomass_ >  150.0f) tier_shown = 3;
	if (end_biomass_ >  400.0f) tier_shown = 4;
	if (end_biomass_ > 1000.0f) tier_shown = 5;
	std::snprintf(v2, sizeof(v2), "%d", tier_shown);
	int ts = (int)end_time_;
	std::snprintf(v3, sizeof(v3), "%d:%02d", ts / 60, ts % 60);
	const char* vals[4] = { v0, v1, v2, v3 };
	// Ring fill fractions — dramatic but derived from the raw numbers.
	float frac[4] = {
		std::min(1.0f, end_kills_ / 10.0f),
		std::min(1.0f, end_biomass_ / 500.0f),
		(float)tier_shown / 5.0f,
		std::min(1.0f, end_time_ / 300.0f),
	};
	glm::vec4 ring_colors[4] = {
		m::ACCENT_DANGER, m::ACCENT_CYAN, m::ACCENT_AMBER, m::ACCENT_SUCCESS
	};

	const int SLOT_W = CARD_W / 4;
	int slot_y = card_y + 40;
	for (int i = 0; i < 4; ++i) {
		int cx = card_x + SLOT_W * i + SLOT_W / 2;
		int ring_cy = slot_y + 80;
		int radius = 56, thickness = 10;
		m::drawRingProgress(cx, ring_cy, radius, thickness,
			frac[i], ring_colors[i]);
		// Numeric readout centered in the ring.
		int vw = m::measureTextPx(vals[i], m::TYPE_TITLE_MD);
		m::drawTextModern(cx - vw / 2, ring_cy - m::TYPE_TITLE_MD / 2 - 2,
			vals[i], m::TYPE_TITLE_MD, m::TEXT_PRIMARY);
		// Label below.
		int lw = m::measureTextPx(labels[i], m::TYPE_LABEL);
		m::drawTextLabel(cx - lw / 2, ring_cy + radius + 18,
			labels[i], m::TEXT_SECONDARY);
	}

	// Buttons: PLAY AGAIN (primary) + MAIN MENU (ghost).
	const int BTN_W_LG = 240, BTN_W_SM = 180, BTN_H = 52;
	int btn_total = BTN_W_LG + 16 + BTN_W_SM;
	int btn_x0 = (W - btn_total) / 2;
	int btn_y = card_y + CARD_H + 36;

	glm::vec2 mouse_fb = mouse_px_;
	{
		int ww, wh; glfwGetWindowSize(window_.handle(), &ww, &wh);
		if (ww > 0 && wh > 0) {
			mouse_fb.x = mouse_px_.x * (float)W / (float)ww;
			mouse_fb.y = mouse_px_.y * (float)H / (float)wh;
		}
	}
	auto in_rect = [&](int x, int y, int w, int h) {
		return mouse_fb.x >= x && mouse_fb.x <= x + w
		    && mouse_fb.y >= y && mouse_fb.y <= y + h;
	};
	{
		bool hov = in_rect(btn_x0, btn_y, BTN_W_LG, BTN_H);
		bool prs = hov && mouse_left_down_;
		bool clk = hov && mouse_left_click_;
		m::buttonPrimary(btn_x0, btn_y, BTN_W_LG, BTN_H, "PLAY AGAIN", hov, prs);
		if (clk) { end_confetti_spawned_ = false; ui_fx_.clear(); goToStarter(); return; }
	}
	{
		int x = btn_x0 + BTN_W_LG + 16;
		bool hov = in_rect(x, btn_y, BTN_W_SM, BTN_H);
		bool prs = hov && mouse_left_down_;
		bool clk = hov && mouse_left_click_;
		m::buttonGhost(x, btn_y, BTN_W_SM, BTN_H, "MAIN MENU", hov, prs);
		if (clk) { end_confetti_spawned_ = false; ui_fx_.clear(); goToMainMenu(); return; }
	}
}

// ========================================================================
// KID MODE — starter picker, kid lab, celebration
// ========================================================================

void App::goToStarter() {
	state_ = AppState::STARTER;
	state_time_ = 0.0f;
	beginTransition(AppState::STARTER);
}

void App::goToLab(monsters::StarterKind kind) {
	std::mt19937 rng((uint32_t)(opts_.seed ^ (uint32_t)std::time(nullptr)));
	auto t = monsters::makeStarter(kind, rng);
	creature_name_ = generateName(rng);
	lab_.load_starter(t.cell, t.parts, t.color, creature_name_);
	state_ = AppState::LAB;
	state_time_ = 0.0f;
	beginTransition(AppState::LAB);
}

void App::goToCelebrate() {
	state_ = AppState::CELEBRATE;
	state_time_ = 0.0f;
	celebrate_t_ = 0.0f;
	beginTransition(AppState::CELEBRATE);
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
	namespace m = ui::modern;
	int W, H; glfwGetFramebufferSize(window_.handle(), &W, &H);
	m::beginFrame(text_.get(), W, H);

	// Dark charcoal scrim.
	m::drawScrim(0, 0, W, H, m::SURFACE_BG_TOP, m::SURFACE_BG_BOTTOM);

	// Mouse in framebuffer pixel space for hit-tests.
	glm::vec2 mouse_fb = mouse_px_;
	{
		int ww, wh; glfwGetWindowSize(window_.handle(), &ww, &wh);
		if (ww > 0 && wh > 0) {
			mouse_fb.x = mouse_px_.x * (float)W / (float)ww;
			mouse_fb.y = mouse_px_.y * (float)H / (float)wh;
		}
	}
	auto in_rect = [&](int x, int y, int w, int h) {
		return mouse_fb.x >= x && mouse_fb.x <= x + w
		    && mouse_fb.y >= y && mouse_fb.y <= y + h;
	};

	// Heading.
	{
		const std::string title = "CHOOSE YOUR STARTER";
		int tw = m::measureTextPx(title, m::TYPE_DISPLAY);
		m::drawTextDisplay((W - tw) / 2, 40, title, m::TEXT_PRIMARY);
		const std::string sub = "BUILD FROM A PREBUILT OR START FRESH";
		int sw = m::measureTextPx(sub, m::TYPE_LABEL);
		m::drawTextLabel((W - sw) / 2, 40 + m::TYPE_DISPLAY + 12,
			sub, m::TEXT_SECONDARY);
	}

	// Grid: 3 cols × 2 rows, 240×280 cards, 24px gap, centered horizontally.
	const monsters::StarterKind kinds[6] = {
		monsters::StarterKind::SPIKY,
		monsters::StarterKind::SQUISHY,
		monsters::StarterKind::ZOOMY,
		monsters::StarterKind::TOUGH,
		monsters::StarterKind::RANDOM,
		monsters::StarterKind::PLAIN,
	};
	const int COLS = 3, ROWS = 2;
	const int CARD_W = 240, CARD_H = 280;
	const int GAP = 24;
	const int GRID_W = COLS * CARD_W + (COLS - 1) * GAP;
	const int GRID_H = ROWS * CARD_H + (ROWS - 1) * GAP;
	const int grid_x = (W - GRID_W) / 2;
	const int grid_y = 200;

	std::mt19937 preview_rng(0xBEEFu);
	static int selected_idx = -1;

	int picked = -1;
	for (int i = 0; i < 6; ++i) {
		int col = i % COLS, row = i / COLS;
		int cx = grid_x + col * (CARD_W + GAP);
		int cy = grid_y + row * (CARD_H + GAP);
		bool hover = in_rect(cx, cy, CARD_W, CARD_H);

		// Hover scale 1.02 about card center (visual cue — just expand bounds).
		int draw_x = cx, draw_y = cy, draw_w = CARD_W, draw_h = CARD_H;
		if (hover) {
			int dw = (int)(CARD_W * 0.02f), dh = (int)(CARD_H * 0.02f);
			draw_x -= dw / 2; draw_y -= dh / 2;
			draw_w += dw; draw_h += dh;
		}

		// Shadow + glass panel.
		m::drawSoftShadow(draw_x, draw_y + 6, draw_w, draw_h, m::RADIUS_LG, 18, 0.35f);
		m::drawGlassPanel(draw_x, draw_y, draw_w, draw_h, m::RADIUS_LG);

		// Selected: cyan border + fill tint.
		if (selected_idx == i) {
			glm::vec4 tint = m::ACCENT_CYAN; tint.a = 0.10f;
			m::drawRoundedRect(draw_x, draw_y, draw_w, draw_h, m::RADIUS_LG,
				tint, m::ACCENT_CYAN, 2);
		} else if (hover) {
			m::drawInnerGlow(draw_x, draw_y, draw_w, draw_h, m::RADIUS_LG,
				m::ACCENT_CYAN_GLOW, 6);
		}

		// Chalk creature preview on a cream bezel inside the top 70%.
		int preview_h = (int)(draw_h * 0.60f);
		int preview_y = draw_y + 18;
		int preview_w = draw_w - 32;
		int preview_x = draw_x + 16;
		m::drawRoundedRect(preview_x, preview_y, preview_w, preview_h, m::RADIUS_MD,
			glm::vec4(0.96f, 0.95f, 0.89f, 1.0f), m::STROKE_SUBTLE, 1);

		auto tmpl = monsters::makeStarter(kinds[i], preview_rng);
		float ccx = (float)(preview_x + preview_w / 2);
		float ccy = (float)(preview_y + preview_h / 2);
		auto preview_shape = tmpl.shape();
		float max_r = 0.0f;
		for (auto& v : preview_shape) max_r = std::max(max_r, glm::length(v));
		float k = (max_r > 0.0f) ? ((preview_h * 0.40f) / max_r) : 1.0f;
		ChalkStroke outline;
		outline.color = tmpl.color;
		outline.half_width = 4.0f;
		for (auto& v : preview_shape) outline.points.push_back({ccx + v.x * k, ccy - v.y * k});
		outline.points.push_back(outline.points.front());
		std::vector<ChalkStroke> tmp{outline};
		renderer_->drawStrokes(tmp, nullptr, W, H);
		auto local_to_screen = [&](glm::vec2 v) { return glm::vec2(ccx + v.x * k, ccy - v.y * k); };
		std::vector<ChalkStroke> ps;
		appendPartStrokes(tmpl.parts, tmpl.color, local_to_screen, k,
		                  (float)glfwGetTime(), ps);
		if (!ps.empty()) renderer_->drawStrokes(ps, nullptr, W, H);

		// Name.
		int name_y = preview_y + preview_h + 18;
		std::string name = monsters::starterName(kinds[i]);
		int nw = m::measureTextPx(name, m::TYPE_TITLE_SM);
		m::drawTextModern(draw_x + (draw_w - nw) / 2, name_y, name,
			m::TYPE_TITLE_SM, m::TEXT_PRIMARY);

		// 3 stat pips (SPEED / TOUGH / BITE) — tiny 36×4 bars with label above.
		// Values derived approximately from the starter kind for visual variety.
		float speed = 0.5f, tough = 0.5f, bite = 0.5f;
		switch (kinds[i]) {
			case monsters::StarterKind::SPIKY:   bite = 0.9f; tough = 0.4f; speed = 0.5f; break;
			case monsters::StarterKind::SQUISHY: bite = 0.6f; tough = 0.3f; speed = 0.6f; break;
			case monsters::StarterKind::ZOOMY:   speed = 0.95f; bite = 0.5f; tough = 0.35f; break;
			case monsters::StarterKind::TOUGH:   tough = 0.95f; bite = 0.45f; speed = 0.35f; break;
			case monsters::StarterKind::RANDOM:  speed = 0.55f; bite = 0.55f; tough = 0.55f; break;
			case monsters::StarterKind::PLAIN:   speed = 0.4f;  bite = 0.4f;  tough = 0.4f;  break;
		}
		const int PIP_W = 46, PIP_H = 4;
		const int PIP_GAP = 12;
		int pip_total = PIP_W * 3 + PIP_GAP * 2;
		int pip_x = draw_x + (draw_w - pip_total) / 2;
		int pip_y = name_y + 18;
		auto pip = [&](int x, const char* lbl, float v, glm::vec4 col) {
			int lw = m::measureTextPx(lbl, m::TYPE_CAPTION);
			m::drawTextModern(x + (PIP_W - lw) / 2, pip_y, lbl,
				m::TYPE_CAPTION, m::TEXT_MUTED);
			m::drawRoundedRect(x, pip_y + 14, PIP_W, PIP_H, 2, m::TRACK_BG);
			int fw = (int)(PIP_W * std::max(0.0f, std::min(1.0f, v)));
			if (fw > 0) m::drawRoundedRect(x, pip_y + 14, fw, PIP_H, 2, col);
		};
		pip(pip_x,                        "SPD", speed, m::ACCENT_CYAN);
		pip(pip_x + PIP_W + PIP_GAP,      "TGH", tough, m::ACCENT_AMBER);
		pip(pip_x + (PIP_W + PIP_GAP)*2,  "BTE", bite,  m::ACCENT_DANGER);

		if (hover && mouse_left_click_) {
			selected_idx = i;
			picked = i;
		}
	}

	// Bottom buttons: START (primary, requires selection), BACK (ghost).
	const int BTN_W_LG = 220, BTN_W_SM = 140, BTN_H = 52;
	int btn_y = grid_y + GRID_H + 32;
	int total_btn_w = BTN_W_SM + 16 + BTN_W_LG;
	int btn_x0 = (W - total_btn_w) / 2;
	{
		bool hov = in_rect(btn_x0, btn_y, BTN_W_SM, BTN_H);
		bool prs = hov && mouse_left_down_;
		bool clk = hov && mouse_left_click_;
		m::buttonGhost(btn_x0, btn_y, BTN_W_SM, BTN_H, "BACK", hov, prs);
		if (clk) { goToMainMenu(); return; }
	}
	{
		int x = btn_x0 + BTN_W_SM + 16;
		bool hov = in_rect(x, btn_y, BTN_W_LG, BTN_H);
		bool prs = hov && mouse_left_down_;
		bool clk = hov && mouse_left_click_;
		m::buttonPrimary(x, btn_y, BTN_W_LG, BTN_H, "START", hov, prs);
		if (clk && selected_idx >= 0) {
			int idx = selected_idx;
			selected_idx = -1;
			goToLab(kinds[idx]);
			return;
		}
	}

	// If the user double-clicks a card, jump straight in (legacy behavior).
	if (picked >= 0 && mouse_left_click_) {
		// only when clicked on an already-selected card this same frame? keep
		// the "one-click-select, then START" contract. Nothing to do here.
	}
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
	namespace m = ui::modern;
	int W, H; glfwGetFramebufferSize(window_.handle(), &W, &H);
	m::beginFrame(text_.get(), W, H);

	// Dark scrim with a subtle vignette (darker bottom).
	glm::vec4 top_c = m::SURFACE_BG_TOP;
	glm::vec4 bot_c = m::SURFACE_BG_BOTTOM; bot_c.r *= 0.6f; bot_c.g *= 0.6f; bot_c.b *= 0.6f;
	m::drawScrim(0, 0, W, H, top_c, bot_c);

	// Slow, subtle cyan/amber mote swarm (replaces rainbow confetti).
	{
		static unsigned rs = 0xC11EBEEFu;
		static float motes[50][2];
		static float mvel[50][2];
		static unsigned char mcol[50];
		static bool seeded = false;
		auto r01 = [&]() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
			return (rs & 0xFFFFFF) / float(0x1000000); };
		if (!seeded) {
			for (int i = 0; i < 50; ++i) {
				motes[i][0] = r01() * W;
				motes[i][1] = r01() * H;
				float ang = r01() * 6.28318f;
				float sp = 4.0f + r01() * 6.0f;
				mvel[i][0] = std::cos(ang) * sp;
				mvel[i][1] = std::sin(ang) * sp;
				mcol[i] = (r01() < 0.5f) ? 0 : 1;
			}
			seeded = true;
		}
		for (int i = 0; i < 50; ++i) {
			motes[i][0] += mvel[i][0] * dt;
			motes[i][1] += mvel[i][1] * dt;
			if (motes[i][0] < -4)   motes[i][0] = (float)W + 4;
			if (motes[i][0] > W+4)  motes[i][0] = -4;
			if (motes[i][1] < -4)   motes[i][1] = (float)H + 4;
			if (motes[i][1] > H+4)  motes[i][1] = -4;
			glm::vec4 col = (mcol[i] == 0) ? m::ACCENT_CYAN : m::ACCENT_AMBER;
			col.a = 0.45f;
			m::drawRoundedRect((int)motes[i][0], (int)motes[i][1], 3, 3, 1, col);
		}
	}

	// Center card: 720×490, glass panel with shadow. Widened from 600 so
	// the PARTS/BIOMASS/TIER metric row has breathing room with real fonts.
	// Height grown from 420 to fit a distinct pills row between the creature
	// preview bezel and the metrics row.
	const int CARD_W = 720, CARD_H = 490;
	int card_x = (W - CARD_W) / 2;
	int card_y = (H - CARD_H) / 2;
	m::drawSoftShadow(card_x, card_y + 10, CARD_W, CARD_H, m::RADIUS_LG, 28, 0.45f);
	m::drawGlassPanel(card_x, card_y, CARD_W, CARD_H, m::RADIUS_LG);

	// Heading.
	{
		std::string title = "MEET " + creature_name_;
		int tw = m::measureTextPx(title, m::TYPE_TITLE_MD);
		m::drawTextModern(card_x + (CARD_W - tw) / 2, card_y + 28,
			title, m::TYPE_TITLE_MD, m::TEXT_PRIMARY);
	}

	// Chalk creature preview on its own cream bezel (200×200).
	const int PREVIEW_SZ = 200;
	int prev_x = card_x + (CARD_W - PREVIEW_SZ) / 2;
	int prev_y = card_y + 80;
	m::drawRoundedRect(prev_x, prev_y, PREVIEW_SZ, PREVIEW_SZ, m::RADIUS_MD,
		glm::vec4(0.96f, 0.95f, 0.89f, 1.0f), m::STROKE_SUBTLE, 1);

	float t = celebrate_t_;
	float scale = 1.0f, rot = 0.0f;
	if (t < 0.5f) { scale = 1.0f + 0.4f * (t / 0.5f); }
	else if (t < 1.0f) { scale = 1.4f; rot = std::sin((t - 0.5f) * 25.0f) * (20.0f * 3.14159f / 180.0f); }
	else if (t < 1.6f) { scale = 1.4f - 0.4f * ((t - 1.0f) / 0.6f); }
	else { scale = 1.0f + 0.04f * std::sin(t * 2.5f); }

	auto poly = sim::cellToPolygon(lab_.cell(), 1);
	float cx = (float)(prev_x + PREVIEW_SZ / 2);
	float cy = (float)(prev_y + PREVIEW_SZ / 2);
	float pxu = 1.8f * scale;
	float c = std::cos(rot), si = std::sin(rot);

	// Phase 1.1: body fill first.
	{
		sim::PartEffect pe = sim::computePartEffects(lab_.parts());
		std::vector<glm::vec2> poly_px;
		poly_px.reserve(poly.size());
		for (auto& v : poly) {
			glm::vec2 r(c * v.x - si * v.y, si * v.x + c * v.y);
			poly_px.push_back({cx + r.x * pxu, cy - r.y * pxu});
		}
		fill_renderer_->drawFill(poly_px, lab_.color(), pe.diet,
			123.0f, (float)glfwGetTime(), W, H);
	}

	ChalkStroke outline;
	outline.color = lab_.color();
	outline.half_width = 4.5f;
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
	renderer_->drawStrokes(ss, nullptr, W, H);

	// Diet + tier pill badges below the creature, centered as a pair.
	// Sit on their own row between the preview bezel and the metric row.
	{
		int pill_y = prev_y + PREVIEW_SZ + 20;
		const char* diet = "OMNIVORE";
		const char* tier_s = "TIER 1 - SPECK";
		// Width of pills: text_w + 2 * SPACE_MD (from drawPillBadge).
		int dw = m::measureTextPx(diet,   m::TYPE_BODY) + m::SPACE_MD * 2;
		int tw = m::measureTextPx(tier_s, m::TYPE_BODY) + m::SPACE_MD * 2;
		int gap = 16;
		int total = dw + gap + tw;
		int x0 = card_x + (CARD_W - total) / 2;
		m::drawPillBadge(x0, pill_y, diet,
			m::TEXT_PRIMARY, m::SURFACE_PANEL_HI, m::ACCENT_CYAN);
		m::drawPillBadge(x0 + dw + gap, pill_y, tier_s,
			m::TEXT_PRIMARY, m::SURFACE_PANEL_HI, m::ACCENT_AMBER);
	}

	// 3-column metric row: PARTS / BIOMASS / TIER.
	{
		const int MROW_Y = card_y + CARD_H - 120;
		const int COL_W = CARD_W / 3;
		const char* labels[3] = { "PARTS", "BIOMASS", "TIER" };
		char val0[16], val1[16], val2[16];
		std::snprintf(val0, sizeof(val0), "%d", (int)lab_.parts().size());
		std::snprintf(val1, sizeof(val1), "%.0f", 28.0f);
		std::snprintf(val2, sizeof(val2), "1");
		const char* vals[3] = { val0, val1, val2 };
		for (int i = 0; i < 3; ++i) {
			int col_cx = card_x + COL_W * i + COL_W / 2;
			// Real-font tracked label width.
			m::drawTextRole(col_cx, MROW_Y, labels[i], m::Role::LABEL,
				m::TEXT_SECONDARY, m::Align::CENTER);
			// Inter-Bold value centered under caption.
			m::drawTextRole(col_cx, MROW_Y + 22, vals[i], m::Role::TITLE_SM,
				m::TEXT_PRIMARY, m::Align::CENTER);
		}
	}

	// ENTER ARENA button.
	int btn_w = 240, btn_h = 48;
	int btn_x = card_x + (CARD_W - btn_w) / 2;
	int btn_y = card_y + CARD_H - 60;
	glm::vec2 mouse_fb = mouse_px_;
	{
		int ww, wh; glfwGetWindowSize(window_.handle(), &ww, &wh);
		if (ww > 0 && wh > 0) {
			mouse_fb.x = mouse_px_.x * (float)W / (float)ww;
			mouse_fb.y = mouse_px_.y * (float)H / (float)wh;
		}
	}
	bool hov = mouse_fb.x >= btn_x && mouse_fb.x <= btn_x + btn_w
	        && mouse_fb.y >= btn_y && mouse_fb.y <= btn_y + btn_h;
	bool prs = hov && mouse_left_down_;
	bool clk = hov && mouse_left_click_;
	m::buttonPrimary(btn_x, btn_y, btn_w, btn_h, "ENTER ARENA >", hov, prs);

	// Auto-advance after 4s or on button click.
	if (t >= 4.0f || clk) {
		celebrate_confetti_spawned_ = false;
		ui_fx_.clear();
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

// ========================================================================
// UI kitchen sink — exercises every modern primitive in one screen.
// ========================================================================

void App::drawUiKitchenSink() {
	namespace m = ui::modern;
	int W, H; glfwGetFramebufferSize(window_.handle(), &W, &H);
	m::beginFrame(text_.get(), W, H);

	// Charcoal gradient background.
	m::drawScrim(0, 0, W, H, m::SURFACE_BG_TOP, m::SURFACE_BG_BOTTOM);

	// Page title + caption.
	m::drawTextDisplay(W / 2, 24, "UI KITCHEN SINK",
		m::TEXT_PRIMARY, m::Align::CENTER);
	m::drawTextModern(W / 2, 24 + m::TYPE_DISPLAY + 8,
		"CellCraft modern design system", m::TYPE_BODY,
		m::TEXT_SECONDARY, m::Align::CENTER);

	// Glass panel hosting buttons, stats, badge, ring.
	int panel_x = 64, panel_y = 180;
	int panel_w = W - 128, panel_h = H - 320;
	m::drawSoftShadow(panel_x, panel_y, panel_w, panel_h, m::RADIUS_LG, 24, 0.45f);
	m::drawGlassPanel(panel_x, panel_y, panel_w, panel_h, m::RADIUS_LG);

	// Section label.
	int col_x = panel_x + m::SPACE_2XL;
	int col_y = panel_y + m::SPACE_2XL;
	m::drawTextLabel(col_x, col_y, "BUTTONS", m::TEXT_SECONDARY);
	col_y += m::SPACE_LG + m::TYPE_LABEL;

	// Three buttons in a row.
	int btn_w = 180, btn_h = 44;
	m::buttonPrimary(col_x,                         col_y, btn_w, btn_h, "PLAY", false, false);
	m::buttonGhost  (col_x + btn_w + m::SPACE_LG,   col_y, btn_w, btn_h, "OPTIONS", true, false);
	m::buttonIcon   (col_x + (btn_w + m::SPACE_LG)*2, col_y, btn_h, "?", "Help", true, false);

	// Divider.
	int div_y = col_y + btn_h + m::SPACE_XL;
	m::drawDivider(col_x, div_y, panel_w - m::SPACE_2XL * 2,
		m::DividerAxis::HORIZONTAL, m::STROKE_SUBTLE);

	// Stat bars.
	int stats_y = div_y + m::SPACE_XL;
	m::drawTextLabel(col_x, stats_y, "STATS", m::TEXT_SECONDARY);
	stats_y += m::SPACE_LG + m::TYPE_LABEL;
	int bar_w = 320;
	m::drawStatBar(col_x, stats_y,                    bar_w, "SPEED",    0.72f, "72/100", m::ACCENT_CYAN);
	m::drawStatBar(col_x, stats_y + 40,               bar_w, "FULLNESS", 0.34f, "34%",    m::ACCENT_AMBER);
	m::drawStatBar(col_x, stats_y + 80,               bar_w, "HEALTH",   0.91f, "91/100", m::ACCENT_SUCCESS);

	// Pill badge + ring progress (right column).
	int rcol_x = col_x + bar_w + m::SPACE_3XL;
	m::drawTextLabel(rcol_x, stats_y, "BADGE", m::TEXT_SECONDARY);
	m::drawPillBadge(rcol_x, stats_y + m::SPACE_LG + m::TYPE_LABEL,
		"T1 - SPECK", m::TEXT_PRIMARY, m::SURFACE_PANEL_HI, m::ACCENT_CYAN);

	m::drawTextLabel(rcol_x, stats_y + 60, "RING", m::TEXT_SECONDARY);
	m::drawRingProgress(rcol_x + 50, stats_y + 60 + 70, 36, 8,
		0.66f, m::ACCENT_CYAN);

	// Type sample row at the bottom.
	int type_y = panel_y + panel_h - 220;
	m::drawDivider(col_x, type_y - m::SPACE_LG, panel_w - m::SPACE_2XL * 2,
		m::DividerAxis::HORIZONTAL, m::STROKE_SUBTLE);
	m::drawTextLabel(col_x, type_y, "TYPE SAMPLES", m::TEXT_SECONDARY);
	int ty = type_y + m::SPACE_LG + m::TYPE_LABEL;
	m::drawTextModern(col_x, ty,                                "Display 72 - Hello",  m::TYPE_DISPLAY,  m::TEXT_PRIMARY);
	ty += m::TYPE_DISPLAY + m::SPACE_SM;
	m::drawTextModern(col_x, ty,                                "Title LG 40",         m::TYPE_TITLE_LG, m::TEXT_PRIMARY);
	int tx2 = col_x + 360;
	m::drawTextModern(tx2,   ty,                                "Title MD 28",         m::TYPE_TITLE_MD, m::TEXT_SECONDARY);
	int tx3 = col_x + 600;
	m::drawTextModern(tx3,   ty,                                "Title SM 20",         m::TYPE_TITLE_SM, m::TEXT_SECONDARY);
	ty += m::TYPE_TITLE_LG + m::SPACE_SM;
	m::drawTextModern(col_x, ty,                                "Body 14: the quick brown fox jumps", m::TYPE_BODY, m::TEXT_PRIMARY);
	m::drawTextModern(col_x + 420, ty,                          "Caption 11 muted",    m::TYPE_CAPTION,  m::TEXT_MUTED);
}

} // namespace civcraft::cellcraft

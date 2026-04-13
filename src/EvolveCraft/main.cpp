// EvolveCraft entry point — singleplayer, one process.
//
// Stages shipped so far (see src/EvolveCraft/docs/DESIGN.md §16):
//   1. SwimField + rendered pond + one cell
//   2. Food pellets + eat on overlap
//   5. (simplified) Three species — wanderer/predator/prey
//   6. Reproduction via energy threshold + DNA trait drift
//
// Multiplayer, embedded Python, parts-based 3D assembly, and the editor UI
// are still ahead. Those stages need substantial infra (TCP handshake for
// evolvecraft, pybind11 BrainRuntime, .glb loading, ImGui bindings).

#include "client/gl.h"
#include "client/window.h"
#include "client/orbit_camera.h"
#include "client/pond_renderer.h"
#include "client/text.h"
#include "client/particles.h"

#include "server/pond_sim.h"
#include "server/brains.h"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>

namespace evolvecraft {

static void scrollCallback(GLFWwindow* w, double xo, double yo) {
	auto* cam = static_cast<OrbitCamera*>(glfwGetWindowUserPointer(w));
	if (cam) cam->applyScroll(yo);
}

// Given a mouse pixel position and the current view/proj, intersect the
// "camera → pixel" ray with the water plane y = waterLevel. Returns false if
// the ray doesn't hit (looking up, or parallel to plane).
static bool screenToWaterPlane(double mx, double my, int winW, int winH,
                               const glm::mat4& view, const glm::mat4& proj,
                               float waterLevel, glm::vec3& outPos) {
	float x = (float)(2.0 * mx / winW - 1.0);
	float y = (float)(1.0 - 2.0 * my / winH);
	glm::mat4 invVP = glm::inverse(proj * view);
	glm::vec4 nearH = invVP * glm::vec4(x, y, -1.0f, 1.0f);
	glm::vec4 farH  = invVP * glm::vec4(x, y,  1.0f, 1.0f);
	glm::vec3 pNear = glm::vec3(nearH) / nearH.w;
	glm::vec3 pFar  = glm::vec3(farH)  / farH.w;
	glm::vec3 dir = glm::normalize(pFar - pNear);
	if (std::abs(dir.y) < 1e-5f) return false;
	float t = (waterLevel - pNear.y) / dir.y;
	if (t <= 0) return false;
	outPos = pNear + dir * t;
	return true;
}

static void writeScreenshotPPM(const char* path, int w, int h) {
	std::vector<unsigned char> pixels(w * h * 3);
	glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
	FILE* f = std::fopen(path, "wb");
	if (!f) return;
	std::fprintf(f, "P6\n%d %d\n255\n", w, h);
	// flip vertically
	for (int y = h - 1; y >= 0; y--)
		std::fwrite(&pixels[y * w * 3], 1, w * 3, f);
	std::fclose(f);
}

struct Args {
	bool logOnly = false;
	bool headless = false;     // skip GLFW entirely
	int  templateIndex = 1;    // 0=solo cell, 1=ecosystem (default)
	float duration = 0.0f;     // >0 = run N seconds then exit
	bool  screenshot = false;  // after ~3s, auto-save /tmp/evolvecraft_screenshot.ppm
	unsigned seed = 1337;
};

static Args parseArgs(int argc, char** argv) {
	Args a;
	for (int i = 1; i < argc; i++) {
		if (!std::strcmp(argv[i], "--log-only")) { a.logOnly = true; a.headless = true; }
		else if (!std::strcmp(argv[i], "--headless")) a.headless = true;
		else if (!std::strcmp(argv[i], "--template") && i+1 < argc) a.templateIndex = std::atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--seconds") && i+1 < argc) a.duration = (float)std::atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--seed") && i+1 < argc) a.seed = (unsigned)std::atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--screenshot")) a.screenshot = true;
	}
	return a;
}

static SimConfig configForTemplate(int idx, unsigned seed) {
	SimConfig cfg;
	cfg.seed = seed;
	switch (idx) {
	case 0: // one cell, no food — stage 1 log-only sanity
		cfg.pondRadius = 40.0f;
		cfg.initialCells = 1;
		cfg.initialFood = 0;
		cfg.foodSpawnPerSec = 0.0f;
		cfg.maxFood = 0;
		break;
	case 1: // ecosystem: wanderers + predators + prey + food spawn
	default:
		cfg.pondRadius = 55.0f;
		cfg.initialCells = 0; // seeded below explicitly
		cfg.initialFood = 60;
		cfg.foodSpawnPerSec = 6.0f;
		cfg.maxFood = 120;
		break;
	}
	return cfg;
}

static void seedEcosystem(PondSim& sim) {
	for (int i = 0; i < 10; i++) {
		DNA d;
		d.size     = 0.9f;
		d.colorHue = 0.55f + 0.15f * (i / 10.0f);
		sim.spawnCreature(sim.randomPondPos(sim.field().radius * 0.6f), Species::Wanderer, d);
	}
	for (int i = 0; i < 22; i++) {
		DNA d;
		d.size      = 0.95f;
		d.baseSpeed = 3.4f;
		d.baseSense = 14.0f;
		d.colorHue  = 0.30f;
		sim.spawnCreature(sim.randomPondPos(sim.field().radius * 0.75f), Species::Prey, d);
	}
	for (int i = 0; i < 6; i++) {
		DNA d;
		d.size       = 1.35f;
		d.baseSpeed  = 4.1f;
		d.baseSense  = 22.0f;
		d.aggression = 0.8f;
		d.colorHue   = 0.02f;
		sim.spawnCreature(sim.randomPondPos(sim.field().radius * 0.5f), Species::Predator, d);
	}
}

int run(int argc, char** argv) {
	Args args = parseArgs(argc, argv);
	SimConfig cfg = configForTemplate(args.templateIndex, args.seed);

	PondSim sim;
	sim.init(cfg);
	if (args.templateIndex == 1) {
		seedEcosystem(sim);
	}

	sim.m_combatEnabled = args.templateIndex == 1;
	installBrains(sim);

	if (args.logOnly || args.headless) {
		// Headless simulation loop: no window, no GL.
		const float tickDt = 1.0f / 30.0f;
		float elapsed = 0.0f;
		float logAccum = 0.0f;
		printf("[EvolveCraft] headless sim: tmpl=%d seed=%u dur=%.1fs\n",
		       args.templateIndex, args.seed, args.duration);
		while (args.duration <= 0.0f || elapsed < args.duration) {
			sim.tick(tickDt);
			elapsed += tickDt;
			logAccum += tickDt;
			if (logAccum >= 1.0f) {
				logAccum = 0.0f;
				printf("[World] t=%.1fs cells=%d food=%d totalEnergy=%.1f\n",
				       elapsed, sim.stats().creaturesAlive,
				       sim.stats().foodAlive, sim.stats().totalEnergy);
			}
			if (args.duration <= 0.0f && elapsed > 10.0f) break; // default sanity cap
		}
		printf("[EvolveCraft] headless done: ticks=%d cells=%d\n",
		       sim.stats().tickCount, sim.stats().creaturesAlive);
		return 0;
	}

	// === GUI mode ===
	fprintf(stderr, "[EvolveCraft] GUI mode, about to create window\n");
	modcraft::Window win;
	if (!win.init(1280, 800, "EvolveCraft")) return 1;
	fprintf(stderr, "[EvolveCraft] window up, initializing renderer\n");
	glfwSetInputMode(win.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	// Screenshot / headless-capture runs: disable vsync so we're not throttled
	// by a detached display.
	if (args.screenshot || args.duration > 0.0f)
		glfwSwapInterval(0);

	// Frame the whole pond in the default view. With fov=55 and the pond
	// diameter ≈ 2R, camera must stand ~ R / tan(fov/2) above.
	OrbitCamera cam;
	cam.fov       = 48.0f;
	cam.pitchDeg  = 58.0f;    // 90 is straight down; 58 gives a nice isometric feel
	cam.distance  = cfg.pondRadius * 1.85f;
	cam.maxDistance = cfg.pondRadius * 3.0f;
	cam.minDistance = 4.0f;
	glfwSetWindowUserPointer(win.handle(), &cam);
	glfwSetScrollCallback(win.handle(), scrollCallback);

	// Scope the GL-owning objects so their destructors (which call
	// glDeleteProgram / glDeleteBuffers via Shader::~Shader) run BEFORE
	// win.shutdown() tears down the GLFW context. Without this scope we
	// segfault on exit.
	{
	PondRenderer renderer;
	if (!renderer.init()) {
		fprintf(stderr, "[EvolveCraft] renderer init failed\n");
		return 1;
	}
	renderer.resize(win.width(), win.height());

	modcraft::TextRenderer hud;
	if (!hud.init("shaders")) {
		fprintf(stderr, "[EvolveCraft] text renderer init failed (non-fatal)\n");
	}

	modcraft::ParticleSystem particles;
	if (!particles.init("shaders")) {
		fprintf(stderr, "[EvolveCraft] particle system init failed (non-fatal)\n");
	}
	// Wire sim event hooks → particle bursts. All three hooks fire from
	// PondSim::tick and forward the world-space position of the event; the
	// particle system will integrate + fade them over the next ~0.8s.
	sim.m_onEatHook = [&](glm::vec3 pos) {
		particles.emitItemPickup(pos + glm::vec3(0, 0.3f, 0),
		                         glm::vec3(1.0f, 0.85f, 0.25f));
	};
	sim.m_onSplitHook = [&](glm::vec3 pos) {
		// Split = colourful burst — creature-ish colour so you can see whose
		// lineage just doubled.
		particles.emitBlockBreak(pos + glm::vec3(0, 0.3f, 0),
		                         glm::vec3(0.55f, 0.95f, 0.55f), 16);
	};
	sim.m_onDeathHook = [&](glm::vec3 pos) {
		particles.emitDeathPuff(pos + glm::vec3(0, 0.3f, 0),
		                        glm::vec3(0.85f, 0.25f, 0.25f), 0.9f);
	};

	fprintf(stderr, "[EvolveCraft] renderer ready, entering main loop\n");

	float time = 0.0f;
	float lastTime = (float)glfwGetTime();
	float simAccum = 0.0f;
	const float simDt = 1.0f / 30.0f;
	float statsAccum = 0.0f;
	float elapsed = 0.0f;
	bool screenshotTaken = false;
	bool m_closeupTaken  = false;
	// Smoothed FPS for HUD (raw dt is too jittery to show directly).
	float fpsSmoothed = 60.0f;
	// Edge-detect left mouse clicks so holding doesn't spam food.
	bool prevLMB = false;
	bool prevRMB = false;
	bool prevSpace = false;
	bool prevT = false;
	bool prevY = false;
	bool prevI = false, prevO = false, prevP = false;
	// Time controls: pause + 1×/2×/4×/8× sim rate.
	bool  paused = false;
	float simRate = 1.0f;
	// Selected creature for the inspect panel (0 = nothing).
	CreatureId selectedId = 0;
	// Camera auto-follows the selected creature when true.
	bool followSelected = false;

	while (!win.shouldClose()) {
		float now = (float)glfwGetTime();
		float dt = std::min(now - lastTime, 0.05f);
		lastTime = now;
		time += dt;
		elapsed += dt;

		win.pollEvents();
		cam.processInput(win.handle(), dt);
		if (glfwGetKey(win.handle(), GLFW_KEY_ESCAPE) == GLFW_PRESS) break;

		// Time controls — SPACE toggles pause, 1/2/3/4 = 1×/2×/4×/8×.
		bool space = glfwGetKey(win.handle(), GLFW_KEY_SPACE) == GLFW_PRESS;
		if (space && !prevSpace) paused = !paused;
		prevSpace = space;
		if (glfwGetKey(win.handle(), GLFW_KEY_1) == GLFW_PRESS) simRate = 1.0f;
		if (glfwGetKey(win.handle(), GLFW_KEY_2) == GLFW_PRESS) simRate = 2.0f;
		if (glfwGetKey(win.handle(), GLFW_KEY_3) == GLFW_PRESS) simRate = 4.0f;
		if (glfwGetKey(win.handle(), GLFW_KEY_4) == GLFW_PRESS) simRate = 8.0f;
		// T: frame camera on selected creature (one-shot). Y: toggle follow.
		bool tk = glfwGetKey(win.handle(), GLFW_KEY_T) == GLFW_PRESS;
		bool yk = glfwGetKey(win.handle(), GLFW_KEY_Y) == GLFW_PRESS;
		if (tk && !prevT && selectedId) {
			for (const auto& c : sim.creatures()) {
				if (c.id == selectedId && c.alive) {
					cam.center   = c.pos;
					cam.distance = std::min(cam.distance, 12.0f);
					break;
				}
			}
		}
		if (yk && !prevY) followSelected = !followSelected;
		prevT = tk;
		prevY = yk;

		// Seed-spawn keys: I wanderer, O prey, P predator. Spawn near camera
		// center with a small randomized offset so clicks don't stack.
		auto spawnNearCenter = [&](SpeciesId sp, DNA d) {
			float r = sim.randUniform(0, 4.0f);
			float a = sim.randUniform(0, 6.2831853f);
			glm::vec3 p = cam.center + glm::vec3(r * std::cos(a), 0, r * std::sin(a));
			p.y = sim.field().waterLevel;
			// Clamp inside pond.
			float rr = std::sqrt(p.x * p.x + p.z * p.z);
			if (rr > sim.field().radius * 0.95f) {
				float s = sim.field().radius * 0.95f / rr;
				p.x *= s; p.z *= s;
			}
			auto& c = sim.spawnCreature(p, sp, d);
			particles.emitBlockBreak(p + glm::vec3(0, 0.3f, 0),
			                         glm::vec3(0.9f, 0.9f, 1.0f), 10);
			selectedId = c.id; // auto-select new spawn so T/Y work
		};
		bool ik = glfwGetKey(win.handle(), GLFW_KEY_I) == GLFW_PRESS;
		bool ok = glfwGetKey(win.handle(), GLFW_KEY_O) == GLFW_PRESS;
		bool pk = glfwGetKey(win.handle(), GLFW_KEY_P) == GLFW_PRESS;
		if (ik && !prevI) {
			DNA d; d.size = 0.9f; d.colorHue = 0.60f;
			spawnNearCenter(Species::Wanderer, d);
		}
		if (ok && !prevO) {
			DNA d; d.size = 0.95f; d.baseSpeed = 3.4f; d.baseSense = 14.0f;
			d.colorHue = 0.30f;
			spawnNearCenter(Species::Prey, d);
		}
		if (pk && !prevP) {
			DNA d; d.size = 1.35f; d.baseSpeed = 4.1f; d.baseSense = 22.0f;
			d.aggression = 0.8f; d.colorHue = 0.02f;
			spawnNearCenter(Species::Predator, d);
		}
		prevI = ik; prevO = ok; prevP = pk;

		// Left-click → drop food pellet at clicked water position. Spawning from
		// the ray keeps the interaction spatially honest even when the camera
		// pitches; the small splash burst gives instant feedback.
		bool lmb = glfwGetMouseButton(win.handle(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
		if (lmb && !prevLMB) {
			double mx, my;
			glfwGetCursorPos(win.handle(), &mx, &my);
			float aspect0 = (float)win.width() / (float)win.height();
			glm::vec3 hit;
			if (screenToWaterPlane(mx, my, win.width(), win.height(),
			                       cam.view(), cam.proj(aspect0),
			                       sim.field().waterLevel, hit)) {
				float r = std::sqrt(hit.x * hit.x + hit.z * hit.z);
				if (r <= sim.field().radius * 0.98f) {
					sim.spawnFood(hit, 4.0f);
					particles.emitItemPickup(hit + glm::vec3(0, 0.2f, 0),
					                         glm::vec3(0.9f, 0.8f, 0.3f));
				}
			}
		}
		prevLMB = lmb;

		// Right-click → select the nearest creature to the clicked water point.
		// Cheap linear scan; 5k is fine — we're singleplayer.
		bool rmb = glfwGetMouseButton(win.handle(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
		if (rmb && !prevRMB) {
			double mx, my;
			glfwGetCursorPos(win.handle(), &mx, &my);
			float aspect0 = (float)win.width() / (float)win.height();
			glm::vec3 hit;
			if (screenToWaterPlane(mx, my, win.width(), win.height(),
			                       cam.view(), cam.proj(aspect0),
			                       sim.field().waterLevel, hit)) {
				float bestSq = 9.0f; // within 3m of click
				CreatureId bestId = 0;
				for (const auto& c : sim.creatures()) {
					if (!c.alive) continue;
					float dx = c.pos.x - hit.x, dz = c.pos.z - hit.z;
					float d2 = dx*dx + dz*dz;
					if (d2 < bestSq) { bestSq = d2; bestId = c.id; }
				}
				selectedId = bestId; // 0 deselects if nothing nearby
			}
		}
		prevRMB = rmb;

		if (!paused) {
			simAccum += dt * simRate;
			// Cap per-frame sim work so a pause-after-long-delay doesn't
			// freeze the renderer trying to catch up.
			int budget = 16;
			while (simAccum >= simDt && budget-- > 0) {
				sim.tick(simDt);
				simAccum -= simDt;
			}
			if (budget < 0) simAccum = 0;
		}

		particles.update(dt);

		// Resolve selected creature here (not in the HUD block) so both the
		// follow camera and the selection-ring emitter can see it.
		const Creature* sel = nullptr;
		if (selectedId) {
			for (const auto& c : sim.creatures()) {
				if (c.id == selectedId && c.alive) { sel = &c; break; }
			}
			if (!sel) selectedId = 0;
		}

		// Selection halo: every ~0.08s emit a full 10-point ring around the
		// selected creature. Very short lives mean gravity barely moves them,
		// so the overlapping ring snapshots form a stable golden disc that
		// visibly pulses and rotates.
		static float haloAccum = 0.0f;
		haloAccum += dt;
		if (sel && haloAccum >= 0.05f) {
			haloAccum = 0.0f;
			float ringR = std::max(0.9f, sel->dna.size * 0.9f) + 0.75f;
			// Lift the ring to roughly mid-body height so it encircles
			// the creature, not sit on the water beneath it.
			float hy = sel->dna.size * 0.55f + 0.5f;
			float phase = time * 2.0f;
			// Slight radial pulse so the ring breathes.
			float pulse = 1.0f + 0.08f * std::sin(time * 3.0f);
			const int N = 14;
			for (int k = 0; k < N; k++) {
				float ang = phase + k * (6.2831853f / N);
				modcraft::Particle p;
				p.pos = sel->pos + glm::vec3(std::cos(ang) * ringR * pulse,
				                              hy,
				                              std::sin(ang) * ringR * pulse);
				// Initial upward vel cancels gravity (-12) over 0.12s,
				// so each snapshot's ring is visibly planar.
				p.vel = glm::vec3(0, 0.72f, 0);
				p.color = glm::vec4(1.0f, 0.95f, 0.4f, 1.0f);
				p.life    = 0.12f;
				p.maxLife = 0.12f;
				p.size    = 0.14f;
				particles.addParticle(p);
			}
		}

		// If follow-mode is on, lerp the camera target toward the selected
		// creature each frame. Lerp (not snap) keeps orbit motion pleasant
		// even when the creature darts.
		if (followSelected && sel) {
			float k = std::min(1.0f, dt * 4.0f);
			cam.center = glm::mix(cam.center, sel->pos, k);
			cam.center.y = 0;
		}

		glViewport(0, 0, win.width(), win.height());
		renderer.resize(win.width(), win.height());
		float aspect = (float)win.width() / (float)win.height();
		renderer.render(sim.field(), sim.creatures(), sim.foods(),
		                cam.view(), cam.proj(aspect), cam.position(), time);
		// Particles drawn AFTER the scene so they blend over creatures.
		particles.render(cam.proj(aspect) * cam.view());

		// HUD: per-species counts + food + tick/fps. One-frame dt is jittery,
		// smooth with a simple exponential filter before display.
		if (dt > 0.0001f) fpsSmoothed = fpsSmoothed * 0.9f + (1.0f / dt) * 0.1f;
		int nWander = 0, nPrey = 0, nPred = 0;
		for (const auto& c : sim.creatures()) {
			if (!c.alive) continue;
			if (c.species == Species::Wanderer) nWander++;
			else if (c.species == Species::Prey) nPrey++;
			else if (c.species == Species::Predator) nPred++;
		}
		char line1[96], line2[128], line3[128];
		const char* speedTag = paused ? "PAUSED" :
		                       (simRate >= 7.9f ? "8x" :
		                        simRate >= 3.9f ? "4x" :
		                        simRate >= 1.9f ? "2x" : "1x");
		std::snprintf(line1, sizeof(line1),
		              "EvolveCraft  %.0f fps  t=%.0fs  %s",
		              fpsSmoothed, elapsed, speedTag);
		std::snprintf(line2, sizeof(line2),
		              "wanderers:%d  prey:%d  predators:%d  food:%d",
		              nWander, nPrey, nPred, sim.stats().foodAlive);

		// `sel` resolved above (pre-render) so the selection halo can use it.
		if (sel) {
			const char* spName = sel->species == Species::Wanderer ? "wanderer"
			                   : sel->species == Species::Prey     ? "prey"
			                   : sel->species == Species::Predator ? "predator"
			                   : "?";
			std::snprintf(line3, sizeof(line3),
			              "#%u %s  hp:%d/%d  e:%.1f  sz:%.2f spd:%.1f sns:%.1f",
			              (unsigned)sel->id, spName, sel->hp, sel->maxHp,
			              sel->energy, sel->dna.size, sel->effectiveSpeed,
			              sel->effectiveSense);
		} else {
			std::snprintf(line3, sizeof(line3),
			              "LMB food  RMB inspect  I/O/P spawn  SPC pause  "
			              "1-4 speed  WASD pan  T frame  Y follow");
		}

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glm::vec4 line1Col = paused
		    ? glm::vec4(1.0f, 0.65f, 0.25f, 1.0f)  // orange while paused
		    : glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
		hud.drawText(line1, -0.98f, 0.92f, 1.1f, line1Col, aspect);
		hud.drawText(line2, -0.98f, 0.82f, 0.95f,
		             glm::vec4(0.80f, 0.95f, 1.0f, 0.95f), aspect);
		hud.drawText(line3, -0.98f, 0.72f, 0.82f,
		             sel ? glm::vec4(1.0f, 0.95f, 0.55f, 1.0f)
		                 : glm::vec4(0.75f, 0.82f, 0.92f, 1.0f),
		             aspect);

		win.swapBuffers();

		statsAccum += dt;
		if (statsAccum >= 1.0f) {
			statsAccum = 0.0f;
			printf("[World] t=%.1fs cells=%d food=%d\n",
			       elapsed, sim.stats().creaturesAlive, sim.stats().foodAlive);
		}

		if (args.screenshot && !screenshotTaken && elapsed > 3.0f) {
			writeScreenshotPPM("/tmp/evolvecraft_auto_screenshot.ppm",
			                   win.width(), win.height());
			printf("[EvolveCraft] wrote /tmp/evolvecraft_auto_screenshot.ppm\n");
			screenshotTaken = true;
			// Schedule a second close-up shot by moving the camera near a
			// random creature. The main loop will pick up the new camera state.
			// Also auto-select it so the halo ring renders in the closeup.
			if (!sim.creatures().empty()) {
				const Creature& c = sim.creatures()[sim.creatures().size() / 2];
				cam.center   = c.pos;
				cam.distance = 10.0f;
				cam.pitchDeg = 50.0f;
				selectedId   = c.id;
			}
		}
		if (args.screenshot && screenshotTaken && elapsed > 4.5f &&
		    !m_closeupTaken) {
			writeScreenshotPPM("/tmp/evolvecraft_closeup.ppm",
			                   win.width(), win.height());
			printf("[EvolveCraft] wrote /tmp/evolvecraft_closeup.ppm\n");
			m_closeupTaken = true;
		}

		if (args.duration > 0.0f && elapsed > args.duration) break;
	}
	particles.shutdown();
	hud.shutdown();
	} // end GL-owning scope: renderer + hud destruct with live context

	win.shutdown();
	return 0;
}

} // namespace evolvecraft

int main(int argc, char** argv) { return evolvecraft::run(argc, argv); }

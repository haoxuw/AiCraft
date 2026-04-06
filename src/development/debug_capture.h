#pragma once

// DebugCapture — scenario-based visual QA system.
//
// Activated via CLI flags — zero overhead when inactive.
//
//   --debug-scenario item_views --debug-item base:sword
//   --debug-scenario animation  --debug-item base:sword
//
// Screenshots are written to /tmp/debug_N_<suffix>.ppm
//
// Architecture:
//   DebugCapture       coordinator — owns the active IScenario
//   IScenario          interface   — one step per frame, returns true when done
//   ItemViewsScenario  FPS/TPS/RPG/RTS/ground shots for a single item
//   AnimationScenario  progressive swing-animation frames
//
// To add a new scenario: subclass IScenario in src/development/ and register
// its name in DebugCapture::configure().

#include "development/scenario.h"
#include "development/item_views_scenario.h"
#include "development/animation_scenario.h"
#include <memory>
#include <string>
#include <cstdio>

namespace modcraft {
namespace development {

class DebugCapture {
public:
	struct Config {
		bool        active      = false;
		std::string scenario;           // "item_views" | "animation"
		std::string targetItem;         // e.g. "base:sword"
		std::string outputDir   = "/tmp";
	};

	// Configure from parsed CLI args. Call once before entering the game loop.
	void configure(const Config& cfg) {
		m_cfg = cfg;
		if (!cfg.active) return;

		if (cfg.scenario == "item_views") {
			m_scenario = std::make_unique<ItemViewsScenario>(cfg.targetItem);
		} else if (cfg.scenario == "animation") {
			m_scenario = std::make_unique<AnimationScenario>(cfg.targetItem);
		} else {
			fprintf(stderr, "[DebugCapture] Unknown scenario '%s'\n",
			        cfg.scenario.c_str());
			m_cfg.active = false;
		}
	}

	bool active() const { return m_cfg.active && m_scenario; }
	bool done()   const { return m_done; }

	// Drive the scenario each frame.
	// Inject game callbacks so DebugCapture stays decoupled from Game internals.
	void tick(float dt, Entity* player, Camera& camera,
	          const ScenarioCallbacks& cb)
	{
		if (!active() || m_done) return;

		// Wrap the save callback to inject the numbered path prefix.
		ScenarioCallbacks wrapped = cb;
		wrapped.save = [this, &cb](const std::string& suffix) {
			char path[256];
			snprintf(path, sizeof(path), "%s/debug_%d_%s.ppm",
			         m_cfg.outputDir.c_str(), m_shotIndex++, suffix.c_str());
			cb.save(path);  // game writes the actual PPM at this path
			printf("[DebugCapture] Shot: %s\n", path);
		};

		if (m_scenario->tick(dt, player, camera, wrapped)) {
			printf("[DebugCapture] Scenario '%s' complete.\n",
			       m_scenario->name());
			m_done = true;
		}
	}

private:
	Config                       m_cfg;
	std::unique_ptr<IScenario>   m_scenario;
	int                          m_shotIndex = 0;
	bool                         m_done      = false;
};

} // namespace development
} // namespace modcraft

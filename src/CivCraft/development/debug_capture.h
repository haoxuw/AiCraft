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
#include "development/character_views_scenario.h"
#include "development/combo_views_scenario.h"
#include <sstream>
#include <memory>
#include <string>
#include <cstdio>

namespace civcraft {
namespace development {

class DebugCapture {
public:
	struct Config {
		bool        active      = false;
		std::string scenario;           // "item_views" | "animation" | "character_views"
		std::string targetItem;         // e.g. "sword"
		std::string targetCharacter;    // e.g. "pig" (character_views only)
		std::string targetClip;         // e.g. "chop" (character_views only, optional)
		std::string handItem;           // e.g. "sword" — equip in hand for clip shots
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
		} else if (cfg.scenario == "character_views") {
			m_scenario = std::make_unique<CharacterViewsScenario>(
				cfg.targetCharacter, cfg.targetClip, 0.6f, cfg.handItem);
		} else if (cfg.scenario == "combo_views") {
			// Clips come in via --debug-clip as a space-separated list.
			// Default: the built-in sword combo.
			std::vector<std::string> clips;
			std::istringstream ss(cfg.targetClip);
			std::string c;
			while (ss >> c) clips.push_back(c);
			if (clips.empty()) clips = {"swing_left", "swing_right", "cleave"};
			std::string hand = cfg.handItem.empty() ? "sword" : cfg.handItem;
			m_scenario = std::make_unique<ComboViewsScenario>(clips, hand);
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
} // namespace civcraft

#pragma once

#include <GLFW/glfw3.h>
#include "client/input_source.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace modcraft {

// Action name constants -- use these instead of raw strings.
namespace Action {
	constexpr const char* MoveForward  = "move_forward";
	constexpr const char* MoveBackward = "move_backward";
	constexpr const char* MoveLeft     = "move_left";
	constexpr const char* MoveRight    = "move_right";
	constexpr const char* Jump         = "jump";
	constexpr const char* Descend      = "descend";
	constexpr const char* Sprint       = "sprint";
	constexpr const char* CycleView    = "cycle_view";
	constexpr const char* BreakBlock   = "break_block";
	constexpr const char* PlaceBlock   = "place_block";
	constexpr const char* Screenshot   = "screenshot";
	constexpr const char* ToggleDebug  = "toggle_debug";
	constexpr const char* ToggleInventory = "toggle_inventory";
	constexpr const char* MenuBack     = "menu_back";
	constexpr const char* DropItem     = "drop_item";
	constexpr const char* EquipItem    = "equip_item";
	constexpr const char* Dance        = "dance";
	constexpr const char* ControlPrev  = "control_prev";  // [ — cycle to previous owned entity
	constexpr const char* ControlNext  = "control_next";  // ] — cycle to next owned entity
}

struct ActionBinding {
	std::string action;
	std::string displayName;
	std::string keyName;
	int glfwKey = -1;
	int mouseButton = -1;
};

/**
 * ControlManager -- maps actions to input, queries multiple InputSources.
 *
 * Architecture:
 *   1. Load bindings from config (action → key/button)
 *   2. Add one or more InputSources (keyboard, gamepad, touch)
 *   3. Each frame: call update() which polls ALL sources
 *   4. Query: held("jump") / pressed("jump") -- returns true if
 *      ANY source has that input active
 *
 * This means a gamepad and keyboard work simultaneously with
 * zero extra code in the game logic.
 */
class ControlManager {
public:
	// Load bindings from YAML config. Returns false if file not found.
	bool load(const std::string& path);

	// Add an input source. Manager takes ownership.
	void addSource(std::unique_ptr<InputSource> source) {
		m_sources.push_back(std::move(source));
	}

	// Call once per frame BEFORE querying actions.
	void update(GLFWwindow* window);

	// Is the action currently held down (by ANY source)?
	bool held(const char* action) const;

	// Was the action just pressed this frame (edge-triggered)?
	bool pressed(const char* action) const;

	// All bindings for display in the controls screen.
	const std::vector<ActionBinding>& bindings() const { return m_bindingList; }

	// Get all connected sources (for debug/settings display)
	const std::vector<std::unique_ptr<InputSource>>& sources() const { return m_sources; }

private:
	static int keyNameToGLFW(const std::string& name);
	static std::string displayNameForAction(const std::string& action);

	std::unordered_map<std::string, ActionBinding> m_bindings;
	std::vector<ActionBinding> m_bindingList;

	struct InputState { bool current = false; bool previous = false; };
	std::unordered_map<std::string, InputState> m_state;

	std::vector<std::unique_ptr<InputSource>> m_sources;
};

} // namespace modcraft

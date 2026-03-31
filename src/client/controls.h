#pragma once

#include <GLFW/glfw3.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace aicraft {

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
	constexpr const char* MenuBack     = "menu_back";
}

struct ActionBinding {
	std::string action;       // "move_forward"
	std::string displayName;  // "Move Forward"
	std::string keyName;      // "W"
	int glfwKey = -1;         // GLFW_KEY_W or -1 if mouse
	int mouseButton = -1;     // GLFW_MOUSE_BUTTON_LEFT or -1 if keyboard
};

class ControlManager {
public:
	// Load bindings from a config file. Returns false if file not found.
	bool load(const std::string& path);

	// Call once per frame BEFORE querying actions.
	void update(GLFWwindow* window);

	// Is the action currently held down?
	bool held(const char* action) const;

	// Was the action just pressed this frame (edge-triggered)?
	bool pressed(const char* action) const;

	// All bindings for display in the controls screen.
	const std::vector<ActionBinding>& bindings() const { return m_bindingList; }

private:
	// Key name <-> GLFW code mapping
	static int keyNameToGLFW(const std::string& name);
	static std::string displayNameForAction(const std::string& action);

	std::unordered_map<std::string, ActionBinding> m_bindings;
	std::vector<ActionBinding> m_bindingList; // ordered for display

	// Per-frame state
	struct InputState { bool current = false; bool previous = false; };
	std::unordered_map<std::string, InputState> m_state;
};

} // namespace aicraft

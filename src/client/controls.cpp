#include "client/controls.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdio>

namespace agentica {

// ============================================================
// Key name -> GLFW code mapping
// ============================================================

struct KeyMapping { const char* name; int code; bool isMouse; };

static const KeyMapping KEY_TABLE[] = {
	// Letters
	{"A", GLFW_KEY_A, false}, {"B", GLFW_KEY_B, false}, {"C", GLFW_KEY_C, false},
	{"D", GLFW_KEY_D, false}, {"E", GLFW_KEY_E, false}, {"F", GLFW_KEY_F, false},
	{"G", GLFW_KEY_G, false}, {"H", GLFW_KEY_H, false}, {"I", GLFW_KEY_I, false},
	{"J", GLFW_KEY_J, false}, {"K", GLFW_KEY_K, false}, {"L", GLFW_KEY_L, false},
	{"M", GLFW_KEY_M, false}, {"N", GLFW_KEY_N, false}, {"O", GLFW_KEY_O, false},
	{"P", GLFW_KEY_P, false}, {"Q", GLFW_KEY_Q, false}, {"R", GLFW_KEY_R, false},
	{"S", GLFW_KEY_S, false}, {"T", GLFW_KEY_T, false}, {"U", GLFW_KEY_U, false},
	{"V", GLFW_KEY_V, false}, {"W", GLFW_KEY_W, false}, {"X", GLFW_KEY_X, false},
	{"Y", GLFW_KEY_Y, false}, {"Z", GLFW_KEY_Z, false},
	// Numbers
	{"0", GLFW_KEY_0, false}, {"1", GLFW_KEY_1, false}, {"2", GLFW_KEY_2, false},
	{"3", GLFW_KEY_3, false}, {"4", GLFW_KEY_4, false}, {"5", GLFW_KEY_5, false},
	{"6", GLFW_KEY_6, false}, {"7", GLFW_KEY_7, false}, {"8", GLFW_KEY_8, false},
	{"9", GLFW_KEY_9, false},
	// Function keys
	{"F1", GLFW_KEY_F1, false}, {"F2", GLFW_KEY_F2, false}, {"F3", GLFW_KEY_F3, false},
	{"F4", GLFW_KEY_F4, false}, {"F5", GLFW_KEY_F5, false}, {"F6", GLFW_KEY_F6, false},
	{"F7", GLFW_KEY_F7, false}, {"F8", GLFW_KEY_F8, false}, {"F9", GLFW_KEY_F9, false},
	{"F10", GLFW_KEY_F10, false}, {"F11", GLFW_KEY_F11, false}, {"F12", GLFW_KEY_F12, false},
	// Special keys
	{"Space", GLFW_KEY_SPACE, false},
	{"Escape", GLFW_KEY_ESCAPE, false},
	{"Enter", GLFW_KEY_ENTER, false},
	{"Tab", GLFW_KEY_TAB, false},
	{"Backspace", GLFW_KEY_BACKSPACE, false},
	{"Delete", GLFW_KEY_DELETE, false},
	{"Insert", GLFW_KEY_INSERT, false},
	{"Home", GLFW_KEY_HOME, false},
	{"End", GLFW_KEY_END, false},
	{"PageUp", GLFW_KEY_PAGE_UP, false},
	{"PageDown", GLFW_KEY_PAGE_DOWN, false},
	// Arrow keys
	{"Up", GLFW_KEY_UP, false},
	{"Down", GLFW_KEY_DOWN, false},
	{"Left", GLFW_KEY_LEFT, false},
	{"Right", GLFW_KEY_RIGHT, false},
	// Modifier keys
	{"LeftShift", GLFW_KEY_LEFT_SHIFT, false},
	{"RightShift", GLFW_KEY_RIGHT_SHIFT, false},
	{"LeftControl", GLFW_KEY_LEFT_CONTROL, false},
	{"RightControl", GLFW_KEY_RIGHT_CONTROL, false},
	{"LeftAlt", GLFW_KEY_LEFT_ALT, false},
	{"RightAlt", GLFW_KEY_RIGHT_ALT, false},
	// Mouse buttons
	{"MouseLeft", GLFW_MOUSE_BUTTON_LEFT, true},
	{"MouseRight", GLFW_MOUSE_BUTTON_RIGHT, true},
	{"MouseMiddle", GLFW_MOUSE_BUTTON_MIDDLE, true},
};

int ControlManager::keyNameToGLFW(const std::string& name) {
	for (auto& k : KEY_TABLE)
		if (name == k.name) return k.code;
	return -1;
}

static bool isMouseButton(const std::string& name) {
	return name.find("Mouse") == 0;
}

// ============================================================
// Action display names
// ============================================================

std::string ControlManager::displayNameForAction(const std::string& action) {
	// Convert snake_case to Title Case
	std::string result;
	bool capitalize = true;
	for (char c : action) {
		if (c == '_') {
			result += ' ';
			capitalize = true;
		} else {
			result += capitalize ? (char)toupper(c) : c;
			capitalize = false;
		}
	}
	return result;
}

// ============================================================
// Load config
// ============================================================

static std::string trim(const std::string& s) {
	size_t start = s.find_first_not_of(" \t\r\n");
	if (start == std::string::npos) return "";
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
}

bool ControlManager::load(const std::string& path) {
	std::ifstream file(path);
	if (!file.is_open()) {
		printf("Controls: could not open %s, using defaults\n", path.c_str());
		return false;
	}

	m_bindings.clear();
	m_bindingList.clear();

	std::string line;
	while (std::getline(file, line)) {
		line = trim(line);
		if (line.empty() || line[0] == '#') continue;

		auto colon = line.find(':');
		if (colon == std::string::npos) continue;

		std::string action = trim(line.substr(0, colon));
		std::string keyName = trim(line.substr(colon + 1));

		if (action.empty() || keyName.empty()) continue;

		ActionBinding binding;
		binding.action = action;
		binding.displayName = displayNameForAction(action);
		binding.keyName = keyName;

		if (isMouseButton(keyName)) {
			binding.mouseButton = keyNameToGLFW(keyName);
			binding.glfwKey = -1;
		} else {
			binding.glfwKey = keyNameToGLFW(keyName);
			binding.mouseButton = -1;
		}

		if (binding.glfwKey == -1 && binding.mouseButton == -1) {
			printf("Controls: unknown key '%s' for action '%s'\n",
				keyName.c_str(), action.c_str());
			continue;
		}

		m_bindings[action] = binding;
		m_bindingList.push_back(binding);
	}

	printf("Controls: loaded %zu bindings from %s\n", m_bindings.size(), path.c_str());
	return true;
}

// ============================================================
// Per-frame update
// ============================================================

void ControlManager::update(GLFWwindow* window) {
	// If no sources registered, add default keyboard+mouse
	if (m_sources.empty()) {
		m_sources.push_back(std::make_unique<GLFWInputSource>());
		// Auto-detect gamepad
		if (glfwJoystickPresent(GLFW_JOYSTICK_1))
			m_sources.push_back(std::make_unique<GamepadInputSource>());
	}

	// Poll all sources
	for (auto& src : m_sources)
		src->beginFrame(window);

	// Update action states: true if ANY source has the key/button down
	for (auto& [action, binding] : m_bindings) {
		auto& state = m_state[action];
		state.previous = state.current;
		state.current = false;

		for (auto& src : m_sources) {
			if (binding.mouseButton >= 0 && src->isMouseButtonDown(binding.mouseButton)) {
				state.current = true;
				break;
			}
			if (binding.glfwKey >= 0 && src->isKeyDown(binding.glfwKey)) {
				state.current = true;
				break;
			}
		}
	}
}

bool ControlManager::held(const char* action) const {
	auto it = m_state.find(action);
	return it != m_state.end() && it->second.current;
}

bool ControlManager::pressed(const char* action) const {
	auto it = m_state.find(action);
	return it != m_state.end() && it->second.current && !it->second.previous;
}

} // namespace agentica

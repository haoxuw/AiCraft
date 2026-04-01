#pragma once

/**
 * InputSource -- abstract interface for polling input devices.
 *
 * The ControlManager holds one or more InputSources and merges
 * their states. Each source knows how to poll one device type.
 *
 * Implementations:
 *   GLFWInputSource    -- keyboard + mouse via GLFW (native + web)
 *   GamepadInputSource -- controllers via GLFW joystick API (future)
 *   TouchInputSource   -- virtual joystick on mobile web (future)
 *
 * Adding a new input device:
 *   1. Subclass InputSource
 *   2. Implement pollKey() and pollMouseButton()
 *   3. Register with ControlManager::addSource()
 */

#include <GLFW/glfw3.h>
#include <memory>
#include <vector>

namespace aicraft {

class InputSource {
public:
	virtual ~InputSource() = default;

	// Called once per frame before any queries.
	virtual void beginFrame(GLFWwindow* window) = 0;

	// Is a keyboard key currently pressed?
	virtual bool isKeyDown(int glfwKey) const = 0;

	// Is a mouse button currently pressed?
	virtual bool isMouseButtonDown(int button) const = 0;

	// Source name for debug/display.
	virtual const char* name() const = 0;
};

// ================================================================
// GLFW keyboard + mouse (works on native and Emscripten)
// ================================================================
class GLFWInputSource : public InputSource {
public:
	void beginFrame(GLFWwindow* window) override {
		m_window = window;
	}

	bool isKeyDown(int glfwKey) const override {
		return m_window && glfwGetKey(m_window, glfwKey) == GLFW_PRESS;
	}

	bool isMouseButtonDown(int button) const override {
		return m_window && glfwGetMouseButton(m_window, button) == GLFW_PRESS;
	}

	const char* name() const override { return "Keyboard+Mouse"; }

private:
	GLFWwindow* m_window = nullptr;
};

// ================================================================
// Gamepad via GLFW joystick API (future)
// ================================================================
class GamepadInputSource : public InputSource {
public:
	explicit GamepadInputSource(int joystickId = GLFW_JOYSTICK_1)
		: m_joyId(joystickId) {}

	void beginFrame(GLFWwindow*) override {
		m_present = glfwJoystickPresent(m_joyId);
		if (m_present) {
			m_buttons = glfwGetJoystickButtons(m_joyId, &m_buttonCount);
			m_axes = glfwGetJoystickAxes(m_joyId, &m_axisCount);
		}
	}

	bool isKeyDown(int glfwKey) const override {
		// Map common keyboard keys to gamepad buttons.
		// This is a simplified mapping; a full implementation would
		// use a configurable mapping table.
		if (!m_present || !m_buttons) return false;
		int btn = keyToButton(glfwKey);
		return btn >= 0 && btn < m_buttonCount && m_buttons[btn] == GLFW_PRESS;
	}

	bool isMouseButtonDown(int button) const override {
		// Map mouse buttons to gamepad triggers
		if (!m_present || !m_axes) return false;
		if (button == GLFW_MOUSE_BUTTON_LEFT && m_axisCount > 5)
			return m_axes[5] > 0.5f;  // right trigger → left click
		if (button == GLFW_MOUSE_BUTTON_RIGHT && m_axisCount > 4)
			return m_axes[4] > 0.5f;  // left trigger → right click
		return false;
	}

	// Read analog stick axes (for movement/camera)
	float leftStickX() const { return m_present && m_axisCount > 0 ? m_axes[0] : 0; }
	float leftStickY() const { return m_present && m_axisCount > 1 ? m_axes[1] : 0; }
	float rightStickX() const { return m_present && m_axisCount > 2 ? m_axes[2] : 0; }
	float rightStickY() const { return m_present && m_axisCount > 3 ? m_axes[3] : 0; }

	bool connected() const { return m_present; }
	const char* name() const override { return "Gamepad"; }

private:
	static int keyToButton(int glfwKey) {
		// Standard gamepad mapping (Xbox-style):
		// A=0, B=1, X=2, Y=3, LB=4, RB=5, Back=6, Start=7,
		// Guide=8, LS=9, RS=10, DUp=11, DRight=12, DDown=13, DLeft=14
		switch (glfwKey) {
		case GLFW_KEY_SPACE:         return 0;  // A = jump
		case GLFW_KEY_LEFT_SHIFT:    return 4;  // LB = sprint
		case GLFW_KEY_ESCAPE:        return 7;  // Start = menu
		case GLFW_KEY_V:             return 3;  // Y = cycle view
		case GLFW_KEY_I:             return 6;  // Back = inventory
		default: return -1;
		}
	}

	int m_joyId;
	bool m_present = false;
	const unsigned char* m_buttons = nullptr;
	const float* m_axes = nullptr;
	int m_buttonCount = 0;
	int m_axisCount = 0;
};

} // namespace aicraft

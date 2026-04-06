#pragma once

/**
 * UI system backed by Dear ImGui.
 *
 * Replaces the hand-rolled TextRenderer + drawRect HUD with
 * ImGui which handles text, buttons, panels, input fields,
 * and works identically on native and web (Emscripten).
 *
 * Usage in the game loop:
 *   ui.beginFrame();
 *   // ... all ImGui draw calls (menus, HUD, panels) ...
 *   ui.endFrame();   // renders everything
 */

#include <GLFW/glfw3.h>

namespace modcraft {

class UI {
public:
	// Initialize ImGui with GLFW window + OpenGL backend.
	bool init(GLFWwindow* window);

	// Shutdown ImGui.
	void shutdown();

	// Call at the start of each frame (before any ImGui calls).
	void beginFrame();

	// Call at the end of each frame (renders all ImGui draw data).
	void endFrame();

	// Is ImGui capturing keyboard input? (e.g., text field focused)
	// If true, game should not process WASD etc.
	bool wantsKeyboard() const;

	// Is ImGui capturing mouse? (e.g., hovering a window)
	bool wantsMouse() const;
};

} // namespace modcraft

#pragma once

#include "client/gl.h"
#include <GLFW/glfw3.h>
#include <string>

namespace civcraft {

class Window {
public:
	bool init(int width, int height, const std::string& title, bool hidden = false);
	void shutdown();

	bool shouldClose() const;
	void swapBuffers();
	void pollEvents();

	GLFWwindow* handle() const { return m_window; }
	int width() const { return m_width; }
	int height() const { return m_height; }
	float aspectRatio() const { return (float)m_width / (float)m_height; }

private:
	static void framebufferCallback(GLFWwindow* w, int width, int height);

	GLFWwindow* m_window = nullptr;
	int m_width = 0;
	int m_height = 0;
};

} // namespace civcraft

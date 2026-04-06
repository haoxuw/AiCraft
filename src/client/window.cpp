#include "client/window.h"
#include <cstdio>

namespace modcraft {

bool Window::init(int width, int height, const std::string& title) {
	if (!glfwInit()) {
		fprintf(stderr, "Failed to init GLFW\n");
		return false;
	}

#ifdef __EMSCRIPTEN__
	// WebGL 2.0 (GL ES 3.0)
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#else
	// OpenGL 4.1 core profile
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_SAMPLES, 4);
#endif

	m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
	if (!m_window) {
		fprintf(stderr, "Failed to create GLFW window\n");
		glfwTerminate();
		return false;
	}

	glfwMakeContextCurrent(m_window);

#ifdef __EMSCRIPTEN__
	// Emscripten provides GL ES 3.0 directly, no loader needed
	printf("WebGL 2.0 context ready\n");
#else
	// Load OpenGL functions via GLAD
	int version = gladLoadGL(glfwGetProcAddress);
	if (!version) {
		fprintf(stderr, "Failed to load OpenGL via GLAD\n");
		return false;
	}
	printf("OpenGL %d.%d loaded\n", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));
#endif

	// VSync
	glfwSwapInterval(1);

#ifndef __EMSCRIPTEN__
	// Native: capture mouse immediately (menu will release it)
	glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
#endif
	// Web: don't capture here -- pointer lock requires user gesture.
	// The game captures when entering play mode via a click.

	// Track size
	m_width = width;
	m_height = height;
	glfwSetWindowUserPointer(m_window, this);
	glfwSetFramebufferSizeCallback(m_window, framebufferCallback);

#ifndef __EMSCRIPTEN__
	// MSAA (not available in WebGL 2 via this path -- use canvas MSAA instead)
	glEnable(GL_MULTISAMPLE);
#endif

	return true;
}

void Window::shutdown() {
	if (m_window) {
		glfwDestroyWindow(m_window);
		m_window = nullptr;
	}
	glfwTerminate();
}

bool Window::shouldClose() const {
	return glfwWindowShouldClose(m_window);
}

void Window::swapBuffers() {
	glfwSwapBuffers(m_window);
}

void Window::pollEvents() {
	glfwPollEvents();
}

void Window::framebufferCallback(GLFWwindow* w, int width, int height) {
	auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
	self->m_width = width;
	self->m_height = height;
	glViewport(0, 0, width, height);
}

} // namespace modcraft

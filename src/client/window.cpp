#include "client/window.h"
#include <cstdio>

namespace aicraft {

bool Window::init(int width, int height, const std::string& title) {
	if (!glfwInit()) {
		fprintf(stderr, "Failed to init GLFW\n");
		return false;
	}

	// Request OpenGL 4.1 core profile
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

	// MSAA 4x for anti-aliasing
	glfwWindowHint(GLFW_SAMPLES, 4);

	m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
	if (!m_window) {
		fprintf(stderr, "Failed to create GLFW window\n");
		glfwTerminate();
		return false;
	}

	glfwMakeContextCurrent(m_window);

	// Load OpenGL functions via GLAD
	int version = gladLoadGL(glfwGetProcAddress);
	if (!version) {
		fprintf(stderr, "Failed to load OpenGL via GLAD\n");
		return false;
	}
	printf("OpenGL %d.%d loaded\n", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));

	// VSync
	glfwSwapInterval(1);

	// Capture mouse
	glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	// Track size
	m_width = width;
	m_height = height;
	glfwSetWindowUserPointer(m_window, this);
	glfwSetFramebufferSizeCallback(m_window, framebufferCallback);

	// Enable MSAA
	glEnable(GL_MULTISAMPLE);

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

} // namespace aicraft

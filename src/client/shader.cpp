#include "client/shader.h"
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <string>

namespace agentworld {

// Portability: replace #version line for WebGL (GLSL ES 3.00).
// Shaders are authored as GLSL 4.10 core. On web builds, we strip
// the version directive and prepend the ES equivalent + precision.
static std::string adaptShaderSource(const std::string& src) {
#ifdef __EMSCRIPTEN__
	std::string out = src;
	// Replace #version line
	auto pos = out.find("#version");
	if (pos != std::string::npos) {
		auto end = out.find('\n', pos);
		out.replace(pos, end - pos,
			"#version 300 es\nprecision mediump float;\nprecision mediump int;");
	}
	return out;
#else
	return src;
#endif
}

Shader::~Shader() {
	if (m_program)
		glDeleteProgram(m_program);
}

bool Shader::loadFromFile(const std::string& vertPath, const std::string& fragPath) {
	auto readFile = [](const std::string& path) -> std::string {
		std::ifstream f(path);
		if (!f.is_open()) {
			fprintf(stderr, "Cannot open shader: %s\n", path.c_str());
			return "";
		}
		std::stringstream ss;
		ss << f.rdbuf();
		return ss.str();
	};

	std::string vertSrc = adaptShaderSource(readFile(vertPath));
	std::string fragSrc = adaptShaderSource(readFile(fragPath));
	if (vertSrc.empty() || fragSrc.empty())
		return false;

	GLuint vert = compile(GL_VERTEX_SHADER, vertSrc.c_str());
	GLuint frag = compile(GL_FRAGMENT_SHADER, fragSrc.c_str());
	if (!vert || !frag)
		return false;

	m_program = glCreateProgram();
	glAttachShader(m_program, vert);
	glAttachShader(m_program, frag);
	glLinkProgram(m_program);

	int ok;
	glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[512];
		glGetProgramInfoLog(m_program, sizeof(log), nullptr, log);
		fprintf(stderr, "Shader link error: %s\n", log);
		return false;
	}

	glDeleteShader(vert);
	glDeleteShader(frag);
	return true;
}

GLuint Shader::compile(GLenum type, const char* source) {
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &source, nullptr);
	glCompileShader(s);

	int ok;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512];
		glGetShaderInfoLog(s, sizeof(log), nullptr, log);
		fprintf(stderr, "Shader compile error (%s): %s\n",
			type == GL_VERTEX_SHADER ? "vert" : "frag", log);
		return 0;
	}
	return s;
}

void Shader::use() const { glUseProgram(m_program); }
void Shader::setMat4(const char* n, const glm::mat4& m) const { glUniformMatrix4fv(glGetUniformLocation(m_program, n), 1, GL_FALSE, glm::value_ptr(m)); }
void Shader::setVec2(const char* n, const glm::vec2& v) const { glUniform2fv(glGetUniformLocation(m_program, n), 1, glm::value_ptr(v)); }
void Shader::setVec3(const char* n, const glm::vec3& v) const { glUniform3fv(glGetUniformLocation(m_program, n), 1, glm::value_ptr(v)); }
void Shader::setVec4(const char* n, const glm::vec4& v) const { glUniform4fv(glGetUniformLocation(m_program, n), 1, glm::value_ptr(v)); }
void Shader::setFloat(const char* n, float v) const { glUniform1f(glGetUniformLocation(m_program, n), v); }
void Shader::setInt(const char* n, int v) const { glUniform1i(glGetUniformLocation(m_program, n), v); }

} // namespace agentworld

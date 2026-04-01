#pragma once

#include "client/gl.h"
#include <glm/glm.hpp>
#include <string>

namespace agentworld {

class Shader {
public:
	~Shader();

	bool loadFromFile(const std::string& vertPath, const std::string& fragPath);
	void use() const;

	void setMat4(const char* name, const glm::mat4& m) const;
	void setVec3(const char* name, const glm::vec3& v) const;
	void setFloat(const char* name, float v) const;
	void setInt(const char* name, int v) const;

	GLuint id() const { return m_program; }

private:
	GLuint compile(GLenum type, const char* source);
	GLuint m_program = 0;
};

} // namespace agentworld

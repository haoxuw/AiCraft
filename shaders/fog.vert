#version 410 core

layout(location = 0) in vec3 aPos;

uniform mat4 uVP;
uniform vec3 uChunkPos;

out vec3 vWorldPos;

void main() {
	// Scale unit cube to chunk size (16x16x16) and position it
	vWorldPos = uChunkPos + aPos * 16.0;
	gl_Position = uVP * vec4(vWorldPos, 1.0);
}

#version 410 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in float aSize;

uniform mat4 uVP;

out vec4 vColor;

void main() {
	gl_Position = uVP * vec4(aPos, 1.0);
	gl_PointSize = aSize / gl_Position.w * 500.0;
	vColor = aColor;
}

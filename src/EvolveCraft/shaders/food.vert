#version 410 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 iPos;
layout (location = 3) in float iRadius;
layout (location = 4) in float iBob;

uniform mat4 uView;
uniform mat4 uProj;
uniform float uTime;

out vec3 vNormal;
out vec3 vWorldPos;

void main() {
	vec3 scaled = aPos * iRadius;
	float yLift = 0.15 * sin(uTime * 2.0 + iBob);
	vec3 worldPos = iPos + scaled + vec3(0, yLift, 0);
	vNormal = aNormal;
	vWorldPos = worldPos;
	gl_Position = uProj * uView * vec4(worldPos, 1.0);
}

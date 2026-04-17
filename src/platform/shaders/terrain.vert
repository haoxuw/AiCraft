#version 410 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in float aAO;
layout(location = 4) in float aShade;
layout(location = 5) in float aAlpha;
layout(location = 6) in float aGlow;

// Combined view*projection — RHI's SceneParams provides this as one matrix
// so backends don't have to decompose it.
uniform mat4 uViewProj;

out vec3 vColor;
out vec3 vNormal;
out vec3 vWorldPos;
out float vAO;
out float vShade;
out float vAlpha;
out float vGlow;

void main() {
	vWorldPos = aPos;
	vColor = aColor;
	vNormal = aNormal;
	vAO = aAO;
	vShade = aShade;
	vAlpha = aAlpha;
	vGlow = aGlow;

	gl_Position = uViewProj * vec4(aPos, 1.0);
}

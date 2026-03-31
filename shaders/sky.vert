#version 410 core

layout(location = 0) in vec2 aPos;

uniform mat4 uInvVP;

out vec3 vRayDir;

void main() {
	gl_Position = vec4(aPos, 0.999, 1.0);

	// Reconstruct world-space ray direction from clip-space position
	vec4 worldPos = uInvVP * vec4(aPos, 1.0, 1.0);
	vRayDir = normalize(worldPos.xyz / worldPos.w);
}

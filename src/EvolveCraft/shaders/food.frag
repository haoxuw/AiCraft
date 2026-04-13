#version 410 core

in vec3 vNormal;
in vec3 vWorldPos;
out vec4 FragColor;

uniform vec3 uCamera;
uniform float uTime;

void main() {
	vec3 sunDir = normalize(vec3(0.35, 0.9, 0.25));
	float diff = max(dot(normalize(vNormal), sunDir), 0.0);
	vec3 base = vec3(0.95, 0.78, 0.25);                // amber
	vec3 glow = vec3(1.0, 0.92, 0.55);
	vec3 col = mix(base, glow, 0.5 + 0.5 * sin(uTime * 3.0 + vWorldPos.x + vWorldPos.z));
	col *= 0.55 + 0.75 * diff;
	FragColor = vec4(col, 1.0);
}

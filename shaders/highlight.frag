#version 410 core

uniform vec4 uColor;
uniform vec3 uSunDir;
out vec4 fragColor;

in vec3 vNormal;

void main() {
	vec3 n = normalize(vNormal);

	// Hemisphere ambient: top faces brighter, bottom darker
	float hemiBlend = n.y * 0.5 + 0.5;
	float ambient = mix(0.40, 0.52, hemiBlend);

	// Directional sunlight
	float diffuse = 0.50 * max(0.0, dot(n, uSunDir));

	float lighting = min(ambient + diffuse, 1.0);

	fragColor = vec4(uColor.rgb * lighting, uColor.a);
}

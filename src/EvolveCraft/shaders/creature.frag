#version 410 core

in vec3 vNormal;
in vec3 vColor;
in vec3 vWorldPos;

uniform vec3 uCamera;
uniform float uTime;

out vec4 FragColor;

void main() {
	// Sun light (slightly tinted), plus rim light for readability on dark pond.
	vec3 sunDir = normalize(vec3(0.35, 0.9, 0.25));
	vec3 N = normalize(vNormal);
	float diff = max(dot(N, sunDir), 0.0);

	vec3 V = normalize(uCamera - vWorldPos);
	float rim = pow(1.0 - max(dot(N, V), 0.0), 2.0);

	vec3 ambient = vColor * 0.30;
	vec3 lit     = vColor * (0.55 + 0.65 * diff);
	vec3 rimLit  = mix(vColor, vec3(1.0), 0.4) * rim * 0.55;

	vec3 col = ambient + lit + rimLit;

	// gentle pulse so alive cells look alive
	col *= 1.0 + 0.06 * sin(uTime * 2.5 + vWorldPos.x * 0.3 + vWorldPos.z * 0.3);

	FragColor = vec4(col, 1.0);
}

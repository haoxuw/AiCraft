#version 410 core

in vec3 vNormal;
in vec3 vColor;
in vec3 vWorldPos;

uniform vec3 uCamera;
uniform float uTime;

out vec4 FragColor;

void main() {
	vec3 sunDir = normalize(vec3(0.25, 0.85, 0.30));
	vec3 N = normalize(vNormal);
	float diff = max(dot(N, sunDir), 0.0);

	vec3 V = normalize(uCamera - vWorldPos);
	float fresnel = pow(1.0 - max(dot(N, V), 0.0), 2.5);

	// subsurface-ish backlight so translucent-looking cells pop against dark pond
	float sss = pow(max(dot(-N, sunDir), 0.0), 1.5) * 0.25;

	vec3 ambient = vColor * 0.28;
	vec3 lit     = vColor * (0.50 + 0.70 * diff);
	vec3 rimLit  = mix(vColor, vec3(1.0), 0.55) * fresnel * 0.55;
	vec3 backlit = vColor * sss;

	vec3 col = ambient + lit + rimLit + backlit;

	// subtle breathing pulse tied to spatial coords + time
	float pulse = 0.04 * sin(uTime * 2.0 + vWorldPos.x * 0.25 + vWorldPos.z * 0.25);
	col *= 1.0 + pulse;

	FragColor = vec4(col, 1.0);
}

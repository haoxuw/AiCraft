#version 410 core

uniform vec4 uColor;
uniform vec3 uSunDir;
uniform sampler2D uPartTex;
uniform int uUseTexture; // 0 = flat color, 1 = textured

in vec3 vNormal;
in vec2 vUV;

out vec4 fragColor;

void main() {
	// Base color: texture or flat uniform
	vec4 baseColor;
	if (uUseTexture == 1) {
		baseColor = texture(uPartTex, vUV);
		if (baseColor.a < 0.01) discard;
	} else {
		baseColor = uColor;
	}

	vec3 n = normalize(vNormal);

	// Hemisphere ambient: top faces brighter, bottom darker
	float hemiBlend = n.y * 0.5 + 0.5;
	float ambient = mix(0.40, 0.52, hemiBlend);

	// Directional sunlight
	float diffuse = 0.50 * max(0.0, dot(n, uSunDir));

	float lighting = min(ambient + diffuse, 1.0);

	fragColor = vec4(baseColor.rgb * lighting, baseColor.a);
}

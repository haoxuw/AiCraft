#version 410 core

in vec4 vColor;
out vec4 fragColor;

void main() {
	// Soft circle from point sprite
	vec2 c = gl_PointCoord * 2.0 - 1.0;
	float d = dot(c, c);
	if (d > 1.0) discard;
	float alpha = vColor.a * (1.0 - d * 0.5);
	fragColor = vec4(vColor.rgb, alpha);
}

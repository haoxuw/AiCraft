#version 410 core

uniform float uAlpha;

out vec4 fragColor;

void main() {
	fragColor = vec4(0.0, 0.0, 0.0, uAlpha);
}

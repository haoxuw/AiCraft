#version 410 core

layout(location = 0) in vec2 aPos;

uniform float uAspect;

void main() {
	// Correct for aspect ratio so crosshair stays square
	gl_Position = vec4(aPos.x / uAspect, aPos.y, 0.0, 1.0);
}

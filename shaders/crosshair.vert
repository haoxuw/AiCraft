#version 410 core

layout(location = 0) in vec2 aPos;

uniform float uAspect;
uniform vec2 uCenter;  // screen-space offset (0,0 = center)

void main() {
	gl_Position = vec4(aPos.x / uAspect + uCenter.x, aPos.y + uCenter.y, 0.0, 1.0);
}

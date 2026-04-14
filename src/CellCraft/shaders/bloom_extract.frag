#version 410 core
in vec2 v_uv;
out vec4 frag;

uniform sampler2D u_scene;
uniform float u_threshold;

void main() {
	vec3 c = texture(u_scene, v_uv).rgb;
	// Soft thresholding — smoother than max().
	float brightness = dot(c, vec3(0.299, 0.587, 0.114));
	float k = smoothstep(u_threshold, u_threshold + 0.15, brightness);
	frag = vec4(c * k, 1.0);
}

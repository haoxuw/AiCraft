#version 450

// Translucent ghost box. Flat color with alpha blend; a subtle edge
// darken makes the box silhouette read against any backdrop.

layout(push_constant) uniform PC {
	mat4  viewProj;
	vec4  aabbMin;
	vec4  aabbMax;
	vec4  color;    // rgba
} pc;

layout(location = 0) in vec3 vLocalPos;
layout(location = 0) out vec4 outColor;

void main() {
	// Distance to nearest face in local-cube coords. A thin band near the
	// edges (|coord - 0.5| ~ 0.5) gets a brighter tint so the silhouette
	// is easy to see even when the preview covers dark terrain.
	vec3 d = abs(vLocalPos - 0.5);
	float edge = max(d.x, max(d.y, d.z));  // 0 at center, 0.5 at face
	float rim  = smoothstep(0.42, 0.50, edge);   // brighter near borders
	vec3 rgb = pc.color.rgb * (0.85 + 0.35 * rim);
	outColor = vec4(rgb, pc.color.a);
}

#version 410 core

// Instanced creature rendering.
// Vertex attrs:  aPos (unit-sphere ish), aNormal
// Instance attrs: iPos (world), iRadius, iColor, iYaw
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 iPos;
layout (location = 3) in float iRadius;
layout (location = 4) in vec3 iColor;
layout (location = 5) in float iYaw;

uniform mat4 uView;
uniform mat4 uProj;

out vec3 vNormal;
out vec3 vColor;
out vec3 vWorldPos;

void main() {
	float s = sin(iYaw), c = cos(iYaw);
	// rotate around Y by yaw, scale by radius, translate to instance pos
	vec3 scaled = aPos * iRadius;
	vec3 rotated = vec3( c*scaled.x + s*scaled.z,
	                     scaled.y,
	                    -s*scaled.x + c*scaled.z );
	vec3 worldPos = iPos + rotated;
	vWorldPos = worldPos;
	vColor    = iColor;
	vNormal   = normalize(vec3( c*aNormal.x + s*aNormal.z,
	                            aNormal.y,
	                           -s*aNormal.x + c*aNormal.z ));
	gl_Position = uProj * uView * vec4(worldPos, 1.0);
}

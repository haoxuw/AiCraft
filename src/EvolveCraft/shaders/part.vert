#version 410 core

// Instanced part rendering. Each instance is a full 4x4 model matrix + color.
// Attribute slots 2..5 = four columns of the model matrix.
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec4 iModelCol0;
layout (location = 3) in vec4 iModelCol1;
layout (location = 4) in vec4 iModelCol2;
layout (location = 5) in vec4 iModelCol3;
layout (location = 6) in vec3 iColor;

uniform mat4 uView;
uniform mat4 uProj;

out vec3 vNormal;
out vec3 vColor;
out vec3 vWorldPos;

void main() {
	mat4 model = mat4(iModelCol0, iModelCol1, iModelCol2, iModelCol3);
	vec4 wp = model * vec4(aPos, 1.0);
	vWorldPos = wp.xyz;
	vColor = iColor;
	// Transform normal by upper-left 3x3 of model. Minor error under
	// non-uniform scale (cones/cylinders) is acceptable for this style.
	vNormal = normalize(mat3(model) * aNormal);
	gl_Position = uProj * uView * wp;
}

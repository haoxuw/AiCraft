#version 450

// Chunk-mesh terrain fragment shader. Faithful port of CivCraft's
// src/CivCraft/shaders/terrain.frag — same lighting/AO/fog/glass logic, but
// receives uniforms via a push-constant block instead of GL uniforms.

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vWorldPos;
layout(location = 3) in float vAO;
layout(location = 4) in float vShade;
layout(location = 5) in float vAlpha;
layout(location = 6) in float vGlow;

layout(push_constant) uniform PC {
	mat4 viewProj;
	vec4 camPos;       // xyz, w=time
	vec4 sunDir;       // xyz, w=sunStrength
	vec4 fog;          // rgb=fogColor, a=fogStart
	vec4 fogExtra;     // x=fogEnd
} pc;

layout(location = 0) out vec4 fragColor;

float hash(vec3 p) {
	p = fract(p * vec3(443.8975, 397.2973, 491.1871));
	p += dot(p, p.yzx + 19.19);
	return fract((p.x + p.y) * p.z);
}

float noise3D(vec3 p) {
	vec3 i = floor(p);
	vec3 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(
		mix(mix(hash(i),              hash(i + vec3(1,0,0)), f.x),
		    mix(hash(i + vec3(0,1,0)), hash(i + vec3(1,1,0)), f.x), f.y),
		mix(mix(hash(i + vec3(0,0,1)), hash(i + vec3(1,0,1)), f.x),
		    mix(hash(i + vec3(0,1,1)), hash(i + vec3(1,1,1)), f.x), f.y),
		f.z
	);
}

void main() {
	vec3 camPos       = pc.camPos.xyz;
	float time        = pc.camPos.w;
	vec3 sunDir       = pc.sunDir.xyz;
	float sunStrength = pc.sunDir.w;
	vec3 fogColor     = pc.fog.rgb;
	float fogStart    = pc.fog.a;
	float fogEnd      = pc.fogExtra.x;

	vec3 blockPos = floor(vWorldPos + 0.001);
	vec3 localPos = fract(vWorldPos + 0.001);

	float blockHash = hash(blockPos);
	float colorVariation = (blockHash - 0.5) * 0.14;

	float grain;
	if (abs(vNormal.y) > 0.5) {
		grain = noise3D(vWorldPos * 3.5)  * 0.06
		      + noise3D(vWorldPos * 9.0)  * 0.03
		      + noise3D(vWorldPos * 22.0) * 0.015;
	} else {
		grain = noise3D(vWorldPos * vec3(3.0, 10.0, 3.0)) * 0.06
		      + noise3D(vWorldPos * vec3(7.0, 22.0, 7.0)) * 0.025;
	}

	vec3 edgeDist = min(localPos, 1.0 - localPos);
	float edgeFactor;
	if (abs(vNormal.y) > 0.5) {
		edgeFactor = smoothstep(0.0, 0.06, min(edgeDist.x, edgeDist.z));
	} else if (abs(vNormal.x) > 0.5) {
		edgeFactor = smoothstep(0.0, 0.06, min(edgeDist.y, edgeDist.z));
	} else {
		edgeFactor = smoothstep(0.0, 0.06, min(edgeDist.x, edgeDist.y));
	}
	edgeFactor = mix(0.82, 1.0, edgeFactor);

	vec3 baseColor = vColor + colorVariation + grain;
	baseColor = clamp(baseColor, 0.0, 1.0);
	baseColor *= edgeFactor;

	float lum = dot(baseColor, vec3(0.299, 0.587, 0.114));
	baseColor = mix(vec3(lum), baseColor, 1.45);
	baseColor = clamp(baseColor, 0.0, 1.0);

	float sunDot = max(dot(vNormal, sunDir), 0.0);
	float ambient = 0.15 + 0.30 * sunStrength;
	float diffuse = ambient + (1.0 - ambient) * sunDot * sunStrength;

	vec3 lit = baseColor * diffuse * vShade * vAO;

	float dawnDusk = sunStrength * (1.0 - sunStrength) * 4.0;
	lit += baseColor * sunDot * sunStrength * (0.10 * vec3(1.0, 0.85, 0.60)
	     + dawnDusk * 0.14 * vec3(1.0, 0.50, 0.18));

	lit += baseColor * (1.0 - sunStrength) * 0.03 * vec3(0.3, 0.4, 0.8);

	if (vGlow > 0.5) {
		float t = time * 0.5;
		float n1 = noise3D(vWorldPos * 2.2 + vec3(t * 0.28, 0.0,    t * 0.18));
		float n2 = noise3D(vWorldPos * 5.5 - vec3(0.0,      t * 0.4, t * 0.12));
		float veins = 0.3 + 0.7 * pow(clamp(n1 * 0.6 + n2 * 0.4, 0.0, 1.0), 0.5);
		float phase = 0.5 + 0.5 * sin(t * 0.32 + vWorldPos.x * 0.14 + vWorldPos.z * 0.14);
		vec3 c1 = vec3(0.30, 0.04, 0.55);
		vec3 c2 = vec3(0.04, 0.44, 0.62);
		vec3 surfaceColor = mix(c1, c2, phase) * veins;
		lit = surfaceColor * diffuse * vShade * vAO;
	}

	float dist = length(vWorldPos - camPos);
	float fog = smoothstep(fogStart, fogEnd, dist);
	lit = mix(lit, fogColor, fog);

	float alpha = vAlpha;
	if (vAlpha < 0.5) {
		vec3 viewDir = normalize(camPos - vWorldPos);
		float cosTheta = abs(dot(vNormal, viewDir));
		float fresnel = pow(1.0 - cosTheta, 2.5);
		lit = mix(lit, vec3(0.9, 0.97, 1.0), fresnel * 0.6);
		alpha = mix(vAlpha, 0.85, fresnel * 0.7);
	}

	fragColor = vec4(lit, alpha);
}

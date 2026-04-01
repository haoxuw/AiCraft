#pragma once

#include <cmath>

namespace agentworld {

// Simple hash-based value noise for terrain generation.
// All functions are inline so they can live in a header with no .cpp.

inline float hashFloat(int x, int z) {
	unsigned int n = (unsigned int)(x * 73856093) ^ (unsigned int)(z * 19349663);
	n = (n << 13) ^ n;
	return (float)((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 2147483647.0f;
}

inline float smoothNoise2D(float x, float z) {
	int ix = (int)std::floor(x);
	int iz = (int)std::floor(z);
	float fx = x - ix;
	float fz = z - iz;
	fx = fx * fx * (3.0f - 2.0f * fx);
	fz = fz * fz * (3.0f - 2.0f * fz);

	float a = hashFloat(ix, iz);
	float b = hashFloat(ix + 1, iz);
	float c = hashFloat(ix, iz + 1);
	float d = hashFloat(ix + 1, iz + 1);
	return a + (b - a) * fx + (c - a) * fz + (a - b - c + d) * fx * fz;
}

// Multi-octave terrain height. Returns world Y of surface at (x, z).
inline float terrainHeight(int seed, float x, float z) {
	float h = 0.0f;
	h += (smoothNoise2D(x * 0.008f + seed, z * 0.008f) - 0.5f) * 30.0f;
	h += (smoothNoise2D(x * 0.025f + seed * 2, z * 0.025f) - 0.5f) * 12.0f;
	h += (smoothNoise2D(x * 0.07f + seed * 3, z * 0.07f) - 0.5f) * 4.0f;
	h += (smoothNoise2D(x * 0.15f + seed * 4, z * 0.15f) - 0.5f) * 1.5f;
	return h;
}

} // namespace agentworld

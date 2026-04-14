#pragma once

#include <cmath>

namespace civcraft {

// ============================================================
// Hash-based value noise (seed-independent, reproducible)
// ============================================================

inline float hashFloat(int x, int z) {
	unsigned int n = (unsigned int)(x * 73856093) ^ (unsigned int)(z * 19349663);
	n = (n << 13) ^ n;
	return (float)((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 2147483647.0f;
}

// Smooth (bi-cubic) value noise in [0, 1].
inline float smoothNoise2D(float x, float z) {
	int ix = (int)std::floor(x);
	int iz = (int)std::floor(z);
	float fx = x - ix;
	float fz = z - iz;
	fx = fx * fx * (3.0f - 2.0f * fx);  // smoothstep
	fz = fz * fz * (3.0f - 2.0f * fz);

	float a = hashFloat(ix,     iz);
	float b = hashFloat(ix + 1, iz);
	float c = hashFloat(ix,     iz + 1);
	float d = hashFloat(ix + 1, iz + 1);
	return a + (b - a) * fx + (c - a) * fz + (a - b - c + d) * fx * fz;
}

// ============================================================
// Terrain parameters (matches WorldPyConfig::terrain fields)
// ============================================================
struct TerrainParams {
	float continentScale     = 0.004f;
	float continentAmplitude = 18.0f;
	float hillScale          = 0.024f;
	float hillAmplitude      = 6.0f;
	float detailScale        = 0.09f;
	float detailAmplitude    = 2.2f;
	float microScale         = 0.26f;
	float microAmplitude     = 0.7f;
};

// ============================================================
// Natural terrain height: continental-style, no spike mountains.
//
// Architecture (four additive layers):
//
//  1. Continental base — low frequency.  Divided into biome bands via
//     a non-linear shaping curve so that:
//       35% of terrain is ocean   (h = -6 to  0)
//       20% is coastal lowland    (h =  0 to  4)
//       27% is rolling plains     (h =  4 to  9)
//       12% is highland           (h =  9 to 14)
//        6% is mountain peak      (h = 14 to 20)
//
//  2. Hill layer — medium frequency, amplitude proportional to
//     continent height.  Plains stay smooth; highlands become rough.
//
//  3. Detail layer — fine surface undulation.
//
//  4. Micro layer — sub-block grain.
// ============================================================

inline float naturalTerrainHeight(int seed, float x, float z,
                                   const TerrainParams& p = TerrainParams{}) {
	// Per-seed offsets (shift noise phase so different seeds give different worlds)
	float ox = (float)((seed * 1234567) & 0xFFFF) * 0.01f;
	float oz = (float)((seed * 7654321) & 0xFFFF) * 0.01f;

	// ── Layer 1: Continental base ─────────────────────────────
	// Two-pass blend for larger-scale variation without extra frequency
	float c0 = smoothNoise2D(x * p.continentScale + ox,         z * p.continentScale + oz);
	float c1 = smoothNoise2D(x * p.continentScale * 2.1f + ox,  z * p.continentScale * 2.1f + oz);
	float continent = c0 * 0.65f + c1 * 0.35f;  // [0, 1]

	// Non-linear shaping: most terrain is plains (0-9), mountains are rare
	float shaped;
	if (continent < 0.35f) {
		// Ocean floor
		shaped = (continent / 0.35f) * 6.0f - 6.0f;
	} else if (continent < 0.55f) {
		// Coastal lowland
		shaped = ((continent - 0.35f) / 0.20f) * 4.0f;
	} else if (continent < 0.82f) {
		// Rolling plains / hills
		shaped = 4.0f + ((continent - 0.55f) / 0.27f) * 5.0f;
	} else if (continent < 0.94f) {
		// Highland
		shaped = 9.0f + ((continent - 0.82f) / 0.12f) * 5.0f;
	} else {
		// Mountain peak (top 6%)
		shaped = 14.0f + ((continent - 0.94f) / 0.06f) * 6.0f;
	}
	// Apply amplitude scaling
	float h = shaped * (p.continentAmplitude / 20.0f);

	// ── Layer 2: Hills (amplitude grows with continent height) ─
	float landFactor = std::max(0.0f, (continent - 0.40f) / 0.60f);
	float hills = (smoothNoise2D(x * p.hillScale + ox * 2.1f, z * p.hillScale + oz * 2.1f) - 0.5f)
	              * p.hillAmplitude * landFactor;

	// ── Layer 3: Surface detail ────────────────────────────────
	float detail = (smoothNoise2D(x * p.detailScale + ox * 3.7f, z * p.detailScale + oz * 3.7f) - 0.5f)
	               * p.detailAmplitude;

	// ── Layer 4: Micro detail ─────────────────────────────────
	float micro = (smoothNoise2D(x * p.microScale + ox * 5.3f, z * p.microScale + oz * 5.3f) - 0.5f)
	              * p.microAmplitude;

	return h + hills + detail + micro;
}

// Legacy free function kept for any code that calls terrainHeight(seed, x, z) directly.
// Routes to naturalTerrainHeight with default parameters.
inline float terrainHeight(int seed, float x, float z) {
	return naturalTerrainHeight(seed, x, z);
}

} // namespace civcraft

#pragma once

// Per-agent execution jitter: frustration temperature + personality seed +
// RNG. Owned by Agent, consumed by action handlers when they make a choice
// that could be worth varying between retries — "pick which leaf to chop",
// "which anchor to try next", "where to route around a wedged path". The
// idea is that a failed tick often repeats the exact same unsuccessful
// attempt; a touch of randomness lets the next tick hit something subtly
// different without changing the high-level plan.
//
// Not exposed to Python. Python decide_plan() is deterministic from its inputs;
// jitter is strictly an executor-layer concern.
//
// State
//   temperature  — ramps on failure sites (+kBumpStep), resets on success.
//                  Caps at 1.0 so four consecutive failures saturate.
//   personality  — deterministic [0, kPersonalityMax] from hash(eid).
//                  Restart-stable; two villagers at the same tree pick
//                  different leaves even with zero frustration.
//   effective    — min(1, temperature + personality), what callers actually
//                  feed into random draws.

#include "logic/entity.h"      // EntityId
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <random>

namespace civcraft {

class Jitter {
public:
	static constexpr float kBumpStep       = 0.25f;  // per-failure ramp
	static constexpr float kMaxTemp        = 1.0f;
	static constexpr float kPersonalityMax = 0.10f;  // lifetime floor per entity

	explicit Jitter(EntityId eid)
	  : m_rng(hashSeed(eid))
	  , m_personality(
	      std::uniform_real_distribution<float>(0.0f, kPersonalityMax)(m_rng))
	{}

	float temperature() const { return m_temp; }
	float personality() const { return m_personality; }
	float effective()   const { return std::min(kMaxTemp, m_temp + m_personality); }

	// Returns true iff the temperature actually changed (callers use this
	// to gate PATHLOG lines so the log only reflects transitions, not noise).
	bool bump() {
		float prev = m_temp;
		m_temp = std::min(kMaxTemp, m_temp + kBumpStep);
		return m_temp != prev;
	}
	bool cool() {
		if (m_temp == 0.0f) return false;
		m_temp = 0.0f;
		return true;
	}

	// Weighted-random index over `count` items ordered by preference
	// (index 0 = best). Weight[i] = 1 / (i + 1)^k where k shrinks as
	// effective() grows: effective=0 → k=∞ → always 0; effective=1 → k=0
	// → uniform. Mid-range is biased toward the front but not dogmatic.
	std::size_t pickIndex(std::size_t count) {
		if (count == 0) return 0;
		float j = effective();
		if (j <= 1e-3f || count == 1) return 0;
		// k is the exponent on rank-inverse weights. Map j ∈ (0, 1] →
		// k ∈ [kMinExp, kMaxExp]; keep kMaxExp finite (8) so even
		// "cold" jitter has a non-zero chance of choosing index 1+.
		constexpr float kMinExp = 0.0f;
		constexpr float kMaxExp = 8.0f;
		float k = kMaxExp - (kMaxExp - kMinExp) * j;

		std::vector<float> weights(count);
		float total = 0.0f;
		for (std::size_t i = 0; i < count; ++i) {
			float w = 1.0f / std::pow(static_cast<float>(i + 1), k);
			weights[i] = w;
			total     += w;
		}
		std::uniform_real_distribution<float> u(0.0f, total);
		float roll = u(m_rng);
		float acc  = 0.0f;
		for (std::size_t i = 0; i < count; ++i) {
			acc += weights[i];
			if (roll <= acc) return i;
		}
		return count - 1;
	}

	// Random XZ cell offset, magnitude bounded by round(effective * maxRadius).
	// Used when a Navigator dead-end suggests re-planning from a different
	// entry cell. Returns (0, 0) when jitter is cold.
	glm::ivec2 offsetXZ(int maxRadius) {
		int r = static_cast<int>(std::round(effective() * maxRadius));
		if (r <= 0) return {0, 0};
		std::uniform_int_distribution<int> d(-r, r);
		return {d(m_rng), d(m_rng)};
	}

private:
	// FNV-1a-ish mix: gives well-distributed seeds for small EntityId values
	// while staying deterministic across restarts for a given id.
	static std::uint32_t hashSeed(EntityId eid) {
		std::uint32_t x = static_cast<std::uint32_t>(eid) * 2654435761u;
		x ^= x >> 16;
		x *= 2246822519u;
		x ^= x >> 13;
		x *= 3266489917u;
		x ^= x >> 16;
		return x ? x : 1u;
	}

	std::mt19937 m_rng;
	float        m_temp        = 0.0f;
	float        m_personality = 0.0f;
};

} // namespace civcraft

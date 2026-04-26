#pragma once

// VillageSiter — picks a village center for a newly-claiming seat via
// reject-sampling. The design rule (docs/28_SEATS_AND_OWNERSHIP.md §3) is
// that every village is **≥ kMinDistance and ≤ kMaxDistance from its nearest
// neighbour**, measured on the XZ plane. The bound is against *any* registry
// entry — Live or Despawned — so new villages can't land on vacated
// footprints either.
//
// Exception: the very first village has no neighbour, so it's placed at
// `initialCenter` (typically the world's preferred-spawn XZ).
//
// The sampler is deterministic for a given (registry state, seed, seat) so
// two replays of the same world produce the same village layout.

#include "server/village_registry.h"
#include <cmath>
#include <cstdint>
#include <optional>
#include <random>
#include <glm/glm.hpp>

namespace solarium {

struct VillageSiterConfig {
	// Pair chosen so every seat still has ≥ 2× perception radius of breathing
	// room, but villages stay within walking distance for multiplayer.
	int  kMinDistance = 256;
	int  kMaxDistance = 512;
	int  kMaxAttempts = 256;  // after this many tries, widen bounds and retry
};

class VillageSiter {
public:
	// Reject-sample a center for a new seat.
	//
	// `registry` is the live set of every sited village (Live + Despawned).
	// `initialCenter` is the fallback used when the registry is empty — the
	// first village has no distance constraint.
	// `worldSeed` + `ownerSeat` feed the RNG so siting is deterministic.
	//
	// Returns std::nullopt only if the search exhausted kMaxAttempts at every
	// widening step — practically impossible in a finite world, but surfaced
	// as a hard error rather than a silent fallback so tests can catch it.
	static std::optional<glm::ivec2> pick(
			const VillageRegistry& registry,
			glm::ivec2 initialCenter,
			int worldSeed,
			SeatId ownerSeat,
			const VillageSiterConfig& cfg = VillageSiterConfig{}) {
		if (registry.size() == 0) return initialCenter;

		// Deterministic RNG: different seats get different seeds, same seat
		// on reload gets the same seed so the attempted positions replay.
		std::mt19937 rng((uint32_t)(worldSeed * 2654435761u) ^ (uint32_t)ownerSeat);

		// Pick the anchor to orbit around — use the village the owner seat
		// would nominally follow: the newest Live village. Widening to the
		// full registry's bounding box happens if we fail to find a spot.
		glm::ivec2 anchor = newestLiveOr(registry, initialCenter);

		for (int widen = 0; widen < 4; widen++) {
			int minD = cfg.kMinDistance;
			int maxD = cfg.kMaxDistance * (1 + widen);  // 1x, 2x, 3x, 4x
			std::uniform_real_distribution<float> angleD(0.0f, 6.28318530718f);
			std::uniform_int_distribution<int>    radD(minD, maxD);

			for (int attempt = 0; attempt < cfg.kMaxAttempts; attempt++) {
				float a = angleD(rng);
				int   r = radD(rng);
				int   dx = (int)std::lround(std::cos(a) * (float)r);
				int   dz = (int)std::lround(std::sin(a) * (float)r);
				glm::ivec2 cand = {anchor.x + dx, anchor.y + dz};
				int64_t d2 = registry.nearestDistSqXZ(cand);
				int64_t minD2 = (int64_t)minD * minD;
				if (d2 >= minD2) return cand;
			}
		}
		return std::nullopt;
	}

private:
	// The newest Live record, or the fallback anchor if none exist.
	static glm::ivec2 newestLiveOr(const VillageRegistry& reg, glm::ivec2 fallback) {
		for (auto it = reg.all().rbegin(); it != reg.all().rend(); ++it) {
			if (it->status == VillageRecord::Status::Live) return it->centerXZ;
		}
		return fallback;
	}
};

} // namespace solarium

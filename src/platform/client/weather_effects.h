#pragma once

// Weather visual effects — class hierarchy.
//
// The server broadcasts a weather "kind" string ("clear", "rain", "snow",
// "leaves") plus intensity and wind. The client decides what to *render*
// based on local conditions (season, what's around the player). This split
// keeps the server display-free (Rule 5) while letting the client refuse to
// render leaves on a cobblestone plaza in summer.
//
// Each WeatherEffect has two responsibilities:
//   shouldActivate(ctx) — is this effect appropriate right now, here?
//   emit(ctx, particles) — append particles to the buffer (8 floats each:
//                          pos.xyz, size, rgb, alpha)

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

#include "logic/constants.h"

namespace solarium {
class ChunkSource;
class BlockRegistry;
}

namespace solarium::vk {

struct WeatherCtx {
	std::string kind;        // server's weather kind: "clear", "rain", "snow", "leaves"
	float       intensity = 0.0f;   // 0..1
	glm::vec2   wind{0.0f, 0.0f};
	glm::vec3   cameraPos{0.0f, 0.0f, 0.0f};
	solarium::Season season = solarium::Season::Spring;
	solarium::ChunkSource*           chunks   = nullptr;
	const solarium::BlockRegistry*   blockReg = nullptr;
	float       wallTime = 0.0f;
	float       sunStr = 1.0f;       // 0 deep night … 1 full day; lets ambient
	                                  // effects (fireflies) gate themselves on TOD
};

class WeatherEffect {
public:
	virtual ~WeatherEffect() = default;
	virtual const char* name() const = 0;
	virtual bool shouldActivate(const WeatherCtx& ctx) const = 0;
	virtual void emit(const WeatherCtx& ctx, std::vector<float>& particles) const = 0;
};

class RainEffect : public WeatherEffect {
public:
	const char* name() const override { return "rain"; }
	bool shouldActivate(const WeatherCtx& ctx) const override;
	void emit(const WeatherCtx& ctx, std::vector<float>& particles) const override;
};

class SnowEffect : public WeatherEffect {
public:
	const char* name() const override { return "snow"; }
	bool shouldActivate(const WeatherCtx& ctx) const override;
	void emit(const WeatherCtx& ctx, std::vector<float>& particles) const override;
};

// Activates only in autumn AND when at least one leaf block is within scan
// radius of the camera. Particle count reduced from the legacy 140 to 60 ×
// intensity, and HDR tints lowered to LDR so leaves read as drifting flakes
// rather than glowing embers.
class LeavesEffect : public WeatherEffect {
public:
	const char* name() const override { return "leaves"; }
	bool shouldActivate(const WeatherCtx& ctx) const override;
	void emit(const WeatherCtx& ctx, std::vector<float>& particles) const override;
};

// Activates at night near trees, in the warm seasons (spring/summer). HDR
// yellow-green tint so each particle blooms into a glowing pinpoint. Each
// firefly drifts slowly and flickers via a per-seed sine wave. Independent
// of the server's weather kind — fireflies appear regardless of rain/snow/
// clear (real fireflies don't care about a drizzle).
class FirefliesEffect : public WeatherEffect {
public:
	const char* name() const override { return "fireflies"; }
	bool shouldActivate(const WeatherCtx& ctx) const override;
	void emit(const WeatherCtx& ctx, std::vector<float>& particles) const override;
};

// Singleton list — main loop iterates and dispatches.
const std::vector<std::unique_ptr<WeatherEffect>>& allWeatherEffects();

} // namespace solarium::vk

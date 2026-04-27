#include "client/weather_effects.h"

#include "logic/block_registry.h"
#include "logic/chunk_source.h"

#include <algorithm>
#include <cmath>

namespace solarium::vk {

namespace {

// Push a single particle into the renderer-visible 8-float layout
// (pos.xyz, size, rgb, alpha).
void pushP(std::vector<float>& v, glm::vec3 p, float size,
           glm::vec3 rgb, float alpha) {
	v.push_back(p.x); v.push_back(p.y); v.push_back(p.z);
	v.push_back(size);
	v.push_back(rgb.x); v.push_back(rgb.y); v.push_back(rgb.z);
	v.push_back(alpha);
}

// Solid-block test for collision-skip on emitting particles.
bool insideSolid(const WeatherCtx& ctx, const glm::vec3& p) {
	if (!ctx.chunks || !ctx.blockReg) return false;
	int bx = (int)std::floor(p.x);
	int by = (int)std::floor(p.y);
	int bz = (int)std::floor(p.z);
	solarium::BlockId bid = ctx.chunks->getBlock(bx, by, bz);
	if (bid == 0) return false;
	return ctx.blockReg->get(bid).solid;
}

// "leaves", "leaves_red", "leaves_gold", … all start with "leaves" in
// their string_id. That's how the artifacts namespace seasonal variants.
bool isLeafBlock(solarium::BlockId bid, const solarium::BlockRegistry& reg) {
	if (bid == 0) return false;
	const std::string& name = reg.get(bid).string_id;
	return name.compare(0, 6, "leaves") == 0;
}

// Coarse 8×8 XZ grid scan around the camera, sampling Y from −2 to +12 to
// catch tree canopies above and below the eye. Cached for ~0.5 s OR until
// the camera moves more than 8 blocks horizontally — emitting 60 particles
// per frame already calls insideSolid() 60×, so we don't want to add a
// 64-block scan on top of every frame.
struct TreeNearbyCache {
	glm::vec3 lastEye{1e9f, 1e9f, 1e9f};
	float     lastTime = -1e9f;
	bool      cached   = false;
};

bool hasTreeNearby(const WeatherCtx& ctx, float radius) {
	static thread_local TreeNearbyCache cache;
	glm::vec3 d = ctx.cameraPos - cache.lastEye;
	float distSq = d.x * d.x + d.z * d.z;
	if (distSq < 64.0f && std::fabs(ctx.wallTime - cache.lastTime) < 0.5f) {
		return cache.cached;
	}
	cache.lastEye  = ctx.cameraPos;
	cache.lastTime = ctx.wallTime;

	const int gridN = 8;
	const int yLow = -2, yHigh = 12;
	glm::vec3 eye = ctx.cameraPos;
	bool found = false;
	for (int gx = 0; gx < gridN && !found; gx++) {
		for (int gz = 0; gz < gridN && !found; gz++) {
			float u = (gridN > 1) ? ((float)gx / (float)(gridN - 1)) : 0.5f;
			float v = (gridN > 1) ? ((float)gz / (float)(gridN - 1)) : 0.5f;
			float sx = eye.x + (u * 2.0f - 1.0f) * radius;
			float sz = eye.z + (v * 2.0f - 1.0f) * radius;
			int bx = (int)std::floor(sx);
			int bz = (int)std::floor(sz);
			for (int dy = yLow; dy <= yHigh && !found; dy++) {
				int by = (int)std::floor(eye.y) + dy;
				solarium::BlockId bid = ctx.chunks->getBlock(bx, by, bz);
				if (isLeafBlock(bid, *ctx.blockReg)) found = true;
			}
		}
	}
	cache.cached = found;
	return found;
}

} // namespace

// ── Rain ───────────────────────────────────────────────────────────────

bool RainEffect::shouldActivate(const WeatherCtx& ctx) const {
	return ctx.kind == "rain" && ctx.intensity > 0.01f;
}

void RainEffect::emit(const WeatherCtx& ctx, std::vector<float>& out) const {
	const int   count     = (int)(320.0f * ctx.intensity);
	const float R         = 22.0f;
	const float hiY       = 20.0f, loY = -4.0f;
	const float span      = hiY - loY;
	const float fallSpeed = 22.0f;
	const float period    = span / std::max(fallSpeed, 0.1f);
	const glm::vec3 col(0.55f, 0.70f, 0.95f);
	const glm::vec3 eye = ctx.cameraPos;

	for (int k = 0; k < count; k++) {
		float seed   = (float)k;
		float ang    = std::fmod(seed * 2.3998f, 6.2831853f);
		float rad    = R * std::sqrt(std::fmod(seed * 0.137f, 1.0f));
		float phase  = std::fmod(seed * 0.091f, 1.0f);
		float t      = std::fmod(ctx.wallTime / period + phase, 1.0f);
		float y      = hiY - t * span;
		float wShift = t * period;
		glm::vec3 p  = eye + glm::vec3(
			std::cos(ang) * rad + ctx.wind.x * wShift,
			y,
			std::sin(ang) * rad + ctx.wind.y * wShift);
		if (insideSolid(ctx, p)) continue;
		pushP(out, p, 0.05f, col, 0.55f * ctx.intensity);
	}
}

// ── Snow ───────────────────────────────────────────────────────────────

bool SnowEffect::shouldActivate(const WeatherCtx& ctx) const {
	return ctx.kind == "snow" && ctx.intensity > 0.01f;
}

void SnowEffect::emit(const WeatherCtx& ctx, std::vector<float>& out) const {
	const int   count     = (int)(240.0f * ctx.intensity);
	const float R         = 20.0f;
	const float hiY       = 18.0f, loY = -4.0f;
	const float span      = hiY - loY;
	const float fallSpeed = 3.5f;
	const float period    = span / std::max(fallSpeed, 0.1f);
	const glm::vec3 col(1.10f, 1.15f, 1.25f);
	const glm::vec3 eye = ctx.cameraPos;

	for (int k = 0; k < count; k++) {
		float seed     = (float)k;
		float ang      = std::fmod(seed * 2.3998f, 6.2831853f);
		float rad      = R * std::sqrt(std::fmod(seed * 0.137f, 1.0f));
		float phase    = std::fmod(seed * 0.091f, 1.0f);
		float t        = std::fmod(ctx.wallTime / period + phase, 1.0f);
		float y        = hiY - t * span;
		float wShift   = t * period;
		float tumbleX  = std::sin(ctx.wallTime * 1.3f + seed * 4.7f) * 0.6f;
		float tumbleZ  = std::cos(ctx.wallTime * 1.1f + seed * 3.1f) * 0.6f;
		glm::vec3 p    = eye + glm::vec3(
			std::cos(ang) * rad + ctx.wind.x * wShift + tumbleX,
			y,
			std::sin(ang) * rad + ctx.wind.y * wShift + tumbleZ);
		if (insideSolid(ctx, p)) continue;
		pushP(out, p, 0.09f, col, 0.70f * ctx.intensity);
	}
}

// ── Leaves ─────────────────────────────────────────────────────────────

bool LeavesEffect::shouldActivate(const WeatherCtx& ctx) const {
	if (ctx.kind != "leaves") return false;
	if (ctx.intensity < 0.01f) return false;
	if (ctx.season != solarium::Season::Autumn) return false;
	return hasTreeNearby(ctx, 30.0f);
}

void LeavesEffect::emit(const WeatherCtx& ctx, std::vector<float>& out) const {
	// Reduced count: was 140 × intensity, now 60 × intensity.
	const int   count   = (int)(60.0f * ctx.intensity);
	const float span    = 22.0f;
	const float period  = span / 2.0f;
	const glm::vec3 eye = ctx.cameraPos;

	for (int k = 0; k < count; k++) {
		float seed   = (float)k;
		int   tint   = (int)std::fmod(seed * 7.17f, 3.0f);
		// Tones brought into LDR (was 2.2, 2.5, 1.8 in red — bloomed to flares)
		glm::vec3 col = (tint == 0) ? glm::vec3(1.10f, 0.55f, 0.15f)
		             : (tint == 1) ? glm::vec3(1.20f, 0.40f, 0.12f)
		                           : glm::vec3(0.95f, 0.70f, 0.25f);
		float ang     = std::fmod(seed * 2.3998f, 6.2831853f);
		float rad     = 18.0f * std::sqrt(std::fmod(seed * 0.137f, 1.0f));
		float phase   = std::fmod(seed * 0.091f, 1.0f);
		float t       = std::fmod(ctx.wallTime / period + phase, 1.0f);
		float y       = 18.0f - t * span;
		float wShift  = t * period;
		float tumbleX = std::sin(ctx.wallTime * 0.9f + seed * 3.7f) * 1.2f;
		float tumbleZ = std::cos(ctx.wallTime * 0.8f + seed * 2.3f) * 1.2f;
		glm::vec3 p   = eye + glm::vec3(
			std::cos(ang) * rad + ctx.wind.x * wShift + tumbleX,
			y,
			std::sin(ang) * rad + ctx.wind.y * wShift + tumbleZ);
		if (insideSolid(ctx, p)) continue;
		pushP(out, p, 0.12f, col, 0.55f * ctx.intensity);
	}
}

// ── Fireflies ──────────────────────────────────────────────────────────

bool FirefliesEffect::shouldActivate(const WeatherCtx& ctx) const {
	// Night only — sunStr ramps from 1 (full day) to 0 (deep night). 0.18
	// catches late dusk through pre-dawn.
	if (ctx.sunStr > 0.18f) return false;
	// Warm-season insects only — autumn/winter is too cold for fireflies.
	if (ctx.season != solarium::Season::Spring &&
	    ctx.season != solarium::Season::Summer) return false;
	// Need vegetation nearby — same coarse leaf-block scan as leaves.
	return hasTreeNearby(ctx, 30.0f);
}

void FirefliesEffect::emit(const WeatherCtx& ctx, std::vector<float>& out) const {
	// Sparse — 30 fireflies feels alive without becoming a swarm.
	const int count = 30;
	const glm::vec3 eye = ctx.cameraPos;
	// Tiny size + HDR warm yellow-green so the bloom pass turns each into a
	// glowing pinpoint. RGB chosen so green dominates slightly (Photinus
	// pyralis: ~565 nm, just on the green side of yellow).
	const glm::vec3 col(1.6f, 1.95f, 0.40f);

	for (int k = 0; k < count; k++) {
		float seed   = (float)k;
		// Anchor — random angle + radius, deterministic per seed
		float ang    = std::fmod(seed * 2.3998f, 6.2831853f);
		float rad    = 12.0f * std::sqrt(std::fmod(seed * 0.137f, 1.0f));
		// Slow horizontal drift — different freqs/seeds so each firefly meanders
		float driftX = std::sin(ctx.wallTime * 0.30f + seed * 1.7f) * 1.5f;
		float driftZ = std::cos(ctx.wallTime * 0.40f + seed * 2.3f) * 1.5f;
		// Vertical bob — faster, smaller amplitude
		float bobY   = std::sin(ctx.wallTime * 1.7f + seed * 3.1f) * 0.4f;
		// Y anchor — distributed across eye-2 to eye+4 so fireflies aren't
		// all stacked at one altitude
		float yAnchor = std::fmod(seed * 0.443f, 1.0f) * 6.0f - 2.0f;

		glm::vec3 p = eye + glm::vec3(
			std::cos(ang) * rad + driftX,
			yAnchor + bobY,
			std::sin(ang) * rad + driftZ);
		if (insideSolid(ctx, p)) continue;

		// Flicker — squared sine for sharp "blink" rather than soft sine.
		// Per-seed phase + frequency so they don't pulse in unison.
		float phase   = std::fmod(seed * 1.71f, 6.2831853f);
		float freq    = 4.0f + std::fmod(seed * 0.13f, 1.0f) * 4.0f;
		float flicker = 0.5f + 0.5f * std::sin(ctx.wallTime * freq + phase);
		flicker      *= flicker;
		// Fade fireflies in as night deepens (sunStr 0.18 → 0.00)
		float dayFade = 1.0f - std::min(ctx.sunStr / 0.18f, 1.0f);

		pushP(out, p, 0.06f, col, 0.85f * flicker * dayFade);
	}
}

// ── Registry ───────────────────────────────────────────────────────────

const std::vector<std::unique_ptr<WeatherEffect>>& allWeatherEffects() {
	static const std::vector<std::unique_ptr<WeatherEffect>> effects = []{
		std::vector<std::unique_ptr<WeatherEffect>> v;
		v.emplace_back(std::make_unique<RainEffect>());
		v.emplace_back(std::make_unique<SnowEffect>());
		v.emplace_back(std::make_unique<LeavesEffect>());
		v.emplace_back(std::make_unique<FirefliesEffect>());
		return v;
	}();
	return effects;
}

} // namespace solarium::vk

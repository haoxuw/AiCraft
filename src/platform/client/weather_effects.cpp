#include "client/weather_effects.h"

#include "logic/block_registry.h"
#include "logic/chunk_source.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

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

// ── Shared falling-particle field ──────────────────────────────────────
// Rain, snow, and leaves all share the same cylinder-around-camera layout:
// deterministic per-seed angle/radius placement, time-modulo fall from hiY
// down to loY at fallSpeed, wind drift, optional XZ tumble. Only their
// counts, sizes, colors, and tumble parameters differ — captured here in
// FallingFieldSpec. Fireflies are NOT a falling field (they hover) so they
// keep their own loop.
struct FallingFieldSpec {
	int       count       = 0;
	float     radius      = 18.0f;   // horizontal extent around camera
	float     hiY         = 18.0f;   // top of the spawn band (relative to eye)
	float     loY         = -4.0f;   // bottom — particle wraps when it crosses
	float     fallSpeed   = 5.0f;    // m/s — drives the time-modulo period
	float     size        = 0.08f;   // particle radius in world units
	float     alpha       = 0.50f;   // already-multiplied-by-intensity alpha
	float     tumbleAmp   = 0.0f;    // 0 = no tumble; else XZ jitter amplitude
	glm::vec2 tumbleFreq  {1.3f, 1.1f};  // angular freq for X / Z
	glm::vec2 tumbleSeedF {4.7f, 3.1f};  // per-seed phase factor for X / Z
};

// Emit a falling-particle cylinder around the camera. Color picked from
// `palette` by deterministic seed: single-element palette → constant color;
// multi-element → per-particle hash pick (used by leaves' three tints).
void emitFallingField(const WeatherCtx& ctx, const FallingFieldSpec& s,
                      std::initializer_list<glm::vec3> palette,
                      std::vector<float>& out) {
	const float span   = s.hiY - s.loY;
	const float period = span / std::max(s.fallSpeed, 0.1f);
	const glm::vec3 eye = ctx.cameraPos;
	const auto* paletteData = palette.begin();
	const int   paletteN    = (int)palette.size();

	for (int k = 0; k < s.count; k++) {
		float seed   = (float)k;
		float ang    = std::fmod(seed * 2.3998f, 6.2831853f);
		float rad    = s.radius * std::sqrt(std::fmod(seed * 0.137f, 1.0f));
		float phase  = std::fmod(seed * 0.091f, 1.0f);
		float t      = std::fmod(ctx.wallTime / period + phase, 1.0f);
		float y      = s.hiY - t * span;
		float wShift = t * period;

		float tumbleX = 0.0f, tumbleZ = 0.0f;
		if (s.tumbleAmp > 0.0f) {
			tumbleX = std::sin(ctx.wallTime * s.tumbleFreq.x + seed * s.tumbleSeedF.x) * s.tumbleAmp;
			tumbleZ = std::cos(ctx.wallTime * s.tumbleFreq.y + seed * s.tumbleSeedF.y) * s.tumbleAmp;
		}

		glm::vec3 p = eye + glm::vec3(
			std::cos(ang) * rad + ctx.wind.x * wShift + tumbleX,
			y,
			std::sin(ang) * rad + ctx.wind.y * wShift + tumbleZ);
		if (insideSolid(ctx, p)) continue;

		glm::vec3 col;
		if (paletteN == 1) {
			col = paletteData[0];
		} else {
			int idx = (int)std::fmod(seed * 7.17f, (float)paletteN);
			col = paletteData[idx];
		}
		pushP(out, p, s.size, col, s.alpha);
	}
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
	const FallingFieldSpec spec{
		.count     = (int)(320.0f * ctx.intensity),
		.radius    = 22.0f,
		.hiY       = 20.0f,
		.loY       = -4.0f,
		.fallSpeed = 22.0f,
		.size      = 0.05f,
		.alpha     = 0.55f * ctx.intensity,
		// no tumble — rain falls straight
	};
	emitFallingField(ctx, spec, { glm::vec3(0.55f, 0.70f, 0.95f) }, out);
}

// ── Snow ───────────────────────────────────────────────────────────────

bool SnowEffect::shouldActivate(const WeatherCtx& ctx) const {
	return ctx.kind == "snow" && ctx.intensity > 0.01f;
}

void SnowEffect::emit(const WeatherCtx& ctx, std::vector<float>& out) const {
	const FallingFieldSpec spec{
		.count       = (int)(240.0f * ctx.intensity),
		.radius      = 20.0f,
		.hiY         = 18.0f,
		.loY         = -4.0f,
		.fallSpeed   = 3.5f,
		.size        = 0.09f,
		.alpha       = 0.70f * ctx.intensity,
		.tumbleAmp   = 0.6f,
		.tumbleFreq  = {1.3f, 1.1f},
		.tumbleSeedF = {4.7f, 3.1f},
	};
	emitFallingField(ctx, spec, { glm::vec3(1.10f, 1.15f, 1.25f) }, out);
}

// ── Leaves ─────────────────────────────────────────────────────────────

bool LeavesEffect::shouldActivate(const WeatherCtx& ctx) const {
	if (ctx.kind != "leaves") return false;
	if (ctx.intensity < 0.01f) return false;
	if (ctx.season != solarium::Season::Autumn) return false;
	return hasTreeNearby(ctx, 30.0f);
}

void LeavesEffect::emit(const WeatherCtx& ctx, std::vector<float>& out) const {
	// Reduced count: was 140 × intensity, now 60 × intensity. Tones in LDR
	// (was 2.2, 2.5, 1.8 in red — used to bloom into glowing flares).
	const FallingFieldSpec spec{
		.count       = (int)(60.0f * ctx.intensity),
		.radius      = 18.0f,
		.hiY         = 18.0f,
		.loY         = -4.0f,
		.fallSpeed   = 2.0f,
		.size        = 0.12f,
		.alpha       = 0.55f * ctx.intensity,
		.tumbleAmp   = 1.2f,
		.tumbleFreq  = {0.9f, 0.8f},
		.tumbleSeedF = {3.7f, 2.3f},
	};
	emitFallingField(ctx, spec, {
		glm::vec3(1.10f, 0.55f, 0.15f),  // amber-orange
		glm::vec3(1.20f, 0.40f, 0.12f),  // deep red-orange
		glm::vec3(0.95f, 0.70f, 0.25f),  // gold
	}, out);
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

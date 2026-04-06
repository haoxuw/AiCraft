#pragma once

#include "client/gl.h"
#include "client/camera.h"
#include "shared/entity.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace agentica {

class TextRenderer;

// ── Scale constants — all floating text uses these, never raw floats ─────────
constexpr float kFTScaleWorld  = 1.80f;  // world-projected: damage, heal, pickup, break
constexpr float kFTScaleFPS    = 1.90f;  // FPS near-crosshair combat
constexpr float kFTScaleCrit   = 2.60f;  // crits: always larger
constexpr float kFTScalePickup = 1.50f;  // pickup / block-break (slightly smaller than combat)

// ── Event source — describes what kind of text this is ──────────────────────
enum class FloatSource {
	DamageDealt,   // hit an enemy  (entity-anchored, filtered by mode)
	DamageTaken,   // player got hit (screen-pos per mode)
	Heal,          // HP restored   (entity-anchored)
	Pickup,        // item collected (HUD lane, Counter by item type)
	BlockBreak,    // block mined   (HUD lane)
};

// ── Submit one of these per game event ──────────────────────────────────────
struct FloatTextEvent {
	FloatSource   source      = FloatSource::DamageDealt;
	EntityId      targetId    = ENTITY_NONE; // entity the text belongs to
	glm::vec3     worldPos    = {};           // world-space spawn position
	std::string   text;                       // label: item name, empty for combat
	float         value       = 0.f;          // numeric: damage, count, heal
	std::string   coalesceKey;               // Counter sub-key (item type ID for pickups)
	bool          isCrit      = false;        // crit: 1.4× scale + orange
	bool          isDying     = false;        // killing blow: red
	bool          isSplash    = false;        // also emit a short per-hit flash alongside Counter
};

// ── FloatingTextManager ──────────────────────────────────────────────────────
//
// WoW/DST-style floating text. ONE entry point: add(). All text is world-anchored
// — it appears at the entity or block that changed, drifts upward and fades.
// No fixed HUD corner. Everything floats in the game world.
//
// Storage model — Counter map (unordered_map<EntryKey, Entry>):
//   One persistent slot per (entity, source, subKey). Accumulates values.
//   Splash vector: brief per-hit flashes independent of the Counter.
//
// Mode policy:
//   FPS  — DamageDealt near crosshair; DamageTaken bottom-centre.
//          Pickup/BlockBreak drift up from screen-centre.
//   TPS/RPG/RTS — all text world-projected above entity/block position.
//   RTS  — entity-anchored for selected entities only.
//   Mode switch — combat entries fast-expire (0.15 s).
//
class FloatingTextManager {
public:
	// Single entry point for ALL floating text — no other code calls drawText
	void add(const FloatTextEvent& ev);

	// Call once per frame before render
	void update(float dt, CameraMode mode);

	// Notify that an entity was removed — detaches its anchored entries so they
	// free-float from their last known position instead of snapping to origin
	void onEntityRemoved(EntityId id);

	// Render everything
	void render(const Camera& cam, float aspect, CameraMode mode,
	            TextRenderer& text,
	            const std::vector<EntityId>& selectedEntities);

private:
	// ── Counter map storage ─────────────────────────────────────────────────
	struct EntryKey {
		EntityId    entityId;
		FloatSource source;
		std::string subKey;      // item-type id for pickups; empty for combat
		bool operator==(const EntryKey& o) const {
			return entityId == o.entityId && source == o.source && subKey == o.subKey;
		}
	};
	struct EntryKeyHash {
		size_t operator()(const EntryKey& k) const {
			size_t h = std::hash<EntityId>{}(k.entityId);
			h ^= std::hash<int>{}((int)k.source) + 0x9e3779b9 + (h<<6) + (h>>2);
			h ^= std::hash<std::string>{}(k.subKey) + 0x9e3779b9 + (h<<6) + (h>>2);
			return h;
		}
	};

	struct Entry {
		FloatSource  source;
		EntityId     entityId;
		glm::vec3    anchorWorld;  // frozen on entity removal
		glm::vec2    screenDrift;  // accumulated NDC upward drift
		std::string  text;         // current display string (rebuilt on accumulation)
		std::string  baseLabel;    // non-numeric label for reformatting ("Oak Log")
		glm::vec4    color;
		float        accum     = 0.f;   // running numeric total
		bool         isCrit    = false; // true if any contributing hit was a crit
		bool         freeFloat = false; // entity removed — drift freely
		float        ttl;
		float        maxTtl;
		float        animAge   = 0.f;  // age since last pop trigger (resets on accumulation)
		float        scale     = 0.f;  // driven by animAge: 0 → peak → 1 (bounce)
	};

	// Per-hit flash: short-lived, not accumulated, independent of Counter
	struct Splash {
		FloatSource  source;
		EntityId     entityId;
		glm::vec3    anchorWorld;
		glm::vec2    screenDrift;
		std::string  text;
		glm::vec4    color;
		bool         isCrit;
		float        ttl;
		float        scale      = 0.f;
		float        horizJitter;  // small random X spread so hits fan out
	};

	std::unordered_map<EntryKey, Entry, EntryKeyHash> m_entries;
	std::vector<Splash>  m_splashes;
	CameraMode           m_prevMode = CameraMode::FirstPerson;

	// ── Timing ───────────────────────────────────────────────────────────────
	// How long each counter entry lives. Resets to maxTtl on every accumulation,
	// so a pickup counter stays up as long as you keep collecting.
	static constexpr float kCombatTtl      =  3.0f;   // damage / heal counter lifetime (s)
	static constexpr float kPickupTtl      = 10.0f;   // pickup / block-break counter lifetime (s)
	static constexpr float kSplashTtl      =  0.55f;  // per-hit flash lifetime (s)
	// Alpha fade-in: text is fully opaque once this much time has elapsed.
	static constexpr float kPopInTime      =  0.12f;  // (s)
	// Camera mode switch: surviving combat counters fast-expire so stale text
	// from the old view doesn't bleed into the new one.
	static constexpr float kModeSwitchTtl  =  0.15f;  // (s)

	// ── Bounce animation ─────────────────────────────────────────────────────
	// Entry scale goes 0 → kPopPeak in kPopPeakTime, then settles to 1.0 by
	// kPopSettleTime. Re-triggered every time the counter accumulates a new hit.
	static constexpr float kPopPeakTime    =  0.10f;  // time to reach peak (s)
	static constexpr float kPopSettleTime  =  0.28f;  // time to settle back to 1.0 (s)
	static constexpr float kPopPeak        =  1.45f;  // peak overshoot scale factor

	// ── FPS panel layout (NDC coords, origin = screen centre) ────────────────
	// Three non-overlapping panels; each is a stacked list, one row per counter.
	static constexpr float kFpsRowH        =  0.11f;  // vertical spacing between rows
	// Damage taken — bottom-centre, stacks upward
	static constexpr float kFpsTakenX     =  0.00f;
	static constexpr float kFpsTakenBaseY = -0.70f;
	// Damage dealt / Heal — near crosshair, stacks upward
	static constexpr float kFpsDealtX     =  0.00f;
	static constexpr float kFpsDealtBaseY =  0.05f;
	// Pickup / Block break — upper-right panel, stacks downward
	static constexpr float kFpsLootX      =  0.55f;
	static constexpr float kFpsLootBaseY  =  0.70f;
	// FPS splash anchor offsets (match the panel base positions above)
	static constexpr float kFpsSplashDmgY = -0.70f;  // DamageTaken splash base Y
	static constexpr float kFpsSplashHitY =  0.05f;  // DamageDealt/Heal splash base Y

	// ── World-anchored (TPS/RPG/RTS) ─────────────────────────────────────────
	// NDC minimum separation before push-apart kicks in.
	static constexpr float kOverlapDist    =  0.09f;
	// World-space Y offset added to entity/block position before projection,
	// so text appears above the target rather than inside it.
	static constexpr float kAnchorLiftY   =  0.15f;
	// NDC drift rate — how fast counters float upward per second.
	static constexpr float kDriftRate     =  0.26f;   // counter (NDC/s)
	static constexpr float kSplashDriftRate = 0.50f;  // splash (NDC/s, faster)
	// Splash scale relative to the base entry scale.
	static constexpr float kSplashScaleMul =  0.75f;
	static constexpr float kSplashAlphaMul =  0.80f;
	// Splash fade-out window (last N seconds of kSplashTtl spent fading out).
	static constexpr float kSplashFadeOut  =  0.20f;
	// Counter fade-out window.
	static constexpr float kCounterFadeOut =  0.45f;
	bool        worldToNDC(const Camera& cam, float aspect,
	                       glm::vec3 wp, glm::vec2& out) const;
	std::string formatDisplay(FloatSource src, float accum,
	                          const std::string& label) const;
	glm::vec4   colorFor(FloatSource src, bool isCrit, bool isDying) const;
	float       ttlFor(FloatSource src) const;
	void        resolveOverlap(std::vector<glm::vec2>& positions) const;
};

} // namespace agentica

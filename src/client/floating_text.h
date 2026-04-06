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

	// Timing / limits
	static constexpr float kCombatTtl     =  3.0f;
	static constexpr float kPickupTtl     = 10.0f;  // counter persists; resets on each new pickup
	static constexpr float kSplashTtl     =  0.55f;
	static constexpr float kPopInTime     =  0.12f;  // alpha fade-in duration
	static constexpr float kPopPeakTime   =  0.10f;  // time to reach scale peak
	static constexpr float kPopSettleTime =  0.28f;  // time to settle back to 1.0
	static constexpr float kPopPeak       =  1.45f;  // overshoot factor
	static constexpr float kModeSwitchTtl =  0.15f;
	static constexpr float kOverlapDist   = 0.09f;  // NDC min separation
	bool        worldToNDC(const Camera& cam, float aspect,
	                       glm::vec3 wp, glm::vec2& out) const;
	std::string formatDisplay(FloatSource src, float accum,
	                          const std::string& label) const;
	glm::vec4   colorFor(FloatSource src, bool isCrit, bool isDying) const;
	float       ttlFor(FloatSource src) const;
	void        resolveOverlap(std::vector<glm::vec2>& positions) const;
};

} // namespace agentica

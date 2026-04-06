#pragma once

#include "client/gl.h"
#include "shared/entity.h"
#include "client/camera.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>

namespace agentica {

class TextRenderer;  // forward-declare to avoid GL header conflicts

enum class FloatTextType {
	DamageDealt,  // Damage done to an enemy — entity-anchored in TPS/RPG/RTS
	DamageTaken,  // Damage the player received — HUD lane
	Pickup,       // Item picked up — HUD lane (coalesced by item name)
	BlockBreak,   // Block mined — HUD lane
	Heal,         // HP restored — entity-anchored or HUD lane
};

struct FloatTextEvent {
	FloatTextType type;
	EntityId targetId = ENTITY_NONE;  // entity anchor (ENTITY_NONE = HUD lane)
	glm::vec3 worldPos = {};          // world-space spawn position
	std::string text;                 // display string e.g. "-15", "+1 Wood"
	std::string coalesceKey;          // same key + type → accumulate value
	glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
};

// FloatingTextSystem
//
// Unified screen-space notification system. Handles four camera modes:
//   FPS   — HUD lane (left side): pickups/breaks; near-crosshair: damage dealt
//   TPS   — entity-anchored (projected + upward drift); HUD for player events
//   RPG   — same as TPS
//   RTS   — entity-anchored only for selected entities; HUD for player events
//
// Entity slots: up to kMaxEntitySlots per entity, Y-stacked to avoid overlap.
// HUD lane:     up to kMaxHudSlots rows, left column.
class FloatingTextSystem {
public:
	void add(FloatTextEvent ev);
	void update(float dt, CameraMode mode);
	void render(const Camera& cam, float aspect, CameraMode mode,
	            TextRenderer& text,
	            const std::vector<EntityId>& selectedEntities);

private:
	struct Entry {
		FloatTextType type;
		EntityId entityId;            // ENTITY_NONE → HUD lane
		glm::vec3 anchorWorld;        // world position at spawn
		glm::vec2 screenDrift;        // accumulated NDC drift (upward)
		std::string text;
		std::string coalesceKey;
		glm::vec4 color;
		float ttl;        // remaining lifetime (seconds)
		float maxTtl;     // total lifetime
		float scale;      // current display scale (pop-in)
		int hudSlot;      // -1 = entity-anchored; ≥0 = HUD row
	};

	static constexpr int kMaxEntitySlots = 4;
	static constexpr int kMaxHudSlots    = 6;
	static constexpr float kEntityTtl   = 1.5f;
	static constexpr float kHudTtl      = 2.5f;
	static constexpr float kPopInTime   = 0.12f;  // scale 0→1 over this duration

	std::vector<Entry> m_entries;

	// Track next available HUD slot slot per type key to stack upward
	// (just pick the lowest free index at add() time)
	int nextHudSlot() const;

	// Project world position to NDC [-1,1]. Returns false if behind camera.
	bool worldToNDC(const Camera& cam, float aspect, glm::vec3 wp,
	                glm::vec2& out) const;
};

} // namespace agentica

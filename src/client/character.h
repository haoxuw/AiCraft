#pragma once

/**
 * Character definition and management.
 *
 * Each character is a BoxModel (body parts with animation) plus metadata.
 * The head part uses a texture atlas for face + hair instead of
 * separate geometry, eliminating Z-fighting and reducing draw calls.
 */

#include "client/box_model.h"
#include "client/face.h"
#include "shared/inventory.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace modcraft {

struct CharacterStats {
	int strength = 3;     // 1-5 stars
	int stamina = 3;
	int agility = 3;
	int intelligence = 3;
};

// Where an item can be visually attached on a character.
struct EquipSlot {
	glm::vec3 offset;         // position relative to model origin
	glm::vec3 halfSize;       // default size of equipped item visual
	glm::vec3 pivot = {0,0,0};// animation pivot (same as the limb it's on)
	glm::vec3 swingAxis = {1,0,0};
	float swingAmplitude = 0;
	float swingPhase = 0;
	float swingSpeed = 1;
};

struct CharacterDef {
	std::string id;
	std::string name;
	std::string description;
	BoxModel model;
	CharacterStats stats;

	float jumpVelocity = 8.3f;

	glm::vec3 skinColor = {0.85f, 0.70f, 0.55f};
	glm::vec3 hairColor = {0.25f, 0.18f, 0.12f};
	glm::vec3 headOffset = {0, 1.75f, 0};
	glm::vec3 headHalfSize = {0.25f, 0.25f, 0.25f};

	// Equipment attachment points (scaled to each character's proportions)
	EquipSlot slotBack;       // jetpack, shield, quiver
	EquipSlot slotHead;       // hat, helmet, crown
	EquipSlot slotRightHand;  // sword, pickaxe, tool
	EquipSlot slotLeftHand;   // shield, torch, offhand
	EquipSlot slotFeet;       // boots, skates
};

// One box of an equipped item's visual model (CLIENT-SIDE).
struct ItemPiece {
	glm::vec4 color;
	glm::vec3 offsetLocal = {0,0,0};  // offset from slot center
	glm::vec3 halfSize = {0.05f, 0.05f, 0.05f};
};

// A particle emitter definition (CLIENT-SIDE).
// Fires when an entity property (trigger) is truthy.
// Mirrors Python: modcraft.items.base.ParticleEmitter
struct ParticleEmitterDef {
	glm::vec3 offset = {0,0,0};       // relative to slot center
	int rate = 4;                      // particles per frame
	glm::vec3 velocity = {0,-5,0};    // base particle velocity
	float velocitySpread = 0.5f;       // random spread
	// Color layers: emitted in order (core → outer)
	std::vector<glm::vec4> colors = {
		{1, 0.95f, 0.8f, 1},
		{1, 0.75f, 0.15f, 1},
		{1, 0.35f, 0.05f, 0.9f},
	};
	float lifeMin = 0.08f, lifeMax = 0.25f;
	float sizeMin = 0.03f, sizeMax = 0.06f;
};

// Active effect: particle emitters triggered by an entity property.
// Mirrors Python: modcraft.items.base.ActiveEffect
struct ActiveEffectDef {
	std::string trigger;               // entity prop name, e.g. "jetpack_active"
	std::vector<ParticleEmitterDef> emitters;
};

// Complete client-side visual for an equipped item.
// Mirrors Python: modcraft.items.base.ItemVisual
struct ItemVisual {
	std::string itemId;
	std::string slotName;              // "back", "head", "right_hand", etc.
	std::vector<ItemPiece> pieces;     // geometry
	std::vector<ActiveEffectDef> effects; // particle effects
};

class CharacterManager {
public:
	void add(CharacterDef def) {
		m_characters.push_back(std::move(def));
	}

	void addItemVisual(ItemVisual vis) {
		m_itemVisuals[vis.itemId] = std::move(vis);
	}

	int count() const { return (int)m_characters.size(); }
	const CharacterDef& get(int i) const { return m_characters[i]; }
	const CharacterDef& selected() const { return m_characters[m_selectedIndex]; }
	int selectedIndex() const { return m_selectedIndex; }
	void select(int i) { if (i >= 0 && i < count()) m_selectedIndex = i; }

	// Build the final model: body + face texture + equipped items
	BoxModel buildModel(int charIndex, const FacePattern& face,
	                     const Inventory* inventory = nullptr) const {
		auto& cdef = m_characters[charIndex];
		BoxModel model = cdef.model;

		// Head texture
		if (!model.parts.empty()) {
			GLuint tex = generateHeadTexture(face, cdef.skinColor, cdef.hairColor);
			model.parts[0].texture = tex;
		}

		// Append equipment visuals from inventory
		if (inventory) {
			for (auto& [itemId, cnt] : inventory->items()) {
				auto it = m_itemVisuals.find(itemId);
				if (it == m_itemVisuals.end()) continue;
				auto& vis = it->second;

				const EquipSlot* slot = getSlot(cdef, vis.slotName);
				if (!slot) continue;

				for (auto& piece : vis.pieces) {
					BodyPart part;
					part.offset = slot->offset + piece.offsetLocal;
					part.halfSize = piece.halfSize;
					part.color = piece.color;
					part.pivot = slot->pivot;
					part.swingAxis = slot->swingAxis;
					part.swingAmplitude = slot->swingAmplitude;
					part.swingPhase = slot->swingPhase;
					part.swingSpeed = slot->swingSpeed;
					model.parts.push_back(part);
				}
			}
		}

		return model;
	}

	BoxModel buildSelectedModel(const FacePattern& face,
	                             const Inventory* inventory = nullptr) const {
		return buildModel(m_selectedIndex, face, inventory);
	}

	// Get active particle emitters for items in inventory.
	// triggerCheck: callable(string trigger_name) -> bool (is it active?)
	struct ActiveEmitter {
		EquipSlot slot;
		ParticleEmitterDef emitter;
	};

	template<typename TriggerFn>
	std::vector<ActiveEmitter> getActiveEffects(int charIndex,
	                                             const Inventory& inv,
	                                             TriggerFn triggerCheck) const {
		std::vector<ActiveEmitter> result;
		auto& cdef = m_characters[charIndex];

		for (auto& [itemId, cnt] : inv.items()) {
			auto it = m_itemVisuals.find(itemId);
			if (it == m_itemVisuals.end()) continue;
			auto& vis = it->second;
			const EquipSlot* slot = getSlot(cdef, vis.slotName);
			if (!slot) continue;

			for (auto& effect : vis.effects) {
				if (!triggerCheck(effect.trigger)) continue;
				for (auto& emitter : effect.emitters) {
					result.push_back({*slot, emitter});
				}
			}
		}
		return result;
	}

private:
	static const EquipSlot* getSlot(const CharacterDef& c, const std::string& name) {
		if (name == "back")       return &c.slotBack;
		if (name == "head")       return &c.slotHead;
		if (name == "right_hand") return &c.slotRightHand;
		if (name == "left_hand")  return &c.slotLeftHand;
		if (name == "feet")       return &c.slotFeet;
		return nullptr;
	}

	std::vector<CharacterDef> m_characters;
	std::unordered_map<std::string, ItemVisual> m_itemVisuals;
	int m_selectedIndex = 0;
};

} // namespace modcraft

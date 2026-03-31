#pragma once

/**
 * Character definition and management.
 *
 * Each character is a BoxModel (body parts with animation) plus metadata.
 * Characters are defined as Python source files in characters/ and mirrored
 * as C++ builtins. When Python embedding is added, the .py files will be
 * loaded directly at runtime.
 *
 * The CharacterManager holds all available characters and tracks which
 * one the player has selected.
 */

#include "client/model.h"
#include "common/face.h"
#include <string>
#include <vector>

namespace aicraft {

struct CharacterStats {
	int strength = 3;     // 1-5 stars
	int stamina = 3;
	int agility = 3;
	int intelligence = 3;
};

struct CharacterDef {
	std::string id;           // "base:blue_knight"
	std::string name;         // "Blue Knight"
	std::string description;  // short flavor text
	BoxModel model;           // geometry + animation params (body only, no face)
	CharacterStats stats;     // RPG stats (1-5 stars)

	// Head geometry info for face overlay positioning.
	glm::vec3 headOffset = {0, 1.75f, 0};
	glm::vec3 headHalfSize = {0.25f, 0.25f, 0.25f};
	glm::vec3 headPivot = {0, 1.5f, 0};
	float headSwingAmp = 5.0f;
	float headSwingSpeed = 2.0f;
};

class CharacterManager {
public:
	void add(CharacterDef def) {
		m_characters.push_back(std::move(def));
	}

	int count() const { return (int)m_characters.size(); }
	const CharacterDef& get(int i) const { return m_characters[i]; }
	const CharacterDef& selected() const { return m_characters[m_selectedIndex]; }
	int selectedIndex() const { return m_selectedIndex; }
	void select(int i) { if (i >= 0 && i < count()) m_selectedIndex = i; }

	// Build the final model: character body + face overlay parts
	BoxModel buildModel(int charIndex, const FacePattern& face) const {
		auto& cdef = m_characters[charIndex];
		BoxModel model = cdef.model; // copy body

		// Generate face overlay parts and append
		auto faceParts = faceToBodyParts(face,
			cdef.headOffset, cdef.headHalfSize,
			cdef.headPivot, {1,0,0},
			cdef.headSwingAmp, 0, cdef.headSwingSpeed);

		model.parts.insert(model.parts.end(), faceParts.begin(), faceParts.end());
		return model;
	}

	BoxModel buildSelectedModel(const FacePattern& face) const {
		return buildModel(m_selectedIndex, face);
	}

private:
	std::vector<CharacterDef> m_characters;
	int m_selectedIndex = 0;
};

} // namespace aicraft

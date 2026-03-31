#pragma once

/**
 * Pixel art face overlay system.
 *
 * Each face is a 16x16 grid of palette indices. Non-zero pixels become
 * tiny colored slabs on the front of the character's head, giving
 * detailed facial features (eyes, mouth, nose, blush, whiskers, etc.)
 * without requiring a texture mapping system.
 *
 * Faces are independent of character body -- any face can be applied
 * to any character for maximum combinatorial variety.
 */

#include "client/model.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <array>

namespace aicraft {

// Palette indices used in face grids.
// Each face can override colors, but these are the defaults.
namespace FC {
	constexpr uint8_t _ = 0;   // transparent
	constexpr uint8_t K = 1;   // black (pupils, outlines)
	constexpr uint8_t W = 2;   // white (eye whites, highlights)
	constexpr uint8_t P = 3;   // pink (blush, inner mouth, inner ears)
	constexpr uint8_t R = 4;   // red
	constexpr uint8_t G = 5;   // green
	constexpr uint8_t B = 6;   // blue
	constexpr uint8_t Y = 7;   // yellow
	constexpr uint8_t N = 8;   // nose brown
	constexpr uint8_t D = 9;   // dark grey
	constexpr uint8_t L = 10;  // light grey
	constexpr uint8_t O = 11;  // orange
	constexpr uint8_t T = 12;  // teal/cyan
	constexpr uint8_t M = 13;  // magenta
}

struct FacePattern {
	std::string id;
	std::string name;
	std::array<std::array<uint8_t, 16>, 16> pixels; // [row][col], row 0 = top
};

// Default color palette (index -> RGBA)
inline const glm::vec4& facePaletteColor(uint8_t idx) {
	static const glm::vec4 palette[] = {
		{0, 0, 0, 0},                // 0: transparent
		{0.06f, 0.04f, 0.04f, 1},    // K: black
		{1.00f, 1.00f, 1.00f, 1},    // W: white
		{0.95f, 0.50f, 0.50f, 1},    // P: pink
		{0.85f, 0.12f, 0.10f, 1},    // R: red
		{0.20f, 0.65f, 0.20f, 1},    // G: green
		{0.20f, 0.35f, 0.80f, 1},    // B: blue
		{0.95f, 0.82f, 0.15f, 1},    // Y: yellow
		{0.55f, 0.38f, 0.28f, 1},    // N: nose/brown
		{0.25f, 0.22f, 0.22f, 1},    // D: dark grey
		{0.78f, 0.78f, 0.78f, 1},    // L: light grey
		{0.92f, 0.55f, 0.12f, 1},    // O: orange
		{0.15f, 0.75f, 0.70f, 1},    // T: teal
		{0.80f, 0.20f, 0.65f, 1},    // M: magenta
	};
	return (idx < 14) ? palette[idx] : palette[0];
}

/**
 * Convert a FacePattern into BodyParts positioned on the head's front face.
 *
 * headOffset / headHalfSize describe the head box geometry.
 * headPivot / headSwing describe the head's animation so face pixels
 * inherit the same motion.
 */
inline std::vector<BodyPart> faceToBodyParts(
	const FacePattern& face,
	glm::vec3 headOffset,
	glm::vec3 headHalfSize,
	glm::vec3 headPivot = {0, 0, 0},
	glm::vec3 headSwingAxis = {1, 0, 0},
	float headSwingAmp = 0,
	float headSwingPhase = 0,
	float headSwingSpeed = 0)
{
	std::vector<BodyPart> parts;

	float headW = headHalfSize.x * 2.0f;
	float headH = headHalfSize.y * 2.0f;
	float pixW = headW / 16.0f;
	float pixH = headH / 16.0f;
	float depth = 0.006f; // thin slab depth

	// Head front face is at z = headOffset.z - headHalfSize.z
	float faceZ = headOffset.z - headHalfSize.z - depth * 0.5f - 0.001f;

	for (int row = 0; row < 16; row++) {
		for (int col = 0; col < 16; col++) {
			uint8_t idx = face.pixels[row][col];
			if (idx == 0) continue;

			glm::vec4 color = facePaletteColor(idx);

			float px = headOffset.x - headHalfSize.x + (col + 0.5f) * pixW;
			float py = headOffset.y + headHalfSize.y - (row + 0.5f) * pixH;

			BodyPart part;
			part.offset = {px, py, faceZ};
			part.halfSize = {pixW * 0.48f, pixH * 0.48f, depth};
			part.color = color;
			// Inherit head animation
			part.pivot = headPivot;
			part.swingAxis = headSwingAxis;
			part.swingAmplitude = headSwingAmp;
			part.swingPhase = headSwingPhase;
			part.swingSpeed = headSwingSpeed;

			parts.push_back(part);
		}
	}
	return parts;
}

class FaceLibrary {
public:
	void add(FacePattern face) { m_faces.push_back(std::move(face)); }
	int count() const { return (int)m_faces.size(); }
	const FacePattern& get(int i) const { return m_faces[i]; }
	int selectedIndex() const { return m_selected; }
	void select(int i) { if (i >= 0 && i < count()) m_selected = i; }
	const FacePattern& selected() const { return m_faces[m_selected]; }

private:
	std::vector<FacePattern> m_faces;
	int m_selected = 0;
};

} // namespace aicraft

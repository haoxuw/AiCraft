#pragma once

/**
 * Pixel art face overlay system using textures.
 *
 * Each face is a 16x16 grid of palette indices. Instead of generating
 * hundreds of tiny BodyParts, faces are baked into a head texture:
 * a 96x16 RGBA atlas with 6 tiles (one per cube face).
 *
 *   Tile layout: [Front][Back][Left][Right][Bottom][Top]
 *   Front = face pattern on skin background
 *   Top/Back = hair color
 *   Left/Right = upper hair + lower skin
 *   Bottom = skin
 */

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <array>
#include <cstring>
#include <algorithm>

namespace aicraft {

// Palette indices used in face grids.
namespace FC {
	constexpr uint8_t _ = 0;   // transparent (shows skin)
	constexpr uint8_t K = 1;   // black
	constexpr uint8_t W = 2;   // white
	constexpr uint8_t P = 3;   // pink
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
 * Generate a 96x16 RGBA head texture atlas.
 *
 * 6 tiles of 16x16 pixels each, laid out horizontally:
 *   [0: Front face] [1: Back] [2: Left] [3: Right] [4: Bottom] [5: Top]
 *
 * Front face = face pattern pixels on skin background.
 * Top/Back = hair color fill.
 * Left/Right = upper half hair, lower half skin.
 * Bottom = skin fill.
 */
inline GLuint generateHeadTexture(const FacePattern& face,
                                   glm::vec3 skinColor,
                                   glm::vec3 hairColor) {
	const int TILE = 16;
	const int W = TILE * 6; // 96
	const int H = TILE;     // 16

	// RGBA pixel buffer
	std::vector<uint8_t> pixels(W * H * 4);

	auto setPixel = [&](int x, int y, glm::vec3 c, float a = 1.0f) {
		int idx = (y * W + x) * 4;
		pixels[idx + 0] = (uint8_t)(std::clamp(c.r, 0.0f, 1.0f) * 255);
		pixels[idx + 1] = (uint8_t)(std::clamp(c.g, 0.0f, 1.0f) * 255);
		pixels[idx + 2] = (uint8_t)(std::clamp(c.b, 0.0f, 1.0f) * 255);
		pixels[idx + 3] = (uint8_t)(std::clamp(a, 0.0f, 1.0f) * 255);
	};

	auto fillTile = [&](int tile, glm::vec3 color) {
		int ox = tile * TILE;
		for (int y = 0; y < TILE; y++)
			for (int x = 0; x < TILE; x++)
				setPixel(ox + x, y, color);
	};

	// Fill all tiles with base colors
	fillTile(0, skinColor);    // Front: skin base
	fillTile(1, hairColor);    // Back: hair
	// Left/Right: upper 7 rows hair, lower 9 rows skin
	for (int y = 0; y < TILE; y++) {
		glm::vec3 c = (y < 7) ? hairColor : skinColor;
		for (int x = 0; x < TILE; x++) {
			setPixel(2 * TILE + x, y, c); // Left
			setPixel(3 * TILE + x, y, c); // Right
		}
	}
	fillTile(4, skinColor);    // Bottom: skin
	fillTile(5, hairColor);    // Top: hair

	// Paint face pattern on front tile (tile 0)
	// Face pattern row 0 = top of head, row 15 = chin
	// Texture row 0 = bottom of face in GL (Y-flipped)
	int frontOx = 0;
	for (int row = 0; row < 16; row++) {
		for (int col = 0; col < 16; col++) {
			uint8_t idx = face.pixels[row][col];
			if (idx == 0) continue; // transparent = keep skin
			glm::vec4 pc = facePaletteColor(idx);
			// GL texture: row 0 = bottom, so flip vertically
			setPixel(frontOx + col, (15 - row), glm::vec3(pc), pc.a);
		}
	}

	// Upload
	GLuint tex;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return tex;
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

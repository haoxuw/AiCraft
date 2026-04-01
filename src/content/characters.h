#pragma once

/**
 * Built-in character definitions -- C++ mirrors of the Python files in characters/.
 *
 * IMPORTANT: Python defs use full size; C++ BodyPart uses halfSize = size / 2.
 * Face features (eyes, mouth) are NOT included here -- they come from the face
 * overlay system in faces.h.
 */

#include "shared/character.h"
#include <cmath>

namespace agentworld::builtin {

// ======================================================================
// Blue Knight -- steel-trimmed blue plate armor, cape, pauldrons
//   STR 4  STA 4  AGI 2  INT 2
// ======================================================================
inline CharacterDef blueKnightDef() {
	const float PI = 3.14159265f;
	CharacterDef c;
	c.id = "base:knight";
	c.name = "Knight";
	c.description = "Stalwart defender in steel-trimmed blue plate.";
	c.stats = {4, 4, 2, 2};
	c.jumpVelocity = 11.2f;  // AGI 2 → ~2.25 blocks
	c.skinColor = {0.85f, 0.70f, 0.55f};
	c.hairColor = {0.25f, 0.18f, 0.12f};
	c.headOffset = {0, 1.75f, 0};
	c.headHalfSize = {0.25f, 0.25f, 0.25f};

	auto& m = c.model;
	m.totalHeight = 2.0f;
	m.walkCycleSpeed = 2.0f;
	m.walkBobAmount = 0.05f;
	m.idleBobAmount = 0.012f;


	// Head (texture provides face + hair -- must be parts[0])
	m.parts.push_back({{0,1.75f,0},{0.25f,0.25f,0.25f},{0.85f,0.70f,0.55f,1},
		{0,1.5f,0},{1,0,0},4,0,2});

	// Torso (Y-axis counter-twist)
	m.parts.push_back({{0,1.08f,0},{0.25f,0.32f,0.15f},{0.18f,0.32f,0.72f,1},
		{0,1.08f,0},{0,1,0},4,PI,1});
	// Chestplate + emblem
	m.parts.push_back({{0,1.12f,-0.14f},{0.22f,0.26f,0.02f},{0.55f,0.58f,0.62f,1}});
	m.parts.push_back({{0,1.18f,-0.17f},{0.05f,0.05f,0.01f},{0.80f,0.70f,0.20f,1}});
	// Gorget
	m.parts.push_back({{0,1.39f,0},{0.23f,0.04f,0.15f},{0.55f,0.58f,0.62f,1}});
	// Belt + buckle
	m.parts.push_back({{0,0.76f,0},{0.26f,0.04f,0.16f},{0.35f,0.25f,0.15f,1}});
	m.parts.push_back({{0,0.76f,-0.15f},{0.04f,0.03f,0.02f},{0.80f,0.70f,0.20f,1}});

	// Left: pauldron + arm + gauntlet + hand (amp=35)
	m.parts.push_back({{-0.37f,1.38f,0},{0.14f,0.06f,0.14f},{0.55f,0.58f,0.62f,1},
		{-0.37f,1.40f,0},{1,0,0},35,PI,1});
	m.parts.push_back({{-0.37f,1.08f,0},{0.10f,0.28f,0.10f},{0.12f,0.22f,0.55f,1},
		{-0.37f,1.40f,0},{1,0,0},35,PI,1});
	m.parts.push_back({{-0.37f,0.86f,0},{0.12f,0.09f,0.12f},{0.38f,0.40f,0.45f,1},
		{-0.37f,1.40f,0},{1,0,0},35,PI,1});
	m.parts.push_back({{-0.37f,0.77f,0},{0.08f,0.05f,0.08f},{0.85f,0.70f,0.55f,1},
		{-0.37f,1.40f,0},{1,0,0},35,PI,1});
	// Right: pauldron + arm + gauntlet + hand (amp=35)
	m.parts.push_back({{ 0.37f,1.38f,0},{0.14f,0.06f,0.14f},{0.55f,0.58f,0.62f,1},
		{ 0.37f,1.40f,0},{1,0,0},35,0,1});
	m.parts.push_back({{ 0.37f,1.08f,0},{0.10f,0.28f,0.10f},{0.12f,0.22f,0.55f,1},
		{ 0.37f,1.40f,0},{1,0,0},35,0,1});
	m.parts.push_back({{ 0.37f,0.86f,0},{0.12f,0.09f,0.12f},{0.38f,0.40f,0.45f,1},
		{ 0.37f,1.40f,0},{1,0,0},35,0,1});
	m.parts.push_back({{ 0.37f,0.77f,0},{0.08f,0.05f,0.08f},{0.85f,0.70f,0.55f,1},
		{ 0.37f,1.40f,0},{1,0,0},35,0,1});

	// Cape
	m.parts.push_back({{0,0.95f,0.20f},{0.22f,0.38f,0.03f},{0.14f,0.25f,0.60f,1},
		{0,1.35f,0.18f},{1,0,0},10,0,1.2f});

	// Legs + shin guards + boots (amp=40)
	m.parts.push_back({{-0.13f,0.40f,0},{0.11f,0.24f,0.11f},{0.18f,0.18f,0.28f,1},
		{-0.13f,0.70f,0},{1,0,0},40,0,1});
	m.parts.push_back({{-0.13f,0.38f,-0.12f},{0.08f,0.14f,0.02f},{0.38f,0.40f,0.45f,1},
		{-0.13f,0.70f,0},{1,0,0},40,0,1});
	m.parts.push_back({{-0.13f,0.10f,0},{0.12f,0.10f,0.13f},{0.30f,0.22f,0.14f,1},
		{-0.13f,0.70f,0},{1,0,0},40,0,1});
	m.parts.push_back({{ 0.13f,0.40f,0},{0.11f,0.24f,0.11f},{0.18f,0.18f,0.28f,1},
		{ 0.13f,0.70f,0},{1,0,0},40,PI,1});
	m.parts.push_back({{ 0.13f,0.38f,-0.12f},{0.08f,0.14f,0.02f},{0.38f,0.40f,0.45f,1},
		{ 0.13f,0.70f,0},{1,0,0},40,PI,1});
	m.parts.push_back({{ 0.13f,0.10f,0},{0.12f,0.10f,0.13f},{0.30f,0.22f,0.14f,1},
		{ 0.13f,0.70f,0},{1,0,0},40,PI,1});

	// Equipment slots (sized for Blue Knight proportions)
	c.slotBack = {{0, 1.05f, 0.18f}, {0.16f, 0.22f, 0.08f}};
	c.slotHead = {{0, 2.02f, 0}, {0.26f, 0.08f, 0.26f}};
	c.slotRightHand = {{0.37f, 0.70f, -0.12f}, {0.04f, 0.30f, 0.04f},
		{0.37f,1.40f,0},{1,0,0},35,0,1};  // swings with right arm
	c.slotLeftHand = {{-0.37f, 0.70f, -0.12f}, {0.04f, 0.30f, 0.04f},
		{-0.37f,1.40f,0},{1,0,0},35,PI,1};
	c.slotFeet = {{0, 0.04f, 0}, {0.14f, 0.04f, 0.16f}};

	return c;
}

// ======================================================================
// Skeleton -- undead warrior in rusted iron
//   STR 3  STA 2  AGI 4  INT 3
// ======================================================================
inline CharacterDef skeletonDef() {
	const float PI = 3.14159265f;
	CharacterDef c;
	c.id = "base:skeleton";
	c.name = "Skeleton";
	c.description = "An undead warrior draped in rusted iron.";
	c.stats = {3, 2, 4, 3};
	c.jumpVelocity = 12.4f;  // AGI 4 → ~2.75 blocks
	c.skinColor = {0.88f, 0.85f, 0.78f};
	c.hairColor = {0.70f, 0.65f, 0.58f}; // dark bone (top of skull)
	c.headOffset = {0, 1.78f, 0};
	c.headHalfSize = {0.23f, 0.23f, 0.23f};

	auto& m = c.model;
	m.totalHeight = 2.0f;
	m.walkCycleSpeed = 1.8f;
	m.walkBobAmount = 0.03f;
	m.idleBobAmount = 0.008f;


	// Skull (texture provides face -- must be parts[0])
	m.parts.push_back({{0,1.78f,0},{0.23f,0.23f,0.23f},{0.88f,0.85f,0.78f,1},
		{0,1.55f,0},{1,0,0},6,0,2});
	// Jaw (structural, kept as geometry)
	m.parts.push_back({{0,1.62f,-0.02f},{0.18f,0.05f,0.17f},{0.82f,0.78f,0.70f,1},
		{0,1.55f,0},{1,0,0},6,0,2});

	// Ribcage (Y-axis counter-twist)
	m.parts.push_back({{0,1.10f,0},{0.06f,0.30f,0.06f},{0.70f,0.65f,0.58f,1},
		{0,1.10f,0},{0,1,0},4,PI,1});
	m.parts.push_back({{0,1.18f,0},{0.20f,0.18f,0.11f},{0.88f,0.85f,0.78f,1}});
	m.parts.push_back({{-0.14f,1.08f,-0.06f},{0.08f,0.02f,0.06f},{0.80f,0.76f,0.68f,1}});
	m.parts.push_back({{ 0.14f,1.08f,-0.06f},{0.08f,0.02f,0.06f},{0.80f,0.76f,0.68f,1}});
	m.parts.push_back({{0,0.72f,0},{0.18f,0.05f,0.11f},{0.70f,0.65f,0.58f,1}});
	m.parts.push_back({{0,0.88f,-0.08f},{0.16f,0.14f,0.02f},{0.30f,0.28f,0.25f,0.85f}});
	m.parts.push_back({{0,0.72f,-0.10f},{0.20f,0.03f,0.02f},{0.52f,0.35f,0.22f,1}});

	// Left: rusted pauldron + bone arm + hand
	m.parts.push_back({{-0.32f,1.40f,0},{0.12f,0.06f,0.10f},{0.52f,0.35f,0.22f,1},
		{-0.32f,1.38f,0},{1,0,0},38,PI,1});
	m.parts.push_back({{-0.32f,1.10f,0},{0.05f,0.26f,0.05f},{0.88f,0.85f,0.78f,1},
		{-0.32f,1.38f,0},{1,0,0},38,PI,1});
	m.parts.push_back({{-0.32f,0.80f,0},{0.06f,0.04f,0.04f},{0.70f,0.65f,0.58f,1},
		{-0.32f,1.38f,0},{1,0,0},38,PI,1});
	// Right: bare bone arm
	m.parts.push_back({{ 0.32f,1.10f,0},{0.05f,0.26f,0.05f},{0.88f,0.85f,0.78f,1},
		{ 0.32f,1.38f,0},{1,0,0},38,0,1});
	m.parts.push_back({{ 0.32f,0.80f,0},{0.06f,0.04f,0.04f},{0.70f,0.65f,0.58f,1},
		{ 0.32f,1.38f,0},{1,0,0},38,0,1});

	// Shield fragment
	m.parts.push_back({{ 0.08f,1.05f,0.16f},{0.14f,0.16f,0.02f},{0.42f,0.30f,0.18f,1}});
	m.parts.push_back({{ 0.08f,1.05f,0.13f},{0.04f,0.04f,0.02f},{0.52f,0.35f,0.22f,1}});

	// Legs + feet
	m.parts.push_back({{-0.12f,0.38f,0},{0.06f,0.22f,0.06f},{0.88f,0.85f,0.78f,1},
		{-0.12f,0.66f,0},{1,0,0},42,0,1});
	m.parts.push_back({{-0.12f,0.08f,-0.04f},{0.07f,0.05f,0.11f},{0.70f,0.65f,0.58f,1},
		{-0.12f,0.66f,0},{1,0,0},42,0,1});
	m.parts.push_back({{ 0.12f,0.38f,0},{0.06f,0.22f,0.06f},{0.88f,0.85f,0.78f,1},
		{ 0.12f,0.66f,0},{1,0,0},42,PI,1});
	m.parts.push_back({{ 0.12f,0.08f,-0.04f},{0.07f,0.05f,0.11f},{0.70f,0.65f,0.58f,1},
		{ 0.12f,0.66f,0},{1,0,0},42,PI,1});

	c.slotBack = {{0, 1.05f, 0.14f}, {0.14f, 0.20f, 0.06f}};
	c.slotHead = {{0, 2.02f, 0}, {0.22f, 0.06f, 0.22f}};
	c.slotRightHand = {{0.32f, 0.75f, -0.10f}, {0.03f, 0.28f, 0.03f},
		{0.32f,1.38f,0},{1,0,0},38,0,1};
	c.slotLeftHand = {{-0.32f, 0.75f, -0.10f}, {0.03f, 0.28f, 0.03f},
		{-0.32f,1.38f,0},{1,0,0},38,PI,1};
	c.slotFeet = {{0, 0.03f, 0}, {0.10f, 0.03f, 0.14f}};
	return c;
}

// ======================================================================
// Crewmate -- Among Us astronaut (helmet IS the head)
//   STR 2  STA 3  AGI 3  INT 4
// ======================================================================
inline CharacterDef crewmateDef() {
	const float PI = 3.14159265f;
	CharacterDef c;
	c.id = "base:crewmate";
	c.name = "Crewmate";
	c.description = "The iconic Among Us astronaut. Sus.";
	c.stats = {2, 3, 3, 4};
	c.jumpVelocity = 11.8f;  // AGI 3 → ~2.5 blocks
	c.skinColor = {0.38f, 0.92f, 0.85f};
	c.hairColor = {0.85f, 0.18f, 0.18f}; // suit red (top of helmet)
	c.headOffset = {0, 1.08f, -0.21f};
	c.headHalfSize = {0.22f, 0.21f, 0.02f};

	auto& m = c.model;
	m.totalHeight = 1.65f;
	m.walkCycleSpeed = 1.5f;
	m.walkBobAmount = 0.055f;
	m.idleBobAmount = 0.015f;


	// Visor = head for face texture (must be parts[0])
	m.parts.push_back({{0,1.08f,-0.21f},{0.22f,0.21f,0.02f},{0.38f,0.92f,0.85f,1}});
	// Body egg shape
	m.parts.push_back({{0,0.64f,0},{0.34f,0.34f,0.26f},{0.85f,0.18f,0.18f,1}});
	m.parts.push_back({{0,1.17f,0},{0.27f,0.22f,0.22f},{0.85f,0.18f,0.18f,1}});
	m.parts.push_back({{0,1.50f,0},{0.18f,0.12f,0.16f},{0.85f,0.18f,0.18f,1}});
	m.parts.push_back({{0,0.34f,0},{0.30f,0.05f,0.22f},{0.52f,0.10f,0.10f,1}});
	// Backpack + O2 port + strap
	m.parts.push_back({{0,0.82f,0.32f},{0.17f,0.26f,0.12f},{0.52f,0.10f,0.10f,1}});
	m.parts.push_back({{0,0.92f,0.43f},{0.05f,0.05f,0.02f},{0.34f,0.06f,0.06f,1}});
	m.parts.push_back({{0,1.08f,0.26f},{0.12f,0.03f,0.06f},{0.42f,0.08f,0.08f,1}});
	// Arms
	m.parts.push_back({{-0.46f,0.82f,0},{0.10f,0.15f,0.10f},{0.68f,0.14f,0.14f,1},
		{-0.36f,1.02f,0},{1,0,0},35,PI,1});
	m.parts.push_back({{ 0.46f,0.82f,0},{0.10f,0.15f,0.10f},{0.68f,0.14f,0.14f,1},
		{ 0.36f,1.02f,0},{1,0,0},35,0,1});
	// Legs + soles
	m.parts.push_back({{-0.15f,0.15f,0},{0.14f,0.15f,0.18f},{0.68f,0.14f,0.14f,1},
		{-0.15f,0.32f,0},{1,0,0},22,0,1});
	m.parts.push_back({{ 0.15f,0.15f,0},{0.14f,0.15f,0.18f},{0.68f,0.14f,0.14f,1},
		{ 0.15f,0.32f,0},{1,0,0},22,PI,1});
	m.parts.push_back({{-0.15f,0.02f,0.01f},{0.15f,0.03f,0.18f},{0.42f,0.08f,0.08f,1},
		{-0.15f,0.32f,0},{1,0,0},22,0,1});
	m.parts.push_back({{ 0.15f,0.02f,0.01f},{0.15f,0.03f,0.18f},{0.42f,0.08f,0.08f,1},
		{ 0.15f,0.32f,0},{1,0,0},22,PI,1});

	// Crewmate equip slots (wider body, stubby limbs)
	c.slotBack = {{0, 0.85f, 0.36f}, {0.18f, 0.24f, 0.10f}};
	c.slotHead = {{0, 1.64f, 0}, {0.20f, 0.08f, 0.18f}};
	c.slotRightHand = {{0.50f, 0.75f, -0.08f}, {0.05f, 0.18f, 0.05f},
		{0.36f,1.02f,0},{1,0,0},35,0,1};
	c.slotLeftHand = {{-0.50f, 0.75f, -0.08f}, {0.05f, 0.18f, 0.05f},
		{-0.36f,1.02f,0},{1,0,0},35,PI,1};
	c.slotFeet = {{0, 0.01f, 0}, {0.16f, 0.03f, 0.20f}};
	return c;
}

// ======================================================================
// Giant -- massive iron guardian (renamed from Iron Golem)
//   STR 5  STA 5  AGI 1  INT 1
// ======================================================================
inline CharacterDef giantDef() {
	const float PI = 3.14159265f;
	CharacterDef c;
	c.id = "base:giant";
	c.name = "Giant";
	c.description = "Massive iron guardian.";
	c.stats = {5, 5, 1, 1};
	c.jumpVelocity = 10.6f;  // AGI 1 → ~2.0 blocks
	c.skinColor = {0.42f, 0.40f, 0.38f};
	c.hairColor = {0.32f, 0.30f, 0.28f};
	c.headOffset = {0, 1.62f, 0};
	c.headHalfSize = {0.30f, 0.26f, 0.28f};

	auto& m = c.model;
	m.totalHeight = 2.2f;
	m.walkCycleSpeed = 1.2f;
	m.walkBobAmount = 0.09f;
	m.idleBobAmount = 0.005f;


	// Head (texture provides face -- must be parts[0])
	m.parts.push_back({{0,1.62f,0},{0.30f,0.26f,0.28f},{0.42f,0.40f,0.38f,1},
		{0,1.46f,0},{1,0,0},3,0,1.5f});

	// Legs + feet
	m.parts.push_back({{-0.22f,0.28f,0},{0.18f,0.28f,0.20f},{0.45f,0.42f,0.40f,1},
		{-0.22f,0.58f,0},{1,0,0},50,0,1});
	m.parts.push_back({{ 0.22f,0.28f,0},{0.18f,0.28f,0.20f},{0.45f,0.42f,0.40f,1},
		{ 0.22f,0.58f,0},{1,0,0},50,PI,1});
	m.parts.push_back({{-0.22f,0.05f,0.04f},{0.20f,0.08f,0.24f},{0.32f,0.30f,0.28f,1},
		{-0.22f,0.58f,0},{1,0,0},50,0,1});
	m.parts.push_back({{ 0.22f,0.05f,0.04f},{0.20f,0.08f,0.24f},{0.32f,0.30f,0.28f,1},
		{ 0.22f,0.58f,0},{1,0,0},50,PI,1});
	// Torso
	m.parts.push_back({{0,0.76f,0},{0.40f,0.26f,0.30f},{0.45f,0.42f,0.40f,1}});
	m.parts.push_back({{0,1.18f,0},{0.42f,0.26f,0.28f},{0.48f,0.45f,0.43f,1}});
	// Chest crack (lava glow)
	m.parts.push_back({{0,1.10f,-0.27f},{0.12f,0.18f,0.01f},{1.00f,0.58f,0.08f,1}});
	m.parts.push_back({{0,1.10f,-0.28f},{0.06f,0.10f,0.01f},{1.00f,0.85f,0.30f,1}});
	// Rivets
	m.parts.push_back({{-0.24f,0.92f,-0.29f},{0.04f,0.04f,0.01f},{0.22f,0.20f,0.18f,1}});
	m.parts.push_back({{ 0.24f,0.92f,-0.29f},{0.04f,0.04f,0.01f},{0.22f,0.20f,0.18f,1}});
	m.parts.push_back({{0,0.92f,-0.29f},{0.03f,0.03f,0.01f},{0.22f,0.20f,0.18f,1}});
	// Shoulder bolts
	m.parts.push_back({{-0.48f,1.34f,0},{0.05f,0.05f,0.05f},{0.22f,0.20f,0.18f,1}});
	m.parts.push_back({{ 0.48f,1.34f,0},{0.05f,0.05f,0.05f},{0.22f,0.20f,0.18f,1}});
	// Arms (massive) + fists
	m.parts.push_back({{-0.64f,1.00f,0},{0.20f,0.42f,0.20f},{0.45f,0.42f,0.40f,1},
		{-0.48f,1.36f,0},{1,0,0},48,PI,1});
	m.parts.push_back({{-0.64f,0.52f,0},{0.24f,0.20f,0.24f},{0.32f,0.30f,0.28f,1},
		{-0.48f,1.36f,0},{1,0,0},48,PI,1});
	m.parts.push_back({{ 0.64f,1.00f,0},{0.20f,0.42f,0.20f},{0.45f,0.42f,0.40f,1},
		{ 0.48f,1.36f,0},{1,0,0},48,0,1});
	m.parts.push_back({{ 0.64f,0.52f,0},{0.24f,0.20f,0.24f},{0.32f,0.30f,0.28f,1},
		{ 0.48f,1.36f,0},{1,0,0},48,0,1});

	// Giant equip slots (massive proportions)
	c.slotBack = {{0, 1.10f, 0.32f}, {0.22f, 0.30f, 0.10f}};
	c.slotHead = {{0, 1.90f, 0}, {0.30f, 0.10f, 0.28f}};
	c.slotRightHand = {{0.68f, 0.45f, -0.15f}, {0.06f, 0.40f, 0.06f},
		{0.48f,1.36f,0},{1,0,0},48,0,1};
	c.slotLeftHand = {{-0.68f, 0.45f, -0.15f}, {0.06f, 0.40f, 0.06f},
		{-0.48f,1.36f,0},{1,0,0},48,PI,1};
	c.slotFeet = {{0, 0.02f, 0}, {0.22f, 0.05f, 0.26f}};
	return c;
}

// ======================================================================
// Purple Mage -- arcane robes, tall hat, staff with gem
//   STR 1  STA 2  AGI 3  INT 5
// ======================================================================
inline CharacterDef purpleMageDef() {
	const float PI = 3.14159265f;
	CharacterDef c;
	c.id = "base:mage";
	c.name = "Mage";
	c.description = "Wielder of arcane arts, draped in star-dusted robes.";
	c.stats = {1, 2, 3, 5};
	c.jumpVelocity = 11.8f;  // AGI 3 → ~2.5 blocks
	c.skinColor = {0.92f, 0.82f, 0.70f};
	c.hairColor = {0.22f, 0.05f, 0.34f}; // hat purple
	c.headOffset = {0, 1.65f, 0};
	c.headHalfSize = {0.22f, 0.22f, 0.22f};

	auto& m = c.model;
	m.totalHeight = 2.4f;
	m.walkCycleSpeed = 1.7f;
	m.walkBobAmount = 0.040f;
	m.idleBobAmount = 0.010f;


	// Head (texture provides face -- must be parts[0])
	m.parts.push_back({{0,1.65f,0},{0.22f,0.22f,0.22f},{0.92f,0.82f,0.70f,1},
		{0,1.44f,0},{1,0,0},5,0,2});

	// Robe body
	m.parts.push_back({{0,0.40f,0},{0.34f,0.10f,0.24f},{0.36f,0.06f,0.54f,1}});
	m.parts.push_back({{0,0.64f,0},{0.30f,0.24f,0.22f},{0.45f,0.10f,0.65f,1}});
	m.parts.push_back({{0,1.08f,0},{0.22f,0.22f,0.16f},{0.45f,0.10f,0.65f,1}});
	m.parts.push_back({{0,1.08f,-0.15f},{0.14f,0.16f,0.02f},{0.60f,0.20f,0.84f,1}});
	// Belt + clasp
	m.parts.push_back({{0,0.87f,-0.21f},{0.18f,0.03f,0.02f},{0.80f,0.68f,0.12f,1}});
	m.parts.push_back({{0,0.87f,-0.23f},{0.04f,0.04f,0.01f},{0.95f,0.88f,0.20f,1}});

	// Tall conical hat (the "hair" -- iconic silhouette)
	m.parts.push_back({{0,1.89f,0},{0.28f,0.04f,0.28f},{0.22f,0.05f,0.34f,1},   // brim
		{0,1.44f,0},{1,0,0},5,0,2});
	m.parts.push_back({{0,1.94f,0},{0.21f,0.025f,0.21f},{0.80f,0.68f,0.12f,1},  // gold band
		{0,1.44f,0},{1,0,0},5,0,2});
	m.parts.push_back({{0,2.02f,0},{0.18f,0.08f,0.18f},{0.22f,0.05f,0.34f,1},
		{0,1.44f,0},{1,0,0},5,0,2});
	m.parts.push_back({{0,2.16f,0},{0.12f,0.10f,0.12f},{0.22f,0.05f,0.34f,1},
		{0,1.44f,0},{1,0,0},5,0,2});
	m.parts.push_back({{0,2.32f,0},{0.07f,0.14f,0.07f},{0.22f,0.05f,0.34f,1},   // tip
		{0,1.44f,0},{1,0,0},5,0,2});
	m.parts.push_back({{0.07f,2.10f,-0.12f},{0.03f,0.03f,0.01f},{0.95f,0.88f,0.20f,1}, // star
		{0,1.44f,0},{1,0,0},5,0,2});

	// Arms + bell sleeves + hands
	m.parts.push_back({{-0.32f,1.08f,0},{0.10f,0.30f,0.10f},{0.45f,0.10f,0.65f,1},
		{-0.32f,1.40f,0},{1,0,0},36,PI,1});
	m.parts.push_back({{-0.32f,0.82f,0},{0.14f,0.06f,0.12f},{0.36f,0.06f,0.54f,1},
		{-0.32f,1.40f,0},{1,0,0},36,PI,1});
	m.parts.push_back({{-0.32f,0.74f,0},{0.08f,0.05f,0.07f},{0.92f,0.82f,0.70f,1},
		{-0.32f,1.40f,0},{1,0,0},36,PI,1});
	m.parts.push_back({{ 0.32f,1.08f,0},{0.10f,0.30f,0.10f},{0.45f,0.10f,0.65f,1},
		{ 0.32f,1.40f,0},{1,0,0},36,0,1});
	m.parts.push_back({{ 0.32f,0.82f,0},{0.14f,0.06f,0.12f},{0.36f,0.06f,0.54f,1},
		{ 0.32f,1.40f,0},{1,0,0},36,0,1});
	m.parts.push_back({{ 0.32f,0.74f,0},{0.08f,0.05f,0.07f},{0.92f,0.82f,0.70f,1},
		{ 0.32f,1.40f,0},{1,0,0},36,0,1});

	// Staff + gem
	m.parts.push_back({{ 0.44f,0.82f,-0.08f},{0.03f,0.48f,0.03f},{0.40f,0.28f,0.12f,1},
		{ 0.32f,1.40f,0},{1,0,0},36,0,1});
	m.parts.push_back({{ 0.44f,1.34f,-0.08f},{0.09f,0.09f,0.09f},{0.38f,0.70f,1.00f,1},
		{ 0.32f,1.40f,0},{1,0,0},36,0,1});
	m.parts.push_back({{ 0.44f,1.34f,-0.08f},{0.05f,0.05f,0.05f},{0.76f,0.92f,1.00f,1},
		{ 0.32f,1.40f,0},{1,0,0},36,0,1});

	// Legs (peek below robe)
	m.parts.push_back({{-0.10f,0.22f,0},{0.09f,0.22f,0.10f},{0.32f,0.06f,0.48f,1},
		{-0.10f,0.48f,0},{1,0,0},30,0,1});
	m.parts.push_back({{ 0.10f,0.22f,0},{0.09f,0.22f,0.10f},{0.32f,0.06f,0.48f,1},
		{ 0.10f,0.48f,0},{1,0,0},30,PI,1});

	// Mage equip slots (robed, tall hat area occupied)
	c.slotBack = {{0, 1.00f, 0.18f}, {0.14f, 0.20f, 0.06f}};
	c.slotHead = {{0, 2.50f, 0}, {0.10f, 0.06f, 0.10f}}; // above hat tip
	c.slotRightHand = {{0.36f, 0.68f, -0.10f}, {0.03f, 0.26f, 0.03f},
		{0.32f,1.40f,0},{1,0,0},36,0,1};
	c.slotLeftHand = {{-0.36f, 0.68f, -0.10f}, {0.03f, 0.26f, 0.03f},
		{-0.32f,1.40f,0},{1,0,0},36,PI,1};
	c.slotFeet = {{0, 0.02f, 0}, {0.12f, 0.04f, 0.14f}};
	return c;
}

// ======================================================================
inline void registerAllCharacters(CharacterManager& mgr) {
	mgr.add(blueKnightDef());
	mgr.add(skeletonDef());
	mgr.add(crewmateDef());
	mgr.add(giantDef());
	mgr.add(purpleMageDef());

	// ---- Item visuals: multi-piece models for equipped items ----

	// Jetpack: twin fuel tanks + frame + nozzles + flame effects
	{
		ItemVisual jet;
		jet.itemId = ItemId::Jetpack;
		jet.slotName = "back";
		jet.pieces = {
			{{0.38f,0.38f,0.42f,1},{-0.08f,0,0.02f},{0.06f,0.18f,0.06f}},       // tank L
			{{0.38f,0.38f,0.42f,1},{ 0.08f,0,0.02f},{0.06f,0.18f,0.06f}},       // tank R
			{{0.50f,0.52f,0.55f,1},{-0.08f,0.18f,0.02f},{0.05f,0.03f,0.05f}},   // cap L
			{{0.50f,0.52f,0.55f,1},{ 0.08f,0.18f,0.02f},{0.05f,0.03f,0.05f}},   // cap R
			{{0.30f,0.30f,0.32f,1},{0,0.08f,0},{0.14f,0.02f,0.04f}},             // crossbar
			{{0.30f,0.30f,0.32f,1},{0,-0.02f,-0.02f},{0.02f,0.16f,0.02f}},       // spine
			{{0.25f,0.22f,0.22f,1},{-0.08f,-0.20f,0.02f},{0.05f,0.04f,0.05f}},   // nozzle L
			{{0.25f,0.22f,0.22f,1},{ 0.08f,-0.20f,0.02f},{0.05f,0.04f,0.05f}},   // nozzle R
			{{0.60f,0.30f,0.08f,1},{-0.08f,-0.22f,0.02f},{0.03f,0.02f,0.03f}},   // glow L
			{{0.60f,0.30f,0.08f,1},{ 0.08f,-0.22f,0.02f},{0.03f,0.02f,0.03f}},   // glow R
			{{0.28f,0.25f,0.22f,1},{-0.06f,0.14f,-0.06f},{0.02f,0.10f,0.02f}},   // strap L
			{{0.28f,0.25f,0.22f,1},{ 0.06f,0.14f,-0.06f},{0.02f,0.10f,0.02f}},   // strap R
		};
		// Flame effects: triggered by "jetpack_active" entity prop
		ActiveEffectDef flames;
		flames.trigger = "jetpack_active";
		ParticleEmitterDef leftFlame;
		leftFlame.offset = {-0.08f, -0.22f, 0.02f};
		leftFlame.rate = 4;
		leftFlame.velocity = {0, -5.0f, 0};
		leftFlame.velocitySpread = 0.6f;
		leftFlame.colors = {{1,0.95f,0.8f,1},{1,0.75f,0.15f,1},{1,0.35f,0.05f,0.9f}};
		leftFlame.lifeMin = 0.08f; leftFlame.lifeMax = 0.25f;
		leftFlame.sizeMin = 0.03f; leftFlame.sizeMax = 0.06f;
		ParticleEmitterDef rightFlame = leftFlame;
		rightFlame.offset = {0.08f, -0.22f, 0.02f};
		flames.emitters = {leftFlame, rightFlame};
		jet.effects = {flames};
		mgr.addItemVisual(std::move(jet));
	}

	// Wood Pickaxe: handle + head
	mgr.addItemVisual({ItemId::WoodPickaxe, "right_hand", {
		{{0.50f, 0.35f, 0.15f, 1}, {0, 0, 0}, {0.02f, 0.28f, 0.02f}},        // handle
		{{0.55f, 0.55f, 0.58f, 1}, {0, 0.22f, 0}, {0.10f, 0.03f, 0.02f}},     // head
	}});

	// Stone Pickaxe
	mgr.addItemVisual({ItemId::StonePickaxe, "right_hand", {
		{{0.50f, 0.35f, 0.15f, 1}, {0, 0, 0}, {0.02f, 0.28f, 0.02f}},
		{{0.50f, 0.50f, 0.52f, 1}, {0, 0.22f, 0}, {0.12f, 0.04f, 0.03f}},
	}});

	// Wood Axe: handle + blade
	mgr.addItemVisual({ItemId::WoodAxe, "right_hand", {
		{{0.50f, 0.35f, 0.15f, 1}, {0, 0, 0}, {0.02f, 0.28f, 0.02f}},
		{{0.55f, 0.55f, 0.58f, 1}, {0.04f, 0.20f, 0}, {0.06f, 0.08f, 0.02f}},
	}});

	// Wood Shovel: handle + flat head
	mgr.addItemVisual({ItemId::WoodShovel, "right_hand", {
		{{0.50f, 0.35f, 0.15f, 1}, {0, 0, 0}, {0.02f, 0.30f, 0.02f}},
		{{0.55f, 0.45f, 0.30f, 1}, {0, 0.26f, 0}, {0.05f, 0.06f, 0.02f}},
	}});

	// Parachute pack
	mgr.addItemVisual({ItemId::Parachute, "back", {
		{{0.85f, 0.25f, 0.20f, 1}, {0, 0.06f, 0}, {0.14f, 0.10f, 0.06f}},
		{{0.75f, 0.20f, 0.15f, 1}, {0, -0.04f, 0}, {0.12f, 0.04f, 0.05f}},
	}});
}

} // namespace agentworld::builtin

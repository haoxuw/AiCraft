#pragma once

/**
 * Built-in character definitions -- C++ mirrors of the Python files in characters/.
 *
 * IMPORTANT: Python defs use full size; C++ BodyPart uses halfSize = size / 2.
 * Face features (eyes, mouth) are NOT included here -- they come from the face
 * overlay system in faces.h.
 */

#include "common/character.h"
#include <cmath>

namespace aicraft::builtin {

// ======================================================================
// Blue Knight -- steel-trimmed blue plate armor, cape, pauldrons
//   STR 4  STA 4  AGI 2  INT 2
// ======================================================================
inline CharacterDef blueKnightDef() {
	const float PI = 3.14159265f;
	CharacterDef c;
	c.id = "base:blue_knight";
	c.name = "Blue Knight";
	c.description = "Stalwart defender in steel-trimmed blue plate.";
	c.stats = {4, 4, 2, 2};
	c.headOffset = {0, 1.75f, 0};
	c.headHalfSize = {0.25f, 0.25f, 0.25f};
	c.headPivot = {0, 1.5f, 0};
	c.headSwingAmp = 5.0f;
	c.headSwingSpeed = 2.0f;

	auto& m = c.model;
	m.totalHeight = 2.0f;
	m.walkCycleSpeed = 3.5f;   // stride=1.8m, ~4.5 strides/sec at speed 8
	m.walkBobAmount = 0.05f;
	m.idleBobAmount = 0.012f;
	m.lateralSwayAmount = 0.03f;

	// Head
	m.parts.push_back({{0,1.75f,0},{0.25f,0.25f,0.25f},{0.85f,0.70f,0.55f,1},
		{0,1.5f,0},{1,0,0},4,0,2});
	// Short cropped hair (flat top military cut)
	m.parts.push_back({{0,1.98f,0.01f},{0.24f,0.04f,0.24f},{0.25f,0.18f,0.12f,1}});
	// Side burns
	m.parts.push_back({{-0.24f,1.82f,0},{0.03f,0.10f,0.20f},{0.22f,0.15f,0.10f,1}});
	m.parts.push_back({{ 0.24f,1.82f,0},{0.03f,0.10f,0.20f},{0.22f,0.15f,0.10f,1}});

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
	c.headOffset = {0, 1.78f, 0};
	c.headHalfSize = {0.23f, 0.23f, 0.23f};
	c.headPivot = {0, 1.55f, 0};
	c.headSwingAmp = 6.0f;
	c.headSwingSpeed = 2.0f;

	auto& m = c.model;
	m.totalHeight = 2.0f;
	m.walkCycleSpeed = 2.7f;
	m.walkBobAmount = 0.03f;
	m.idleBobAmount = 0.008f;
	m.lateralSwayAmount = 0.02f;

	// Skull
	m.parts.push_back({{0,1.78f,0},{0.23f,0.23f,0.23f},{0.88f,0.85f,0.78f,1},
		{0,1.55f,0},{1,0,0},6,0,2});
	// Jaw
	m.parts.push_back({{0,1.62f,-0.02f},{0.18f,0.05f,0.17f},{0.82f,0.78f,0.70f,1},
		{0,1.55f,0},{1,0,0},6,0,2});
	// Bone crown spikes (jagged headpiece)
	m.parts.push_back({{0,2.02f,0},{0.05f,0.06f,0.05f},{0.82f,0.78f,0.70f,1},
		{0,1.55f,0},{1,0,0},6,0,2});
	m.parts.push_back({{-0.14f,1.99f,0},{0.04f,0.04f,0.04f},{0.82f,0.78f,0.70f,1},
		{0,1.55f,0},{1,0,0},6,0,2});
	m.parts.push_back({{ 0.14f,1.99f,0},{0.04f,0.04f,0.04f},{0.82f,0.78f,0.70f,1},
		{0,1.55f,0},{1,0,0},6,0,2});
	// Crown rust accent
	m.parts.push_back({{0,2.00f,-0.04f},{0.03f,0.03f,0.03f},{0.52f,0.35f,0.22f,1},
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
	c.headOffset = {0, 1.08f, -0.21f};
	c.headHalfSize = {0.22f, 0.21f, 0.02f};
	c.headPivot = {0, 0, 0};
	c.headSwingAmp = 0;
	c.headSwingSpeed = 0;

	auto& m = c.model;
	m.totalHeight = 1.65f;
	m.walkCycleSpeed = 2.2f;
	m.walkBobAmount = 0.055f;
	m.idleBobAmount = 0.015f;
	m.lateralSwayAmount = 0.12f;

	// Body egg shape
	m.parts.push_back({{0,0.64f,0},{0.34f,0.34f,0.26f},{0.85f,0.18f,0.18f,1}});
	m.parts.push_back({{0,1.17f,0},{0.27f,0.22f,0.22f},{0.85f,0.18f,0.18f,1}});
	m.parts.push_back({{0,1.50f,0},{0.18f,0.12f,0.16f},{0.85f,0.18f,0.18f,1}});
	m.parts.push_back({{0,0.34f,0},{0.30f,0.05f,0.22f},{0.52f,0.10f,0.10f,1}});
	// Visor + highlight
	m.parts.push_back({{0,1.08f,-0.21f},{0.22f,0.21f,0.02f},{0.38f,0.92f,0.85f,1}});
	m.parts.push_back({{-0.10f,1.24f,-0.22f},{0.07f,0.06f,0.01f},{0.80f,0.98f,0.96f,1}});
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
	c.description = "A massive iron guardian. Each step shakes the ground.";
	c.stats = {5, 5, 1, 1};
	c.headOffset = {0, 1.62f, 0};
	c.headHalfSize = {0.30f, 0.26f, 0.28f};
	c.headPivot = {0, 1.46f, 0};
	c.headSwingAmp = 3.0f;
	c.headSwingSpeed = 1.5f;

	auto& m = c.model;
	m.totalHeight = 2.2f;
	m.walkCycleSpeed = 1.8f;
	m.walkBobAmount = 0.09f;
	m.idleBobAmount = 0.005f;
	m.lateralSwayAmount = 0.10f;

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
	// Head (no neck, sits on shoulders)
	m.parts.push_back({{0,1.62f,0},{0.30f,0.26f,0.28f},{0.42f,0.40f,0.38f,1},
		{0,1.46f,0},{1,0,0},3,0,1.5f});
	// Flat iron plate "hair" on top (like a helmet ridge)
	m.parts.push_back({{0,1.87f,0},{0.28f,0.03f,0.26f},{0.32f,0.30f,0.28f,1},
		{0,1.46f,0},{1,0,0},3,0,1.5f});
	// Nose/jaw
	m.parts.push_back({{0,1.58f,-0.28f},{0.08f,0.06f,0.01f},{0.32f,0.30f,0.28f,1},
		{0,1.46f,0},{1,0,0},3,0,1.5f});
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

	return c;
}

// ======================================================================
// Purple Mage -- arcane robes, tall hat, staff with gem
//   STR 1  STA 2  AGI 3  INT 5
// ======================================================================
inline CharacterDef purpleMageDef() {
	const float PI = 3.14159265f;
	CharacterDef c;
	c.id = "base:purple_mage";
	c.name = "Purple Mage";
	c.description = "Wielder of arcane arts, draped in star-dusted robes.";
	c.stats = {1, 2, 3, 5};
	c.headOffset = {0, 1.65f, 0};
	c.headHalfSize = {0.22f, 0.22f, 0.22f};
	c.headPivot = {0, 1.44f, 0};
	c.headSwingAmp = 5.0f;
	c.headSwingSpeed = 2.0f;

	auto& m = c.model;
	m.totalHeight = 2.4f;
	m.walkCycleSpeed = 2.6f;
	m.walkBobAmount = 0.040f;
	m.idleBobAmount = 0.010f;
	m.lateralSwayAmount = 0.020f;

	// Robe body
	m.parts.push_back({{0,0.40f,0},{0.34f,0.10f,0.24f},{0.36f,0.06f,0.54f,1}});
	m.parts.push_back({{0,0.64f,0},{0.30f,0.24f,0.22f},{0.45f,0.10f,0.65f,1}});
	m.parts.push_back({{0,1.08f,0},{0.22f,0.22f,0.16f},{0.45f,0.10f,0.65f,1}});
	m.parts.push_back({{0,1.08f,-0.15f},{0.14f,0.16f,0.02f},{0.60f,0.20f,0.84f,1}});
	// Belt + clasp
	m.parts.push_back({{0,0.87f,-0.21f},{0.18f,0.03f,0.02f},{0.80f,0.68f,0.12f,1}});
	m.parts.push_back({{0,0.87f,-0.23f},{0.04f,0.04f,0.01f},{0.95f,0.88f,0.20f,1}});

	// Head
	m.parts.push_back({{0,1.65f,0},{0.22f,0.22f,0.22f},{0.92f,0.82f,0.70f,1},
		{0,1.44f,0},{1,0,0},5,0,2});

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

	return c;
}

// ======================================================================
inline void registerAllCharacters(CharacterManager& mgr) {
	mgr.add(blueKnightDef());
	mgr.add(skeletonDef());
	mgr.add(crewmateDef());
	mgr.add(giantDef());
	mgr.add(purpleMageDef());
}

} // namespace aicraft::builtin

#pragma once

#include "client/model.h"
#include <cmath>

namespace aicraft::builtin {

// ======================================================================
// Player: 2 blocks tall, Minecraft-style proportions
// Arms and legs swing opposite when walking.
// ======================================================================
inline BoxModel playerModel() {
	const float PI = 3.14159265f;
	BoxModel m;
	m.totalHeight   = 2.0f;
	m.modelScale    = 1.25f;      // 25% bigger than base (1.25x visual size)
	m.walkCycleSpeed = 2.5f;      // longer strides: fewer cycles per meter
	m.idleBobAmount  = 0.012f;
	m.walkBobAmount  = 0.06f;
	m.lateralSwayAmount = 0.03f;

	// Head -- nods once per step (2x walk freq)
	m.parts.push_back({
		{0, 1.75f, 0}, {0.25f, 0.25f, 0.25f},
		{0.85f, 0.70f, 0.55f, 1},
		{0, 1.5f, 0}, {1,0,0}, 5.0f, 0, 2.0f
	});

	// Torso -- Y-axis counter-twist
	m.parts.push_back({
		{0, 1.05f, 0}, {0.25f, 0.35f, 0.15f},
		{0.20f, 0.45f, 0.75f, 1},
		{0, 1.05f, 0}, {0,1,0}, 5.0f, PI, 1.0f
	});

	// Left arm -- large confident swing
	m.parts.push_back({
		{-0.37f, 1.05f, 0}, {0.10f, 0.35f, 0.10f},
		{0.80f, 0.65f, 0.50f, 1},
		{-0.37f, 1.40f, 0}, {1,0,0}, 55.0f, PI, 1.0f
	});

	// Right arm
	m.parts.push_back({
		{0.37f, 1.05f, 0}, {0.10f, 0.35f, 0.10f},
		{0.80f, 0.65f, 0.50f, 1},
		{0.37f, 1.40f, 0}, {1,0,0}, 55.0f, 0, 1.0f
	});

	// Left leg -- big stride
	m.parts.push_back({
		{-0.12f, 0.35f, 0}, {0.12f, 0.35f, 0.12f},
		{0.22f, 0.22f, 0.32f, 1},
		{-0.12f, 0.70f, 0}, {1,0,0}, 60.0f, 0, 1.0f
	});

	// Right leg
	m.parts.push_back({
		{0.12f, 0.35f, 0}, {0.12f, 0.35f, 0.12f},
		{0.22f, 0.22f, 0.32f, 1},
		{0.12f, 0.70f, 0}, {1,0,0}, 60.0f, PI, 1.0f
	});

	return m;
}

// ======================================================================
// Pig: ~1 block tall, stubby legs, fat body
// All 4 legs animate. Front pair opposite to back pair.
// ======================================================================
inline BoxModel pigModel() {
	const float PI = 3.14159265f;
	BoxModel m;
	m.totalHeight = 0.9f;
	m.modelScale  = 1.25f;
	m.walkCycleSpeed = 7.0f;
	m.idleBobAmount = 0.008f;
	m.walkBobAmount = 0.02f;
	m.lateralSwayAmount = 0.02f;

	// Body (fat)
	m.parts.push_back({
		{0, 0.45f, 0}, {0.30f, 0.25f, 0.40f},
		{0.90f, 0.72f, 0.68f, 1},
		{}, {}, 0, 0, 0
	});

	// Head
	m.parts.push_back({
		{0, 0.55f, -0.45f}, {0.22f, 0.20f, 0.20f},
		{0.92f, 0.75f, 0.70f, 1},
		{0, 0.55f, -0.25f}, {1,0,0}, 8.0f, 0, 0.5f  // slight head bob
	});

	// Snout
	m.parts.push_back({
		{0, 0.48f, -0.64f}, {0.12f, 0.08f, 0.05f},
		{0.95f, 0.65f, 0.60f, 1},
		{0, 0.55f, -0.25f}, {1,0,0}, 8.0f, 0, 0.5f  // moves with head
	});

	// Ears (left)
	m.parts.push_back({
		{-0.18f, 0.72f, -0.42f}, {0.06f, 0.04f, 0.08f},
		{0.88f, 0.65f, 0.60f, 1},
		{}, {}, 0, 0, 0
	});

	// Ears (right)
	m.parts.push_back({
		{0.18f, 0.72f, -0.42f}, {0.06f, 0.04f, 0.08f},
		{0.88f, 0.65f, 0.60f, 1},
		{}, {}, 0, 0, 0
	});

	// Front-left leg
	m.parts.push_back({
		{-0.18f, 0.15f, -0.25f}, {0.07f, 0.15f, 0.07f},
		{0.88f, 0.68f, 0.63f, 1},
		{-0.18f, 0.30f, -0.25f}, {1,0,0}, 30.0f, 0, 1.0f
	});

	// Front-right leg
	m.parts.push_back({
		{0.18f, 0.15f, -0.25f}, {0.07f, 0.15f, 0.07f},
		{0.88f, 0.68f, 0.63f, 1},
		{0.18f, 0.30f, -0.25f}, {1,0,0}, 30.0f, PI, 1.0f
	});

	// Back-left leg
	m.parts.push_back({
		{-0.18f, 0.15f, 0.25f}, {0.07f, 0.15f, 0.07f},
		{0.88f, 0.68f, 0.63f, 1},
		{-0.18f, 0.30f, 0.25f}, {1,0,0}, 30.0f, PI, 1.0f
	});

	// Back-right leg
	m.parts.push_back({
		{0.18f, 0.15f, 0.25f}, {0.07f, 0.15f, 0.07f},
		{0.88f, 0.68f, 0.63f, 1},
		{0.18f, 0.30f, 0.25f}, {1,0,0}, 30.0f, 0, 1.0f
	});

	// Curly tail
	m.parts.push_back({
		{0, 0.55f, 0.42f}, {0.04f, 0.04f, 0.06f},
		{0.92f, 0.70f, 0.65f, 1},
		{0, 0.50f, 0.40f}, {0,1,0}, 15.0f, 0, 2.0f  // tail wags on Y axis, fast
	});

	return m;
}

// ======================================================================
// Chicken: ~0.7 blocks tall, thin legs, round body
// Legs move fast, wings flap slightly, head bobs.
// ======================================================================
inline BoxModel chickenModel() {
	const float PI = 3.14159265f;
	BoxModel m;
	m.totalHeight = 0.7f;
	m.modelScale  = 1.25f;
	m.walkCycleSpeed = 9.0f;
	m.idleBobAmount = 0.005f;
	m.walkBobAmount = 0.015f;
	m.lateralSwayAmount = 0.01f;

	// Body (round-ish)
	m.parts.push_back({
		{0, 0.32f, 0}, {0.16f, 0.14f, 0.22f},
		{0.95f, 0.95f, 0.90f, 1},
		{}, {}, 0, 0, 0
	});

	// Head (higher, in front)
	m.parts.push_back({
		{0, 0.55f, -0.24f}, {0.10f, 0.10f, 0.10f},
		{0.95f, 0.95f, 0.92f, 1},
		{0, 0.45f, -0.10f}, {1,0,0}, 15.0f, 0, 1.5f  // pecking motion
	});

	// Beak (orange, tiny)
	m.parts.push_back({
		{0, 0.52f, -0.35f}, {0.04f, 0.03f, 0.05f},
		{0.95f, 0.70f, 0.20f, 1},
		{0, 0.45f, -0.10f}, {1,0,0}, 15.0f, 0, 1.5f  // moves with head
	});

	// Comb (red, on top of head)
	m.parts.push_back({
		{0, 0.66f, -0.22f}, {0.03f, 0.05f, 0.06f},
		{0.90f, 0.15f, 0.10f, 1},
		{0, 0.45f, -0.10f}, {1,0,0}, 15.0f, 0, 1.5f
	});

	// Wattle (red, under beak)
	m.parts.push_back({
		{0, 0.44f, -0.32f}, {0.03f, 0.04f, 0.02f},
		{0.90f, 0.20f, 0.15f, 1},
		{0, 0.45f, -0.10f}, {1,0,0}, 15.0f, 0, 1.5f
	});

	// Left wing
	m.parts.push_back({
		{-0.18f, 0.33f, 0.02f}, {0.04f, 0.10f, 0.16f},
		{0.92f, 0.92f, 0.87f, 1},
		{-0.14f, 0.42f, 0}, {0,0,1}, 12.0f, 0, 1.0f  // flap on Z axis
	});

	// Right wing
	m.parts.push_back({
		{0.18f, 0.33f, 0.02f}, {0.04f, 0.10f, 0.16f},
		{0.92f, 0.92f, 0.87f, 1},
		{0.14f, 0.42f, 0}, {0,0,1}, 12.0f, PI, 1.0f   // opposite flap
	});

	// Left leg (thin, orange)
	m.parts.push_back({
		{-0.07f, 0.08f, 0}, {0.03f, 0.10f, 0.03f},
		{0.90f, 0.70f, 0.20f, 1},
		{-0.07f, 0.18f, 0}, {1,0,0}, 35.0f, 0, 1.0f
	});

	// Right leg
	m.parts.push_back({
		{0.07f, 0.08f, 0}, {0.03f, 0.10f, 0.03f},
		{0.90f, 0.70f, 0.20f, 1},
		{0.07f, 0.18f, 0}, {1,0,0}, 35.0f, PI, 1.0f
	});

	// Tail feathers (fanned up)
	m.parts.push_back({
		{0, 0.42f, 0.26f}, {0.08f, 0.12f, 0.04f},
		{0.88f, 0.88f, 0.82f, 1},
		{}, {}, 0, 0, 0
	});

	return m;
}

} // namespace aicraft::builtin

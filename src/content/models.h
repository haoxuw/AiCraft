#pragma once

#include "shared/box_model.h"
#include <cmath>

namespace agentworld::builtin {

// ======================================================================
// Lightbulb: tiny icon floating above living entities
// Yellow = normal, red tint = behavior error
// ======================================================================
inline BoxModel lightbulbModel() {
	BoxModel m;
	m.totalHeight = 0.4f;
	m.modelScale = 1.0f;
	// Bulb body (yellow glass)
	m.parts.push_back({{0, 0.15f, 0}, {0.08f, 0.10f, 0.08f}, {1.0f, 0.92f, 0.3f, 0.9f}});
	// Bulb tip (bright)
	m.parts.push_back({{0, 0.27f, 0}, {0.05f, 0.04f, 0.05f}, {1.0f, 1.0f, 0.7f, 0.95f}});
	// Base (gray metal)
	m.parts.push_back({{0, 0.04f, 0}, {0.06f, 0.05f, 0.06f}, {0.5f, 0.5f, 0.5f, 0.9f}});
	return m;
}


// ======================================================================
// Player: 2 blocks tall, Minecraft-style proportions
// Arms and legs swing opposite when walking.
// ======================================================================
inline BoxModel playerModel() {
	const float PI = 3.14159265f;
	BoxModel m;
	m.totalHeight   = 2.0f;
	m.modelScale    = 1.25f;      // 25% bigger than base (1.25x visual size)
	m.walkCycleSpeed = 2.0f;
	m.idleBobAmount  = 0.012f;
	m.walkBobAmount  = 0.06f;


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

	// Left arm -- large Minecraft-style swing (50° each way = 100° total)
	m.parts.push_back({
		{-0.37f, 1.05f, 0}, {0.10f, 0.35f, 0.10f},
		{0.80f, 0.65f, 0.50f, 1},
		{-0.37f, 1.40f, 0}, {1,0,0}, 50.0f, PI, 1.0f
	});

	// Right arm
	m.parts.push_back({
		{0.37f, 1.05f, 0}, {0.10f, 0.35f, 0.10f},
		{0.80f, 0.65f, 0.50f, 1},
		{0.37f, 1.40f, 0}, {1,0,0}, 50.0f, 0, 1.0f
	});

	// Left leg -- big stride (50° each way = 100° total)
	m.parts.push_back({
		{-0.12f, 0.35f, 0}, {0.12f, 0.35f, 0.12f},
		{0.22f, 0.22f, 0.32f, 1},
		{-0.12f, 0.70f, 0}, {1,0,0}, 50.0f, 0, 1.0f
	});

	// Right leg
	m.parts.push_back({
		{0.12f, 0.35f, 0}, {0.12f, 0.35f, 0.12f},
		{0.22f, 0.22f, 0.32f, 1},
		{0.12f, 0.70f, 0}, {1,0,0}, 50.0f, PI, 1.0f
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

// ======================================================================
// Cat: ~0.5 blocks tall, sleek body, pointy ears, long tail
// ======================================================================
inline BoxModel catModel() {
	const float PI = 3.14159265f;
	BoxModel m;
	m.totalHeight = 0.5f;
	m.modelScale  = 1.0f;
	m.walkCycleSpeed = 6.0f;
	m.idleBobAmount = 0.004f;
	m.walkBobAmount = 0.018f;

	// Body (sleek, elongated)
	m.parts.push_back({
		{0, 0.25f, 0}, {0.12f, 0.10f, 0.28f},
		{0.90f, 0.55f, 0.20f, 1}
	});

	// Head (round)
	m.parts.push_back({
		{0, 0.36f, -0.30f}, {0.12f, 0.11f, 0.11f},
		{0.92f, 0.58f, 0.22f, 1},
		{0, 0.34f, -0.18f}, {1,0,0}, 8.0f, 0, 0.5f
	});

	// Left ear (pointy)
	m.parts.push_back({
		{-0.08f, 0.48f, -0.28f}, {0.03f, 0.06f, 0.03f},
		{0.85f, 0.50f, 0.18f, 1}
	});

	// Right ear
	m.parts.push_back({
		{0.08f, 0.48f, -0.28f}, {0.03f, 0.06f, 0.03f},
		{0.85f, 0.50f, 0.18f, 1}
	});

	// Front legs (thin)
	m.parts.push_back({
		{-0.06f, 0.08f, -0.16f}, {0.03f, 0.10f, 0.03f},
		{0.88f, 0.52f, 0.18f, 1},
		{-0.06f, 0.18f, -0.16f}, {1,0,0}, 35.0f, 0, 1.0f
	});
	m.parts.push_back({
		{0.06f, 0.08f, -0.16f}, {0.03f, 0.10f, 0.03f},
		{0.88f, 0.52f, 0.18f, 1},
		{0.06f, 0.18f, -0.16f}, {1,0,0}, 35.0f, PI, 1.0f
	});

	// Back legs
	m.parts.push_back({
		{-0.06f, 0.08f, 0.16f}, {0.03f, 0.10f, 0.03f},
		{0.88f, 0.52f, 0.18f, 1},
		{-0.06f, 0.18f, 0.16f}, {1,0,0}, 35.0f, PI, 1.0f
	});
	m.parts.push_back({
		{0.06f, 0.08f, 0.16f}, {0.03f, 0.10f, 0.03f},
		{0.88f, 0.52f, 0.18f, 1},
		{0.06f, 0.18f, 0.16f}, {1,0,0}, 35.0f, 0, 1.0f
	});

	// Tail (long, curved up)
	m.parts.push_back({
		{0, 0.32f, 0.32f}, {0.02f, 0.02f, 0.12f},
		{0.85f, 0.50f, 0.18f, 1},
		{0, 0.28f, 0.28f}, {1,0,0}, 15.0f, 0, 1.5f
	});
	// Tail tip
	m.parts.push_back({
		{0, 0.38f, 0.44f}, {0.02f, 0.02f, 0.06f},
		{0.80f, 0.45f, 0.15f, 1},
		{0, 0.28f, 0.28f}, {1,0,0}, 20.0f, 0.5f, 1.5f
	});

	return m;
}

// ======================================================================
// Dog: ~0.7 blocks tall, 4 legs, pointy ears, tail
// ======================================================================
inline BoxModel dogModel() {
	const float PI = 3.14159265f;
	BoxModel m;
	m.totalHeight = 0.7f;
	m.modelScale  = 1.25f;
	m.walkCycleSpeed = 5.0f;
	m.idleBobAmount = 0.006f;
	m.walkBobAmount = 0.025f;

	// Body
	m.parts.push_back({
		{0, 0.38f, 0}, {0.18f, 0.16f, 0.35f},
		{0.75f, 0.55f, 0.35f, 1}
	});

	// Head
	m.parts.push_back({
		{0, 0.52f, -0.38f}, {0.16f, 0.14f, 0.16f},
		{0.78f, 0.58f, 0.38f, 1},
		{0, 0.50f, -0.22f}, {1,0,0}, 10.0f, 0, 0.5f
	});

	// Snout
	m.parts.push_back({
		{0, 0.46f, -0.54f}, {0.08f, 0.06f, 0.06f},
		{0.70f, 0.48f, 0.30f, 1},
		{0, 0.50f, -0.22f}, {1,0,0}, 10.0f, 0, 0.5f
	});

	// Left ear
	m.parts.push_back({
		{-0.12f, 0.66f, -0.34f}, {0.04f, 0.08f, 0.04f},
		{0.65f, 0.42f, 0.25f, 1}
	});

	// Right ear
	m.parts.push_back({
		{0.12f, 0.66f, -0.34f}, {0.04f, 0.08f, 0.04f},
		{0.65f, 0.42f, 0.25f, 1}
	});

	// Front-left leg
	m.parts.push_back({
		{-0.10f, 0.12f, -0.20f}, {0.05f, 0.14f, 0.05f},
		{0.72f, 0.52f, 0.32f, 1},
		{-0.10f, 0.26f, -0.20f}, {1,0,0}, 40.0f, 0, 1.0f
	});

	// Front-right leg
	m.parts.push_back({
		{0.10f, 0.12f, -0.20f}, {0.05f, 0.14f, 0.05f},
		{0.72f, 0.52f, 0.32f, 1},
		{0.10f, 0.26f, -0.20f}, {1,0,0}, 40.0f, PI, 1.0f
	});

	// Back-left leg
	m.parts.push_back({
		{-0.10f, 0.12f, 0.20f}, {0.05f, 0.14f, 0.05f},
		{0.72f, 0.52f, 0.32f, 1},
		{-0.10f, 0.26f, 0.20f}, {1,0,0}, 40.0f, PI, 1.0f
	});

	// Back-right leg
	m.parts.push_back({
		{0.10f, 0.12f, 0.20f}, {0.05f, 0.14f, 0.05f},
		{0.72f, 0.52f, 0.32f, 1},
		{0.10f, 0.26f, 0.20f}, {1,0,0}, 40.0f, 0, 1.0f
	});

	// Tail (wags on Y axis)
	m.parts.push_back({
		{0, 0.48f, 0.38f}, {0.03f, 0.03f, 0.10f},
		{0.72f, 0.52f, 0.32f, 1},
		{0, 0.45f, 0.35f}, {0,1,0}, 25.0f, 0, 3.0f
	});

	return m;
}

// ======================================================================
// Villager: ~1.8 blocks tall, humanoid proportions
// ======================================================================
inline BoxModel villagerModel() {
	const float PI = 3.14159265f;
	BoxModel m;
	m.totalHeight = 1.8f;
	m.modelScale  = 1.0f;
	m.walkCycleSpeed = 3.0f;
	m.idleBobAmount = 0.010f;
	m.walkBobAmount = 0.04f;

	// Head
	m.parts.push_back({
		{0, 1.55f, 0}, {0.20f, 0.20f, 0.20f},
		{0.85f, 0.72f, 0.58f, 1},
		{0, 1.35f, 0}, {1,0,0}, 4.0f, 0, 2.0f
	});

	// Hat (brown)
	m.parts.push_back({
		{0, 1.76f, 0}, {0.22f, 0.04f, 0.22f},
		{0.45f, 0.30f, 0.15f, 1},
		{0, 1.35f, 0}, {1,0,0}, 4.0f, 0, 2.0f
	});
	m.parts.push_back({
		{0, 1.83f, 0}, {0.14f, 0.08f, 0.14f},
		{0.45f, 0.30f, 0.15f, 1},
		{0, 1.35f, 0}, {1,0,0}, 4.0f, 0, 2.0f
	});

	// Nose
	m.parts.push_back({
		{0, 1.50f, -0.20f}, {0.04f, 0.06f, 0.04f},
		{0.78f, 0.62f, 0.48f, 1},
		{0, 1.35f, 0}, {1,0,0}, 4.0f, 0, 2.0f
	});

	// Torso (brown robe)
	m.parts.push_back({
		{0, 1.05f, 0}, {0.22f, 0.30f, 0.14f},
		{0.55f, 0.38f, 0.20f, 1}
	});

	// Robe lower (wider)
	m.parts.push_back({
		{0, 0.72f, 0}, {0.24f, 0.12f, 0.16f},
		{0.50f, 0.35f, 0.18f, 1}
	});

	// Belt (dark)
	m.parts.push_back({
		{0, 0.85f, -0.13f}, {0.20f, 0.03f, 0.02f},
		{0.25f, 0.15f, 0.08f, 1}
	});

	// Left arm (50° each way = 100° total)
	m.parts.push_back({
		{-0.32f, 1.05f, 0}, {0.08f, 0.30f, 0.08f},
		{0.55f, 0.38f, 0.20f, 1},
		{-0.32f, 1.35f, 0}, {1,0,0}, 50.0f, PI, 1.0f
	});

	// Right arm
	m.parts.push_back({
		{0.32f, 1.05f, 0}, {0.08f, 0.30f, 0.08f},
		{0.55f, 0.38f, 0.20f, 1},
		{0.32f, 1.35f, 0}, {1,0,0}, 50.0f, 0, 1.0f
	});

	// Left hand
	m.parts.push_back({
		{-0.32f, 0.73f, 0}, {0.06f, 0.04f, 0.06f},
		{0.85f, 0.72f, 0.58f, 1},
		{-0.32f, 1.35f, 0}, {1,0,0}, 50.0f, PI, 1.0f
	});

	// Right hand
	m.parts.push_back({
		{0.32f, 0.73f, 0}, {0.06f, 0.04f, 0.06f},
		{0.85f, 0.72f, 0.58f, 1},
		{0.32f, 1.35f, 0}, {1,0,0}, 50.0f, 0, 1.0f
	});

	// Left leg (50° each way = 100° total)
	m.parts.push_back({
		{-0.10f, 0.30f, 0}, {0.10f, 0.30f, 0.10f},
		{0.50f, 0.35f, 0.18f, 1},
		{-0.10f, 0.60f, 0}, {1,0,0}, 50.0f, 0, 1.0f
	});

	// Right leg
	m.parts.push_back({
		{0.10f, 0.30f, 0}, {0.10f, 0.30f, 0.10f},
		{0.50f, 0.35f, 0.18f, 1},
		{0.10f, 0.60f, 0}, {1,0,0}, 50.0f, PI, 1.0f
	});

	return m;
}

} // namespace agentworld::builtin

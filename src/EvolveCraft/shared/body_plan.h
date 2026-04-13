#pragma once

// BodyPlan — structural DNA for a creature.
//
// A body is a torso + a set of slotted parts. Each part has geometry, an
// offset relative to the torso, a color bias, and statistical effects (e.g.
// jaws add attack). For the vertical slice, parts use procedural geometry
// (sphere / cone / cylinder) rather than loaded .glb files.
//
// See docs/DESIGN.md §7 for the full design. This is a subset that gives us
// a visible parts-evolve system without blocking on an asset pipeline.

#include <glm/glm.hpp>
#include <cstdint>

namespace evolvecraft {

// Part geometry kinds the renderer knows how to instance.
enum class PartShape : uint8_t {
	Sphere,   // torso, eye
	Cone,     // jaw, spike
	Cylinder, // flagellum, tail
};

// Slots a part can occupy on a torso.
enum class SlotKind : uint8_t {
	Head,       // forward (+Z local)
	Tail,       // backward (-Z local)
	LeftSide,   // +X
	RightSide,  // -X
	Dorsal,     // +Y
	Ventral,    // -Y
	Eye,        // on top-front (offset)
	NumSlots,
};

struct PartDef {
	PartShape shape   = PartShape::Sphere;
	SlotKind  slot    = SlotKind::Head;
	glm::vec3 offset  = {0, 0, 0};   // relative to torso center (local space)
	glm::vec3 scale   = {1, 1, 1};
	glm::vec3 color   = {1, 1, 1};
	float     pitch   = 0.0f;        // radians — extra rotation about X
	float     yaw     = 0.0f;        // radians — extra rotation about Y
	float     wiggle  = 0.0f;        // animation amount (0..1): 0 = static, 1 = whips a lot
	// stat mods
	float     dAttack = 0.0f;
	float     dSpeed  = 0.0f;
	float     dSense  = 0.0f;
};

// Catalog of part presets — keep them semantic so the species builder is readable.
namespace parts {

inline PartDef jaw(glm::vec3 tint = {0.9f, 0.3f, 0.2f}) {
	PartDef p;
	p.shape = PartShape::Cone;
	p.slot = SlotKind::Head;
	p.offset = {0, 0.05f, 0.85f};   // front, slightly up
	p.scale  = {0.30f, 0.30f, 0.60f};
	p.color  = tint;
	p.pitch  = 1.5707963f; // 90deg — cone points +Z
	p.dAttack = 4.0f;
	return p;
}

inline PartDef flagellum(glm::vec3 tint = {0.85f, 0.85f, 1.0f}) {
	PartDef p;
	p.shape = PartShape::Cylinder;
	p.slot  = SlotKind::Tail;
	p.offset = {0, 0, -1.10f};
	p.scale  = {0.09f, 0.09f, 1.10f};
	p.color  = tint;
	p.pitch  = 1.5707963f;
	p.dSpeed = 1.2f;
	p.wiggle = 1.0f;
	return p;
}

inline PartDef spikeLeft(glm::vec3 tint = {1.0f, 0.85f, 0.6f}) {
	PartDef p;
	p.shape = PartShape::Cone;
	p.slot  = SlotKind::LeftSide;
	p.offset = {0.75f, 0.05f, 0};
	p.scale  = {0.15f, 0.15f, 0.55f};
	p.color  = tint;
	p.yaw    = 1.5707963f; // point +X
	p.pitch  = 1.5707963f;
	p.dAttack = 1.5f;
	return p;
}

inline PartDef spikeRight(glm::vec3 tint = {1.0f, 0.85f, 0.6f}) {
	PartDef p = spikeLeft(tint);
	p.slot = SlotKind::RightSide;
	p.offset.x = -p.offset.x;
	p.yaw = -1.5707963f; // point -X
	return p;
}

inline PartDef eye(glm::vec3 tint = {0.10f, 0.90f, 0.55f}) {
	PartDef p;
	p.shape = PartShape::Sphere;
	p.slot  = SlotKind::Eye;
	p.offset = {0, 0.40f, 0.35f};
	p.scale  = {0.16f, 0.16f, 0.16f};
	p.color  = tint;
	p.dSense = 6.0f;
	return p;
}

inline PartDef dorsalFin(glm::vec3 tint = {0.8f, 0.6f, 1.0f}) {
	PartDef p;
	p.shape = PartShape::Cone;
	p.slot  = SlotKind::Dorsal;
	p.offset = {0, 0.45f, 0};
	p.scale  = {0.08f, 0.6f, 0.45f};
	p.color  = tint;
	p.pitch  = 3.1415926f; // point +Y
	p.dSpeed = 0.4f;
	return p;
}

inline PartDef tail(glm::vec3 tint = {0.7f, 0.9f, 1.0f}) {
	PartDef p = flagellum(tint);
	p.scale = {0.18f, 0.18f, 0.90f};
	p.wiggle = 0.6f;
	return p;
}

} // namespace parts

// The full body plan. Max ~8 parts in vertical slice; fixed-size vector avoids
// allocations in the renderer.
struct BodyPlan {
	glm::vec3 torsoColor = {0.85f, 0.90f, 0.95f};
	glm::vec3 torsoScale = {1.0f, 0.75f, 1.25f}; // ovoid
	PartDef parts[8];
	int partCount = 0;

	void addPart(const PartDef& p) {
		if (partCount < 8) parts[partCount++] = p;
	}
};

} // namespace evolvecraft

#pragma once

#include <string>
#include <cstdint>

namespace civcraft {

// An annotation is a render-only adornment attached to a block. It does NOT
// occupy a block slot, has no collision, and cannot be interacted with as a
// block. A block holds at most one annotation.
//
// Slot describes where the annotation sits relative to its host block:
//   Top    — sits on top of the block (flowers, mushrooms, grass tufts).
//   Bottom — hangs below the block    (stalactite, roots, cobwebs).
//   Around — wraps the whole block    (moss, vines, frost).
//
// Each annotation type also maps to one slot (the client enforces this at
// render time), so "flower" is always Top and modders can't accidentally
// place a flower under a block.
enum class AnnotationSlot : uint8_t {
	Top    = 0,
	Bottom = 1,
	Around = 2,
};

struct Annotation {
	std::string typeId;         // e.g. "flower_red"
	AnnotationSlot slot = AnnotationSlot::Top;

	bool empty() const { return typeId.empty(); }
};

} // namespace civcraft

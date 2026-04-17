#pragma once

#include <string>
#include <cstdint>

namespace civcraft {

// Render-only block adornment: no collision, no slot use; ≤1 per block.
// Slot: Top (flowers), Bottom (stalactites), Around (vines/moss).
// Each annotation type maps to one slot (enforced at render time).
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

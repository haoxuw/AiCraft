// EvoCraft cell-stage part catalog — single source of truth for DNA cost,
// per-cell maximum count, and the diet a mouth implies. Phase 2 only uses the
// cost + max-count fields for the editor; Phase 3 (gameplay effects) will
// add speed/turn/damage modifiers that other systems read from this table.

#pragma once

#include "net_protocol.h"

#include <array>
#include <cstdint>

namespace evocraft {

struct PartSpec {
	const char*        name;       // human-readable; client uses for tooltips
	uint16_t           dnaCost;    // Spore canonical
	uint8_t            maxCount;   // hard cap per cell (0xff = effectively no cap)
	net::Diet          implDiet;   // mouths set diet on attach; non-mouths use 0xff
	uint8_t            isMouth;    // 1 if this part is a mouth (drives diet)
};

// Order MUST match net::PartKind enum.
inline const std::array<PartSpec, net::PART_COUNT>& partTable() {
	static const std::array<PartSpec, net::PART_COUNT> T = {{
		{"Filter",     15, 1,    net::DIET_HERBIVORE, 1},
		{"Jaw",        15, 1,    net::DIET_CARNIVORE, 1},
		{"Proboscis",  25, 1,    net::DIET_OMNIVORE,  1},
		{"Spike",      10, 1,    (net::Diet)255, 0},
		{"Poison",     15, 6,    (net::Diet)255, 0},
		{"Electric",   25, 6,    (net::Diet)255, 0},
		{"Flagella",   15, 6,    (net::Diet)255, 0},
		{"Cilia",      15, 6,    (net::Diet)255, 0},
		{"Jet",        25, 4,    (net::Diet)255, 0},
		{"Eye Beady",   5, 8,    (net::Diet)255, 0},
		{"Eye Stalk",   5, 8,    (net::Diet)255, 0},
		{"Eye Button",  5, 8,    (net::Diet)255, 0},
	}};
	return T;
}

// Mouths are mutually exclusive: only one mouth at a time. Returns true if
// kind is a mouth.
inline bool isMouth(uint8_t kind) {
	if (kind >= net::PART_COUNT) return false;
	return partTable()[kind].isMouth != 0;
}

inline uint16_t partCost(uint8_t kind) {
	if (kind >= net::PART_COUNT) return 0xffff;
	return partTable()[kind].dnaCost;
}

inline uint8_t partMaxCount(uint8_t kind) {
	if (kind >= net::PART_COUNT) return 0;
	return partTable()[kind].maxCount;
}

} // namespace evocraft

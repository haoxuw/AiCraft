#pragma once

/**
 * material_values.h — Material value table for item/block worth.
 *
 * Used by ConvertItem to validate value conservation:
 *   getMaterialValue(toItem) * toCount <= getMaterialValue(fromItem) * fromCount
 *
 * Unit: 1 base:dirt = 1.0
 */

#include <string>
#include <unordered_map>

namespace modcraft {

inline float getMaterialValue(const std::string& typeId) {
	static const std::unordered_map<std::string, float> s_values = {
		// Terrain blocks
		{"base:air",        0.0f},
		{"base:dirt",       1.0f},
		{"base:grass",      1.0f},
		{"base:stone",      2.0f},
		{"base:sand",       1.0f},
		{"base:gravel",     1.0f},
		{"base:wood",       4.0f},
		{"base:leaves",     0.5f},
		{"base:planks",     2.0f},
		{"base:stairs",     2.0f},
		{"base:chest",      6.0f},
		{"base:torch",      1.0f},
		{"base:bedrock",    0.0f},
		{"base:tnt",        4.0f},
		{"base:door",       3.0f},
		{"base:door_open",  3.0f},
		// Biological — 1 HP = 1 material point (enables hp→item and item→hp conservation)
		{"hp",              1.0f},
		// Items
		{"base:log",        4.0f},
		{"base:apple",      2.0f},
		{"base:bread",      4.0f},
		{"base:meat",       3.0f},
		{"base:cooked_meat",3.0f},
		{"base:egg",        1.0f},
		{"base:bucket",     8.0f},
		{"base:sword",     15.0f},
		{"base:shield",    12.0f},
		{"base:helmet",    10.0f},
		{"base:boots",     10.0f},
		{"base:cape",       8.0f},
		{"base:potion",     5.0f},
	};
	auto it = s_values.find(typeId);
	return (it != s_values.end()) ? it->second : 1.0f; // default: 1.0
}

} // namespace modcraft

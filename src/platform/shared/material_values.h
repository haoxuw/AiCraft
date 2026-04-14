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

namespace civcraft {

inline float getMaterialValue(const std::string& typeId) {
	static const std::unordered_map<std::string, float> s_values = {
		// Terrain blocks
		{"air",        0.0f},
		{"dirt",       1.0f},
		{"grass",      1.0f},
		{"stone",      2.0f},
		{"sand",       1.0f},
		{"gravel",     1.0f},
		{"wood",       4.0f},
		{"logs",      4.0f},   // tree log block
		{"leaves",     0.5f},
		{"planks",     2.0f},
		{"stairs",     2.0f},
		{"chest",      6.0f},
		{"torch",      1.0f},
		{"bedrock",    0.0f},
		{"tnt",        4.0f},
		{"door",       3.0f},
		{"door_open",  3.0f},
		// Biological — 1 HP = 1 material point (enables hp→item and item→hp conservation)
		{"hp",              1.0f},
		// Items
		{"apple",      2.0f},
		{"bread",      4.0f},
		{"meat",       3.0f},
		{"cooked_meat",3.0f},
		{"egg",        1.0f},
		{"bucket",     8.0f},
		{"sword",     15.0f},
		{"shield",    12.0f},
		{"helmet",    10.0f},
		{"boots",     10.0f},
		{"cape",       8.0f},
		{"potion",     5.0f},
		// Living entities — value is also the entity's inventory capacity
		// (sum of material values the entity can carry must be ≤ this).
		// Living entities — value is simultaneously:
		//   (a) the entity's inventory capacity (sum of carried items ≤ value)
		//   (b) the entity's max HP
		// Keep these in sync with artifacts/living/*.py display-only data.
		{"player",       128.0f},
		{"villager",      20.0f},
		{"dog",           15.0f},
		{"cat",            8.0f},
		{"pig",           10.0f},
		{"chicken",        5.0f},
		{"brave_chicken",  8.0f},
		{"crewmate",      20.0f},
		{"giant",         80.0f},
		{"knight",        40.0f},
		{"mage",          18.0f},
		{"skeleton",      22.0f},
	};
	auto it = s_values.find(typeId);
	return (it != s_values.end()) ? it->second : 1.0f; // default: 1.0
}

} // namespace civcraft

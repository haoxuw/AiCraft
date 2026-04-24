#pragma once

// Counter-based inventory (item_id → count). No slots, no stack limits.

#include <string>
#include <map>
#include <vector>
#include <cmath>

#include "material_values.h"

namespace civcraft {

// Main hand = hotbar selection, not a slot.
enum class WearSlot {
	Armor   = 0,
	Offhand = 1,
	Back    = 2,
};
constexpr int WEAR_SLOT_COUNT = 3;

// Accepts legacy names (helmet/body/left_hand/right_hand) for save migration.
inline bool wearSlotFromString(const std::string& s, WearSlot& out) {
	if (s == "armor" || s == "helmet" || s == "head" ||
	    s == "body" || s == "Armor" || s == "Helmet" || s == "Body") {
		out = WearSlot::Armor; return true;
	}
	if (s == "offhand" || s == "left_hand" || s == "right_hand" ||
	    s == "main_hand" || s == "Offhand" || s == "Left Hand" || s == "Right Hand") {
		out = WearSlot::Offhand; return true;
	}
	if (s == "back" || s == "Back") {
		out = WearSlot::Back; return true;
	}
	return false;
}

inline const char* equipSlotName(WearSlot slot) {
	switch (slot) {
	case WearSlot::Armor:   return "Armor";
	case WearSlot::Offhand: return "Offhand";
	case WearSlot::Back:    return "Back";
	default: return "?";
	}
}

class Inventory {
public:
	int add(const std::string& itemId, int count = 1) {
		m_items[itemId] += count;
		return m_items[itemId];
	}

	// Returns actual removed count; never below 0.
	int remove(const std::string& itemId, int count = 1) {
		auto it = m_items.find(itemId);
		if (it == m_items.end()) return 0;
		int removed = std::min(count, it->second);
		it->second -= removed;
		if (it->second <= 0) m_items.erase(it);
		return removed;
	}

	int count(const std::string& itemId) const {
		auto it = m_items.find(itemId);
		return it != m_items.end() ? it->second : 0;
	}

	bool has(const std::string& itemId, int n = 1) const {
		return count(itemId) >= n;
	}

	std::vector<std::pair<std::string, int>> items() const {
		std::vector<std::pair<std::string, int>> result;
		for (auto& [id, cnt] : m_items)
			if (cnt > 0) result.push_back({id, cnt});
		return result;
	}

	int distinctCount() const {
		int n = 0;
		for (auto& [_, cnt] : m_items)
			if (cnt > 0) n++;
		return n;
	}

	// Worn items still count toward carried mass. See material_values.h.
	float totalValue() const {
		float v = 0.0f;
		for (auto& [id, cnt] : m_items)
			if (cnt > 0) v += getMaterialValue(id) * (float)cnt;
		for (auto& id : m_equipped)
			if (!id.empty()) v += getMaterialValue(id);
		return v;
	}

	// capacity <= 0 means "no carry allowed". +infinity still short-circuits
	// to "always fits" for the rare unlimited-bin case (e.g. ground pile).
	// Every Living now has a finite capacity == material_value == max_hp;
	// humanoids are no longer exempt.
	bool canAccept(const std::string& itemId, int count, float capacity) const {
		if (capacity <= 0.0f) return false;
		if (std::isinf(capacity)) return true;
		float cost = getMaterialValue(itemId) * (float)count;
		return totalValue() + cost <= capacity + 1e-4f;
	}

	void clear() { m_items.clear(); }

	void equip(WearSlot slot, const std::string& itemId) {
		int idx = (int)slot;
		if (idx < 0 || idx >= WEAR_SLOT_COUNT) return;
		if (!m_equipped[idx].empty())
			add(m_equipped[idx], 1);
		m_equipped[idx] = itemId;
		if (!itemId.empty())
			remove(itemId, 1);
	}

	void unequip(WearSlot slot) {
		int idx = (int)slot;
		if (idx < 0 || idx >= WEAR_SLOT_COUNT) return;
		if (!m_equipped[idx].empty()) {
			add(m_equipped[idx], 1);
			m_equipped[idx].clear();
		}
	}

	const std::string& equipped(WearSlot slot) const {
		static const std::string empty;
		int idx = (int)slot;
		if (idx < 0 || idx >= WEAR_SLOT_COUNT) return empty;
		return m_equipped[idx];
	}

	bool hasEquipped(WearSlot slot) const {
		return !equipped(slot).empty();
	}

	// false → offhand in left hand (main=right); true → offhand in right hand (main=left).
	bool offhandInRightHand() const { return m_offhandRight; }
	void setOffhandInRightHand(bool right) { m_offhandRight = right; }

private:
	std::map<std::string, int> m_items;
	std::string m_equipped[WEAR_SLOT_COUNT];
	bool m_offhandRight = false;
};

} // namespace civcraft

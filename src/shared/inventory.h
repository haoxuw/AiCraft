#pragma once

/**
 * Inventory: a Counter of items (item_id -> count).
 *
 * Unlike Minecraft's slot-based grid, ModCraft uses a simple
 * counter. No stack limits, no slot management. Items are
 * displayed sorted by ID, skipping zero-count entries.
 *
 * Think Python: collections.Counter({"base:stone": 42, "base:jetpack": 1})
 */

#include <string>
#include <map>
#include <vector>

namespace modcraft {

// Equipment slots — what a character can wear.
// The main hand always shows the hotbar-selected item, so there is no
// "main hand" slot. Three persistent wear slots:
//   Armor   — helmet/body (one combined armor piece)
//   Offhand — shield, torch, etc. (the hand opposite the main hand)
//   Back    — cape, jetpack, parachute
enum class WearSlot {
	Armor   = 0,
	Offhand = 1,
	Back    = 2,
};
constexpr int WEAR_SLOT_COUNT = 3;

// Parse equip slot from Python artifact string. Accepts legacy
// names (helmet/body/head/left_hand/right_hand) for migration.
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
	return false; // not equippable
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
	// Add items. Returns the new count.
	int add(const std::string& itemId, int count = 1) {
		m_items[itemId] += count;
		return m_items[itemId];
	}

	// Remove items. Returns actual removed count. Never goes below 0.
	int remove(const std::string& itemId, int count = 1) {
		auto it = m_items.find(itemId);
		if (it == m_items.end()) return 0;
		int removed = std::min(count, it->second);
		it->second -= removed;
		if (it->second <= 0) m_items.erase(it);
		return removed;
	}

	// Get count of an item.
	int count(const std::string& itemId) const {
		auto it = m_items.find(itemId);
		return it != m_items.end() ? it->second : 0;
	}

	// Check if we have at least N of an item.
	bool has(const std::string& itemId, int n = 1) const {
		return count(itemId) >= n;
	}

	// All non-zero items sorted by ID (for display).
	std::vector<std::pair<std::string, int>> items() const {
		std::vector<std::pair<std::string, int>> result;
		for (auto& [id, cnt] : m_items)
			if (cnt > 0) result.push_back({id, cnt});
		return result;
	}

	// Number of distinct item types held.
	int distinctCount() const {
		int n = 0;
		for (auto& [_, cnt] : m_items)
			if (cnt > 0) n++;
		return n;
	}

	// Clear all items.
	void clear() { m_items.clear(); }

	// ---- Equipment (wearable items) ----

	void equip(WearSlot slot, const std::string& itemId) {
		int idx = (int)slot;
		if (idx < 0 || idx >= WEAR_SLOT_COUNT) return;
		// Unequip current item first (return to inventory)
		if (!m_equipped[idx].empty())
			add(m_equipped[idx], 1);
		m_equipped[idx] = itemId;
		// Remove from inventory (it's now worn)
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

	// Which hand visually displays the offhand item.
	// false → offhand drawn in left hand (main hand = right);
	// true  → offhand drawn in right hand (main hand = left).
	// Default false (most players are right-handed, shield in left).
	bool offhandInRightHand() const { return m_offhandRight; }
	void setOffhandInRightHand(bool right) { m_offhandRight = right; }

private:
	std::map<std::string, int> m_items;
	std::string m_equipped[WEAR_SLOT_COUNT];
	bool m_offhandRight = false;
};

} // namespace modcraft

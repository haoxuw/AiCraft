#pragma once

/**
 * Inventory: a Counter of items (item_id -> count).
 *
 * Unlike Minecraft's slot-based grid, Agentica uses a simple
 * counter. No stack limits, no slot management. Items are
 * displayed sorted by ID, skipping zero-count entries.
 *
 * Think Python: collections.Counter({"base:stone": 42, "base:jetpack": 1})
 */

#include <string>
#include <map>
#include <vector>

namespace agentica {

// Equipment slots — what a character can wear/hold
enum class WearSlot {
	LeftHand  = 0,  // sword, shield, tool
	RightHand = 1,  // sword, shield, tool
	Helmet    = 2,  // head armor
	Body      = 3,  // shoes + pants (combined slot)
	Back      = 4,  // cape, backpack, quiver
};
constexpr int WEAR_SLOT_COUNT = 5;

// Parse equip slot from Python artifact string
inline bool wearSlotFromString(const std::string& s, WearSlot& out) {
	if (s == "left_hand")  { out = WearSlot::LeftHand;  return true; }
	if (s == "right_hand") { out = WearSlot::RightHand; return true; }
	if (s == "helmet" || s == "head") { out = WearSlot::Helmet; return true; }
	if (s == "body")       { out = WearSlot::Body;      return true; }
	if (s == "back")       { out = WearSlot::Back;      return true; }
	return false; // not equippable
}

inline const char* equipSlotName(WearSlot slot) {
	switch (slot) {
	case WearSlot::LeftHand:  return "Left Hand";
	case WearSlot::RightHand: return "Right Hand";
	case WearSlot::Helmet:    return "Helmet";
	case WearSlot::Body:      return "Body";
	case WearSlot::Back:      return "Back";
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

	// ---- Hotbar (shortcut slots referencing items in the counter) ----

	// Assign an item type to a hotbar slot.
	void setHotbar(int slot, const std::string& itemId) {
		if (slot >= 0 && slot < HOTBAR_SLOTS)
			m_hotbar[slot] = itemId;
	}

	// Auto-assign items to hotbar slots 0..N in sorted order.
	// Call after any bulk inventory change (init, network sync, load).
	// Does not overwrite manually-assigned slots that still have items.
	void autoPopulateHotbar() {
		// Build set of items already explicitly assigned
		// Refill empty or stale slots from sorted item list
		int slot = 0;
		for (auto& [id, cnt] : m_items) {
			if (cnt <= 0) continue;
			if (slot >= HOTBAR_SLOTS) break;
			m_hotbar[slot++] = id;
		}
		// Clear any remaining slots (items may have been removed)
		for (; slot < HOTBAR_SLOTS; slot++)
			m_hotbar[slot].clear();
	}

	// Get the item type in a hotbar slot (empty string if unset).
	const std::string& hotbar(int slot) const {
		static const std::string empty;
		if (slot < 0 || slot >= HOTBAR_SLOTS) return empty;
		return m_hotbar[slot];
	}

	// Get the count of the item in a hotbar slot (0 if empty or depleted).
	int hotbarCount(int slot) const {
		auto& id = hotbar(slot);
		return id.empty() ? 0 : count(id);
	}

	// Check if hotbar slot has a usable item (exists in counter with count > 0).
	bool hotbarHasItem(int slot) const {
		return hotbarCount(slot) > 0;
	}

	static constexpr int HOTBAR_SLOTS = 10; // keys 1-9, 0

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

private:
	std::map<std::string, int> m_items;
	std::string m_hotbar[10];
	std::string m_equipped[WEAR_SLOT_COUNT];
};

} // namespace agentica

#pragma once

/**
 * Inventory: a Counter of items (item_id -> count).
 *
 * Unlike Minecraft's slot-based grid, AiCraft uses a simple
 * counter. No stack limits, no slot management. Items are
 * displayed sorted by ID, skipping zero-count entries.
 *
 * Think Python: collections.Counter({"base:stone": 42, "base:jetpack": 1})
 */

#include <string>
#include <map>
#include <vector>

namespace aicraft {

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

private:
	std::map<std::string, int> m_items;
	std::string m_hotbar[10]; // shortcut slots
};

} // namespace aicraft

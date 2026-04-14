#pragma once

/**
 * Hotbar — client-only view over the local player's Inventory.
 *
 * The server stores only a counter map (item_id → count). The hotbar is a
 * purely client-side convenience: 10 slots of "aliases" pointing into that
 * counter, so the player can bind number keys 1–9,0 to whichever items they
 * want on screen. The server knows nothing about hotbar slots.
 *
 * Slot state is populated from S_INVENTORY (via repopulateFrom) and can be
 * rearranged locally by drag/drop. The actual item count in each slot is
 * always resolved against the live Inventory at read time.
 */

#include "shared/inventory.h"
#include <string>
#include <array>

namespace civcraft {

class Hotbar {
public:
	static constexpr int SLOTS = 10; // keys 1-9, 0

	// Refill slots from the first `SLOTS` non-zero entries of `inv`, in the
	// inventory's stable iteration order. Clears any slots beyond the item
	// count. Call this after any S_INVENTORY that replaces the full map.
	void repopulateFrom(const Inventory& inv) {
		// Preferred hotbar layout: sword first, then shield, tools, blocks,
		// then everything else in inventory order. Keeps the most useful
		// items on the leftmost keys (1,2,3...) regardless of item-id
		// alphabetical ordering in std::map.
		static const char* kPriority[] = {
			"sword", "shield",
			"wood_axe", "stone_pickaxe", "shovel",
			"base:stone", "base:wood", "base:dirt",
			"potion",
		};
		int slot = 0;
		std::array<bool, SLOTS> used{};
		(void)used;
		auto seen = [&](const std::string& id) {
			for (int i = 0; i < slot; i++) if (m_slots[i] == id) return true;
			return false;
		};
		for (const char* id : kPriority) {
			if (slot >= SLOTS) break;
			if (inv.count(id) > 0 && !seen(id)) m_slots[slot++] = id;
		}
		for (const auto& [id, cnt] : inv.items()) {
			if (slot >= SLOTS) break;
			if (cnt > 0 && !seen(id)) m_slots[slot++] = id;
		}
		for (; slot < SLOTS; slot++) m_slots[slot].clear();
	}

	void set(int slot, const std::string& itemId) {
		if (slot >= 0 && slot < SLOTS) m_slots[slot] = itemId;
	}

	const std::string& get(int slot) const {
		static const std::string empty;
		return (slot >= 0 && slot < SLOTS) ? m_slots[slot] : empty;
	}

	// Count of the item bound to `slot` as currently held in `inv` (0 if the
	// slot is empty or the item has been used up).
	int count(int slot, const Inventory& inv) const {
		const auto& id = get(slot);
		return id.empty() ? 0 : inv.count(id);
	}

	bool hasItem(int slot, const Inventory& inv) const {
		return count(slot, inv) > 0;
	}

	void clear() {
		for (auto& s : m_slots) s.clear();
	}

private:
	std::array<std::string, SLOTS> m_slots;
};

} // namespace civcraft

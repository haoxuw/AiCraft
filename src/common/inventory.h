#pragma once

#include <string>
#include <vector>
#include <algorithm>

namespace aicraft {

struct ItemStack {
	std::string type;  // block/item string_id, e.g. "base:stone"
	int count = 0;

	bool empty() const { return count <= 0 || type.empty(); }

	void clear() { type.clear(); count = 0; }

	// Add items, return leftover that didn't fit
	int add(int n, int maxStack = 64) {
		int space = maxStack - count;
		int toAdd = std::min(n, space);
		count += toAdd;
		return n - toAdd;
	}

	// Remove items, return actual removed count
	int remove(int n) {
		int r = std::min(n, count);
		count -= r;
		if (count <= 0) clear();
		return r;
	}
};

class Inventory {
public:
	explicit Inventory(int size = 36) : m_slots(size) {}

	int size() const { return (int)m_slots.size(); }
	const ItemStack& slot(int i) const { return m_slots[i]; }
	ItemStack& slot(int i) { return m_slots[i]; }

	// Add item, tries existing stacks first then empty slots. Returns leftover.
	int addItem(const std::string& type, int count = 1) {
		// Merge with existing stacks
		for (auto& s : m_slots) {
			if (s.type == type && !s.empty()) {
				count = s.add(count);
				if (count == 0) return 0;
			}
		}
		// Fill empty slots
		for (auto& s : m_slots) {
			if (s.empty()) {
				s.type = type;
				count = s.add(count);
				if (count == 0) return 0;
			}
		}
		return count;
	}

	bool removeFromSlot(int slot, int count = 1) {
		if (slot < 0 || slot >= size()) return false;
		auto& s = m_slots[slot];
		if (s.empty() || s.count < count) return false;
		s.remove(count);
		return true;
	}

	bool hasItem(const std::string& type) const {
		for (auto& s : m_slots)
			if (s.type == type && !s.empty()) return true;
		return false;
	}

private:
	std::vector<ItemStack> m_slots;
};

} // namespace aicraft

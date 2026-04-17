#pragma once

// Hotbar: client-only 10-slot view over Inventory. Slots are aliases into the
// server's counter map; server knows nothing about hotbar slots. Populated from
// S_INVENTORY, locally drag/drop-reorderable; counts resolved live at read.

#include "logic/inventory.h"
#include <string>
#include <array>
#include <fstream>
#include <sstream>

namespace civcraft {

class Hotbar {
public:
	static constexpr int SLOTS = 10; // keys 1-9, 0

	// Refill from first `SLOTS` non-zero entries; call after an S_INVENTORY full-replace.
	void repopulateFrom(const Inventory& inv) {
		// Priority order keeps useful items on leftmost keys regardless of std::map order.
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

	// Merge update: keep in-stock slots, clear empty ones, append new items.
	// Use on every S_INVENTORY after initial repopulate so drag-drop survives pickups.
	void mergeFrom(const Inventory& inv) {
		auto seen = [&](const std::string& id) {
			for (const auto& s : m_slots) if (s == id) return true;
			return false;
		};
		for (auto& s : m_slots) {
			if (!s.empty() && inv.count(s) <= 0) s.clear();
		}
		for (const auto& [id, cnt] : inv.items()) {
			if (cnt <= 0 || seen(id)) continue;
			for (auto& s : m_slots) {
				if (s.empty()) { s = id; break; }
			}
		}
	}

	// Newline-separated; empty slots = blank lines so positions round-trip.
	bool saveToFile(const std::string& path) const {
		std::ofstream f(path, std::ios::trunc);
		if (!f) return false;
		for (const auto& s : m_slots) f << s << "\n";
		return true;
	}

	// Returns false only if file missing; short files leave trailing slots empty.
	// Caller falls back to repopulateFrom on false.
	bool loadFromFile(const std::string& path) {
		std::ifstream f(path);
		if (!f) return false;
		std::array<std::string, SLOTS> tmp;
		for (int i = 0; i < SLOTS; i++) {
			if (!std::getline(f, tmp[i])) break;
		}
		m_slots = tmp;
		return true;
	}

	void set(int slot, const std::string& itemId) {
		if (slot >= 0 && slot < SLOTS) m_slots[slot] = itemId;
	}

	const std::string& get(int slot) const {
		static const std::string empty;
		return (slot >= 0 && slot < SLOTS) ? m_slots[slot] : empty;
	}

	// Count from live inv (0 if slot empty or item used up).
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

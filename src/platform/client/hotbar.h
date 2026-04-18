#pragma once

// Client-only 10-slot alias layer over Inventory. Slots store itemIds; the
// real stock is always read from the underlying Inventory. Server never
// sees the hotbar.
//
// `selected` drives the held-item model (main hand) and the LMB/RMB/Q
// actions. Scroll wheel cycles it in FPS; 1..9/0 jump to a slot.

#include "logic/inventory.h"
#include <array>
#include <string>
#include <fstream>

namespace civcraft {

class Hotbar {
public:
	static constexpr int SLOTS = 10;

	int selected = 0;

	const std::string& get(int slot) const {
		static const std::string empty;
		return (slot >= 0 && slot < SLOTS) ? m_slots[slot] : empty;
	}
	void set  (int slot, const std::string& id) { if (slot >= 0 && slot < SLOTS) m_slots[slot] = id; }
	void clear(int slot)                        { if (slot >= 0 && slot < SLOTS) m_slots[slot].clear(); }
	void swap (int a, int b) {
		if (a >= 0 && a < SLOTS && b >= 0 && b < SLOTS) std::swap(m_slots[a], m_slots[b]);
	}

	// Currently held item — "" (bare hands) if the selected slot is empty
	// or the player no longer carries any of that item.
	const std::string& mainHand(const Inventory& inv) const {
		static const std::string empty;
		if (selected < 0 || selected >= SLOTS) return empty;
		const auto& id = m_slots[selected];
		return (!id.empty() && inv.count(id) > 0) ? id : empty;
	}

	int count(int slot, const Inventory& inv) const {
		const auto& id = get(slot);
		return id.empty() ? 0 : inv.count(id);
	}

	// Prime an empty hotbar from the player's current inventory. Priority
	// items go left so sword/pickaxe end up on 1/2. Called once after the
	// initial S_INVENTORY when no saved layout exists.
	void repopulateFrom(const Inventory& inv) {
		static const char* kPriority[] = {
			"sword", "shield",
			"wood_axe", "stone_pickaxe", "shovel",
			"stone", "wood", "dirt",
			"potion",
		};
		int slot = 0;
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

	// Called after every S_INVENTORY so drag-placed slots survive pickups.
	// Clears slots whose item the player no longer carries; auto-fills empty
	// slots with newly-picked-up items.
	void mergeFrom(const Inventory& inv) {
		auto seen = [&](const std::string& id) {
			for (const auto& s : m_slots) if (s == id) return true;
			return false;
		};
		for (auto& s : m_slots)
			if (!s.empty() && inv.count(s) <= 0) s.clear();
		for (const auto& [id, cnt] : inv.items()) {
			if (cnt <= 0 || seen(id)) continue;
			for (auto& s : m_slots) {
				if (s.empty()) { s = id; break; }
			}
		}
	}

	// Newline-separated; position round-trips so drag layout persists.
	bool saveToFile(const std::string& path) const {
		std::ofstream f(path, std::ios::trunc);
		if (!f) return false;
		for (const auto& s : m_slots) f << s << "\n";
		f << selected << "\n";
		return true;
	}
	bool loadFromFile(const std::string& path) {
		std::ifstream f(path);
		if (!f) return false;
		std::array<std::string, SLOTS> tmp;
		for (int i = 0; i < SLOTS; i++) if (!std::getline(f, tmp[i])) break;
		int s = 0;
		std::string line;
		if (std::getline(f, line)) { try { s = std::stoi(line); } catch (...) {} }
		m_slots = tmp;
		selected = (s >= 0 && s < SLOTS) ? s : 0;
		return true;
	}

private:
	std::array<std::string, SLOTS> m_slots{};
};

} // namespace civcraft

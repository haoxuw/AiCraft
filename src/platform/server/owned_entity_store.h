#pragma once

// Player-owned NPC state across sessions. Server runs no AI, so on disconnect we
// snapshot everything the client owns and restore on next login. Keyed by
// SeatId — the same durable id used for ownership on `Prop::Owner` (Phase 5 of
// docs/28_SEATS_AND_OWNERSHIP.md). Keying by seat (not `character_skin`) means
// a player switching skins or reclaiming a seat from a different keypair still
// gets their NPCs back. Disk side: world_save.h.

#include "logic/entity.h"
#include "server/seat_registry.h"
#include "server/entity_manager.h"
#include "logic/inventory.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cstdio>

namespace solarium {

// Dead NPCs + unknown types dropped at snapshot, not restore.
struct OwnedEntitySnapshot {
	std::string typeId;
	glm::vec3   position{0};
	glm::vec3   velocity{0};
	float       yaw = 0;
	std::unordered_map<std::string, PropValue> props;
	// Mirrors entities.bin shape.
	std::vector<std::pair<std::string, int>>         items;
	std::vector<std::pair<std::string, std::string>> equipment;
};

class OwnedEntityStore {
public:
	using Map = std::unordered_map<SeatId, std::vector<OwnedEntitySnapshot>>;

	// Last-disconnect-wins. Must be called before removeClient's despawn loop.
	// Player entity intentionally excluded (respawned fresh by addClient).
	void snapshot(EntityManager& entities, SeatId ownerSeat) {
		if (ownerSeat == SEAT_NONE) return;
		std::vector<OwnedEntitySnapshot> snaps;
		entities.forEach([&](Entity& e) {
			if (e.getProp<int>(Prop::Owner, 0) != (int)ownerSeat) return;
			if (e.def().playable) return;  // skip the player entity itself
			if (!e.alive()) return;
			snaps.push_back(captureEntity(e));
		});
		if (snaps.empty()) {
			m_snapshots.erase(ownerSeat);
			return;
		}
		std::printf("[Server] Snapshotted %zu owned entities for seat %u\n",
		            snaps.size(), ownerSeat);
		m_snapshots[ownerSeat] = std::move(snaps);
	}

	// Consumes snapshot (crash mid-restore can't duplicate mobs). Returns true
	// if anything restored so caller skips fresh template spawn.
	bool restore(EntityManager& entities, SeatId ownerSeat) {
		if (ownerSeat == SEAT_NONE) return false;
		auto it = m_snapshots.find(ownerSeat);
		if (it == m_snapshots.end() || it->second.empty()) return false;
		int restored = 0, dropped = 0;
		for (auto& s : it->second) {
			if (respawnOne(entities, ownerSeat, s)) restored++;
			else                                     dropped++;
		}
		m_snapshots.erase(it);
		std::printf("[Server] Restored %d owned entities for seat %u%s\n",
		            restored, ownerSeat,
		            dropped ? (" (" + std::to_string(dropped) + " dropped)").c_str() : "");
		return restored > 0;
	}

	// Persistence hooks (world_save.h).
	const Map& all() const { return m_snapshots; }
	void loadFromDisk(SeatId ownerSeat, std::vector<OwnedEntitySnapshot> snaps) {
		if (ownerSeat == SEAT_NONE || snaps.empty()) return;
		m_snapshots[ownerSeat] = std::move(snaps);
	}
	size_t seatCount() const { return m_snapshots.size(); }

private:
	static OwnedEntitySnapshot captureEntity(const Entity& e) {
		OwnedEntitySnapshot s;
		s.typeId   = e.typeId();
		s.position = e.position;
		s.velocity = e.velocity;
		s.yaw      = e.yaw;
		s.props    = e.props();
		if (e.inventory) {
			for (auto& [id, cnt] : e.inventory->items())
				if (cnt > 0) s.items.push_back({id, cnt});
			for (int i = 0; i < WEAR_SLOT_COUNT; i++) {
				const auto& eq = e.inventory->equipped((WearSlot)i);
				if (!eq.empty())
					s.equipment.push_back({equipSlotName((WearSlot)i), eq});
			}
		}
		return s;
	}

	// Returns false if type no longer registered (mod removed between sessions).
	static bool respawnOne(EntityManager& entities, SeatId ownerSeat, const OwnedEntitySnapshot& s) {
		auto props = s.props;
		props[Prop::Owner] = (int)ownerSeat;
		EntityId eid = entities.spawn(s.typeId, s.position, props);
		if (eid == ENTITY_NONE) {
			std::printf("[Server] Restore: type '%s' no longer registered; dropped\n",
			            s.typeId.c_str());
			return false;
		}
		Entity* e = entities.get(eid);
		if (!e) return false;
		e->velocity = s.velocity;
		e->yaw      = s.yaw;
		if (e->inventory) {
			e->inventory->clear();
			for (auto& [id, cnt] : s.items)
				e->inventory->add(id, cnt);
			for (auto& [slotName, eqId] : s.equipment) {
				WearSlot ws;
				if (wearSlotFromString(slotName, ws)) {
					e->inventory->add(eqId, 1);
					e->inventory->equip(ws, eqId);
				}
			}
		}
		return true;
	}

	Map m_snapshots;  // SeatId → NPCs
};

} // namespace solarium

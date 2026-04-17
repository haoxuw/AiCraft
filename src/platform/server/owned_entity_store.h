#pragma once

// Player-owned NPC state across sessions. Server runs no AI, so on disconnect we
// snapshot everything the client owns and restore on next login. Keyed by
// character_skin (not playerId) so it survives server restarts. Disk side: world_save.h.

#include "logic/entity.h"
#include "server/entity_manager.h"
#include "logic/inventory.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cstdio>

namespace civcraft {

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
	// Preserved so a mid-journey dog resumes instead of warping home.
	bool        navActive = false;
	glm::vec3   navLongGoal{0};
};

class OwnedEntityStore {
public:
	using Map = std::unordered_map<std::string, std::vector<OwnedEntitySnapshot>>;

	// Last-disconnect-wins. Must be called before removeClient's despawn loop.
	void snapshot(EntityManager& entities, EntityId playerId, const std::string& skin) {
		std::vector<OwnedEntitySnapshot> snaps;
		entities.forEach([&](Entity& e) {
			if (e.id() == playerId) return;
			if (e.getProp<int>(Prop::Owner, 0) != (int)playerId) return;
			if (!e.alive()) return;
			snaps.push_back(captureEntity(e));
		});
		if (snaps.empty()) {
			m_snapshots.erase(skin);
			return;
		}
		std::printf("[Server] Snapshotted %zu owned entities for '%s'\n",
		            snaps.size(), skin.c_str());
		m_snapshots[skin] = std::move(snaps);
	}

	// Consumes snapshot (crash mid-restore can't duplicate mobs). Returns true
	// if anything restored so caller skips fresh template spawn.
	bool restore(EntityManager& entities, EntityId ownerId, const std::string& skin) {
		auto it = m_snapshots.find(skin);
		if (it == m_snapshots.end() || it->second.empty()) return false;
		int restored = 0, dropped = 0;
		for (auto& s : it->second) {
			if (respawnOne(entities, ownerId, s)) restored++;
			else                                   dropped++;
		}
		m_snapshots.erase(it);
		std::printf("[Server] Restored %d owned entities for '%s'%s\n",
		            restored, skin.c_str(),
		            dropped ? (" (" + std::to_string(dropped) + " dropped)").c_str() : "");
		return restored > 0;
	}

	// Persistence hooks (world_save.h).
	const Map& all() const { return m_snapshots; }
	void loadFromDisk(const std::string& skin, std::vector<OwnedEntitySnapshot> snaps) {
		if (snaps.empty()) return;
		m_snapshots[skin] = std::move(snaps);
	}
	size_t skinCount() const { return m_snapshots.size(); }

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
		s.navActive   = e.nav.active;
		s.navLongGoal = e.nav.longGoal;
		return s;
	}

	// Returns false if type no longer registered (mod removed between sessions).
	static bool respawnOne(EntityManager& entities, EntityId ownerId, const OwnedEntitySnapshot& s) {
		auto props = s.props;
		props[Prop::Owner] = (int)ownerId;
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
		if (s.navActive) e->nav.setGoal(s.navLongGoal);
		return true;
	}

	Map m_snapshots;  // character_skin → NPCs
};

} // namespace civcraft

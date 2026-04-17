#pragma once

/**
 * OwnedEntityStore — persists player-owned NPC state across client sessions.
 *
 * Why this exists: the server runs no AI. NPCs are only animated while their
 * owning GUI client is connected. When a client disconnects we take a
 * snapshot of every entity it owns (position, HP, inventory, nav goal) and
 * stash it keyed by character skin. When the same skin logs in again we
 * re-spawn those NPCs in place instead of re-running the world template's
 * fresh-spawn path. The on-disk persistence of these snapshots lives in
 * world_save.h; this class owns the in-memory side.
 *
 * Keyed by the player's `character_skin` prop (e.g. "knight") so the
 * mapping survives server restarts where the ephemeral playerId changes.
 */

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

// Snapshot of one owned NPC, taken at client disconnect so the same mob
// can be re-spawned with its current state when the owner logs back in.
// Server-side only — no AI runs on the server, so dead NPCs and unknown
// types are dropped at snapshot time rather than restored.
struct OwnedEntitySnapshot {
	std::string typeId;            // e.g. "pig" — looked up via EntityManager on restore
	glm::vec3   position{0};
	glm::vec3   velocity{0};
	float       yaw = 0;
	std::unordered_map<std::string, PropValue> props;
	// Inventory (if entity def has one). Mirrors entities.bin shape.
	std::vector<std::pair<std::string, int>>         items;
	std::vector<std::pair<std::string, std::string>> equipment; // slotName, itemId
	// Nav goal — preserved so a dog mid-journey resumes instead of warping home.
	bool        navActive = false;
	glm::vec3   navLongGoal{0};
};

class OwnedEntityStore {
public:
	using Map = std::unordered_map<std::string, std::vector<OwnedEntitySnapshot>>;

	// Build a snapshot for every entity owned by playerId (excluding the
	// player themselves and any dead NPCs) and stash under skin. Overwrites
	// any prior snapshot for that skin — last disconnect wins.
	// Called just before removeClient's despawn loop so the world sees mobs
	// vanish at the same moment they get recorded.
	void snapshot(EntityManager& entities, EntityId playerId, const std::string& skin) {
		std::vector<OwnedEntitySnapshot> snaps;
		entities.forEach([&](Entity& e) {
			if (e.id() == playerId) return;                           // player re-spawns fresh
			if (e.getProp<int>(Prop::Owner, 0) != (int)playerId) return;
			if (!e.alive()) return;                                    // dead → let it go
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

	// Re-spawn everything for `skin` under the new ownerId, consuming the
	// snapshot (so a crash mid-restore can't duplicate mobs). Returns true
	// if anything was restored, so the caller can skip the fresh template
	// spawn. Unknown types are dropped with a warning.
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

	// ─── Persistence hooks (used by world_save.h) ───────────────────
	// Read-only iteration for saving to disk.
	const Map& all() const { return m_snapshots; }
	// Install snapshots loaded from disk. Overwrites any existing entry
	// for the given skin — normal path since loadWorld runs on an empty store.
	void loadFromDisk(const std::string& skin, std::vector<OwnedEntitySnapshot> snaps) {
		if (snaps.empty()) return;
		m_snapshots[skin] = std::move(snaps);
	}
	size_t skinCount() const { return m_snapshots.size(); }

private:
	// Freeze `e` into a portable snapshot. Inventory is only captured if the
	// entity def actually has one — most mobs don't.
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

	// Re-create one entity from its snapshot. Returns false if the entity
	// type no longer exists in the registry (mod removed between sessions).
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

	Map m_snapshots;  // character_skin → owned NPCs awaiting owner's next login
};

} // namespace civcraft

#pragma once

/**
 * rts_executor.h — Client-side RTS pathfinding + driving.
 *
 * Owns waypoint paths for each commanded entity and, each tick, computes a
 * steering target per entity (cell-center of the current waypoint). The
 * caller uses this target to (a) drive the possessed player via the existing
 * virtual-joystick path, and (b) emit ActionProposal::Move for every *other*
 * owned commanded unit.
 *
 * The server is unaware of any of this — it only sees incremental Move
 * proposals, exactly as if the user were piloting each unit with WASD.
 * That matches Rule 4 (all intelligence runs on clients) and Rule 0
 * (server accepts only four action types).
 *
 * Planning uses GridPlanner::planBatch() from agent/pathfind.h with a
 * WorldView backed by the client's chunk mirror.
 */

#include "agent/pathfind.h"
#include "shared/action.h"
#include "shared/block_registry.h"
#include "shared/chunk_source.h"
#include "shared/constants.h"
#include "shared/entity.h"
#include "shared/server_interface.h"
#include <glm/glm.hpp>
#include <cmath>
#include <cstdio>
#include <optional>
#include <unordered_map>
#include <vector>

namespace civcraft {

// WorldView backed by the client's ChunkSource + BlockRegistry.
// Mirrors the server's SolidFnWorldView but reads from the client's
// locally cached chunks — unloaded chunks report !isSolid (air), which
// is the same behaviour as GridPlanner expects elsewhere.
struct ClientChunkWorldView : public WorldView {
	ChunkSource&          chunks;
	const BlockRegistry&  blocks;
	ClientChunkWorldView(ChunkSource& c, const BlockRegistry& b) : chunks(c), blocks(b) {}
	bool isSolid(glm::ivec3 p) const override {
		return blocks.get(chunks.getBlock(p.x, p.y, p.z)).solid;
	}
};

class RtsExecutor {
public:
	// Replace any previous plan for these entities with a fresh per-unit A*.
	// Each unit gets a *distinct* formation cell around `goal` so they don't
	// all pile on the same block. `starts` and `entityIds` are parallel.
	void planGroup(const std::vector<EntityId>& entityIds,
	               const std::vector<glm::ivec3>& starts,
	               glm::ivec3 goal,
	               ChunkSource& chunks, const BlockRegistry& blocks) {
		if (entityIds.empty()) return;
		ClientChunkWorldView view(chunks, blocks);
		GridPlanner planner(view);

		auto formationGoals = buildFormationGoals(goal, (int)entityIds.size(), view);

		for (size_t i = 0; i < entityIds.size(); i++) {
			EntityId eid  = entityIds[i];
			glm::ivec3 gg = formationGoals[i];
			Path p        = planner.plan(starts[i], gg);
			m_plans[eid]   = std::move(p);
			m_cursors[eid] = 0;
			m_goals[eid]   = gg;
			printf("[RTS-CLIENT] plan entity=%u goal=(%d,%d,%d) steps=%zu cost=%.2f partial=%d\n",
			       (unsigned)eid, gg.x, gg.y, gg.z,
			       m_plans[eid].steps.size(),
			       m_plans[eid].cost, m_plans[eid].partial ? 1 : 0);
		}
	}

	// Advance this entity's cursor past any waypoints it has arrived at
	// (XZ-proximity) and return the next steering target (cell center).
	// Returns nullopt if no plan exists or the plan is exhausted.
	std::optional<glm::vec3> steerTargetFor(EntityId eid, glm::vec3 pos) {
		auto it = m_plans.find(eid);
		if (it == m_plans.end()) return std::nullopt;
		const auto& path = it->second;
		int& cursor = m_cursors[eid];
		while (cursor < (int)path.steps.size()) {
			glm::ivec3 wp = path.steps[cursor].pos;
			float cx = wp.x + 0.5f, cz = wp.z + 0.5f;
			float dx = pos.x - cx, dz = pos.z - cz;
			if (std::sqrt(dx*dx + dz*dz) < 1.3f) { cursor++; continue; }
			break;
		}
		if (cursor >= (int)path.steps.size()) return std::nullopt;
		glm::ivec3 wp = path.steps[cursor].pos;
		return glm::vec3{wp.x + 0.5f, (float)wp.y, wp.z + 0.5f};
	}

	// Emit ActionProposal::Move for every commanded entity *except*
	// `excludedId` (typically the possessed player, who is driven via the
	// gameplay virtual-joystick instead). walkSpeedFallback is used for
	// entities whose def has walk_speed==0.
	void driveRemote(ServerInterface& server, EntityId excludedId,
	                 float walkSpeedFallback = 2.0f) {
		std::vector<EntityId> arrived;
		for (auto& [eid, path] : m_plans) {
			if (eid == excludedId) continue;
			Entity* e = server.getEntity(eid);
			if (!e) { arrived.push_back(eid); continue; }
			auto steer = steerTargetFor(eid, e->position);
			if (!steer) { arrived.push_back(eid); continue; }

			float walkSpeed = e->def().walk_speed;
			if (walkSpeed <= 0) walkSpeed = walkSpeedFallback;

			glm::vec3 d = *steer - e->position;
			d.y = 0;
			float len = std::sqrt(d.x*d.x + d.z*d.z);
			if (len < 0.001f) continue;
			d /= len;

			ActionProposal move;
			move.type         = ActionProposal::Move;
			move.actorId      = eid;
			move.desiredVel   = {d.x * walkSpeed, 0, d.z * walkSpeed};
			move.hasClientPos = false;   // server runs physics for this entity
			move.lookPitch    = e->lookPitch;
			move.lookYaw      = e->lookYaw;
			server.sendAction(move);
		}
		for (auto eid : arrived) cancel(eid);
	}

	void cancel(EntityId eid) {
		m_plans.erase(eid);
		m_cursors.erase(eid);
		m_goals.erase(eid);
	}

	void cancelAll() {
		m_plans.clear();
		m_cursors.clear();
		m_goals.clear();
	}

	bool has(EntityId eid) const { return m_plans.count(eid) > 0; }

	// For F3 debug viz (Phase B).
	const Path* pathFor(EntityId eid) const {
		auto it = m_plans.find(eid);
		return it == m_plans.end() ? nullptr : &it->second;
	}
	int cursorFor(EntityId eid) const {
		auto it = m_cursors.find(eid);
		return it == m_cursors.end() ? 0 : it->second;
	}

private:
	std::unordered_map<EntityId, Path>       m_plans;
	std::unordered_map<EntityId, int>        m_cursors;
	std::unordered_map<EntityId, glm::ivec3> m_goals;

	// Spread N units into a cols×rows grid centered on `clicked`. Each cell
	// is snapped to the nearest standable Y (search ±4) so slopes don't
	// produce unreachable goals. If a slot is unreachable even then, fall
	// back to the clicked cell.
	static std::vector<glm::ivec3> buildFormationGoals(
			glm::ivec3 clicked, int n, const WorldView& view) {
		std::vector<glm::ivec3> out;
		out.reserve(n);
		if (n == 1) { out.push_back(clicked); return out; }

		int cols = (int)std::ceil(std::sqrt((float)n));
		int rows = (n + cols - 1) / cols;
		const int spacing = 2;                 // blocks between slots
		int offX = (cols - 1) * spacing / 2;
		int offZ = (rows - 1) * spacing / 2;

		auto standable = [&](glm::ivec3 p) {
			if (!view.isSolid({p.x, p.y - 1, p.z})) return false;
			if ( view.isSolid(p))                   return false;
			if ( view.isSolid({p.x, p.y + 1, p.z})) return false;
			return true;
		};
		auto snapY = [&](glm::ivec3 p) -> glm::ivec3 {
			for (int dy = 0; dy <= 4; dy++) {
				glm::ivec3 up{p.x, p.y + dy, p.z};
				if (standable(up)) return up;
				glm::ivec3 dn{p.x, p.y - dy, p.z};
				if (standable(dn)) return dn;
			}
			return p;
		};

		for (int i = 0; i < n; i++) {
			int gx = clicked.x + (i % cols) * spacing - offX;
			int gz = clicked.z + (i / cols) * spacing - offZ;
			out.push_back(snapY({gx, clicked.y, gz}));
		}
		return out;
	}
};

} // namespace civcraft

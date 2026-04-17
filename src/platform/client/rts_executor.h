#pragma once

// Client-side RTS pathfinding + driving (async). ONE shared flow field per group
// command drives every unit by sampling it each tick. Rule 4 (client intelligence)
// + Rule 0 (emits only Move proposals).

#include "agent/pathfind.h"
#include "logic/action.h"
#include "logic/block_registry.h"
#include "logic/chunk_source.h"
#include "logic/constants.h"
#include "logic/entity.h"
#include "net/server_interface.h"
#include <glm/glm.hpp>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <future>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

namespace civcraft {

// Unloaded chunks report !isSolid (air), matching GridPlanner's expectation.
struct ClientChunkWorldView : public WorldView {
	ChunkSource&          chunks;
	const BlockRegistry&  blocks;
	ClientChunkWorldView(ChunkSource& c, const BlockRegistry& b) : chunks(c), blocks(b) {}
	bool isSolid(glm::ivec3 p) const override {
		return blocks.get(chunks.getBlock(p.x, p.y, p.z)).solid;
	}
};

// Walk: cancel if unreachable. Build (long-press): enter experimental builder mode
// (Phase 1 jump-climb; Phase 2+ stack blocks to bridge gaps).
enum class CommandKind { Walk, Build };

class RtsExecutor {
public:
	// Disable to make Build fall back to Walk. Phase 1 = jump-climb only.
	static constexpr bool kExperimentalPathBuilder = true;
	struct BuilderConfig {
		int   maxClimbBlocks   = 8;
		int   maxPlacedBlocks  = 50;   // Phase 2+
		float maxDurationSec   = 60.f; // Phase 2+
		int   stuckTicksThresh = 6;
	} m_builderCfg;

	// Sync: formation slots install + naive steering. Async detached Dijkstra worker;
	// poll() swaps in the flow field when ready.
	void planGroup(const std::vector<EntityId>& entityIds,
	               const std::vector<glm::ivec3>& starts,
	               glm::ivec3 goal,
	               ChunkSource& chunks, const BlockRegistry& blocks,
	               CommandKind kind = CommandKind::Walk) {
		if (entityIds.empty()) return;
		using clk = std::chrono::steady_clock;
		auto t0 = clk::now();

		ClientChunkWorldView view(chunks, blocks);
		auto slots = buildFormationGoals(goal, (int)entityIds.size(), view);
		auto t1 = clk::now();

		m_formation.clear();
		m_arrived.clear();
		for (size_t i = 0; i < entityIds.size(); i++) {
			m_formation[entityIds[i]] = slots[i];
			m_arrived[entityIds[i]]   = false;
		}
		m_flow.reset();   // naive mode until async lands
		m_kind = kind;
		m_builderMode = false;
		m_builder.clear();

		// packaged_task + detached thread: ~future() must not block on new commands
		// mid-plan (std::async(async) would).
		auto state = std::make_shared<PendingState>();
		state->goal        = goal;
		state->starts      = starts;
		state->entityCount = entityIds.size();
		state->t0          = t0;
		state->setupMs     = std::chrono::duration<double, std::milli>(t1 - t0).count();
		state->kind        = kind;

		std::packaged_task<FlowField()> task(
			[&chunks, &blocks, goal, starts]() {
				ClientChunkWorldView v(chunks, blocks);
				GridPlanner planner(v);
				return planner.planFlowField(goal, starts);
			});
		state->future = task.get_future();
		std::thread(std::move(task)).detach();

		m_pending = std::move(state);

		printf("[RTS-CLIENT] plan-kickoff N=%zu goal=(%d,%d,%d) kind=%s "
		       "setup=%.2fms (flow field running async)\n",
		       entityIds.size(), goal.x, goal.y, goal.z,
		       kind == CommandKind::Build ? "BUILD" : "walk",
		       m_pending->setupMs);
	}

	CommandKind currentKind() const { return m_kind; }
	bool        inBuilderMode() const { return m_builderMode; }

	// Called per tick. Installs field when async ready; cancel or enter builder on unreachable.
	void poll() {
		if (!m_pending) return;
		if (m_pending->future.wait_for(std::chrono::milliseconds(0))
		    != std::future_status::ready) return;

		FlowField field = m_pending->future.get();
		auto t2 = std::chrono::steady_clock::now();
		double totalMs = std::chrono::duration<double, std::milli>(
			t2 - m_pending->t0).count();

		int reachable = 0;
		for (auto& s : m_pending->starts)
			if (field.step.count(s)) reachable++;

		if (reachable == 0) {
			bool enterBuilder = kExperimentalPathBuilder
			                    && m_pending->kind == CommandKind::Build;
			printf("[RTS-CLIENT] NO PATH to (%d,%d,%d) — %zu units unreachable "
			       "(field=%zu cells, async=%.2fms) → %s\n",
			       m_pending->goal.x, m_pending->goal.y, m_pending->goal.z,
			       m_pending->entityCount, field.size(), totalMs,
			       enterBuilder ? "BUILDER MODE" : "cancel");
			if (enterBuilder) {
				// m_flow stays null (naive steering); builder stuck-analysis fires
				// when a unit can't progress to its slot.
				m_builderMode = true;
				m_pending.reset();
				return;
			}
			cancelAll();
			return;
		}

		m_flow = std::make_shared<FlowField>(std::move(field));
		printf("[RTS-CLIENT] plan-ready N=%zu goal=(%d,%d,%d) "
		       "reach=%d/%zu field=%zu cells | async-total=%.2fms\n",
		       m_pending->entityCount, m_pending->goal.x, m_pending->goal.y,
		       m_pending->goal.z, reachable, m_pending->entityCount,
		       m_flow->size(), totalMs);

		m_pending.reset();
	}

	// nullopt = no command / arrived. Naive or near-slot → slotC; else flow-field next
	// cell, or goal if off-field.
	std::optional<glm::vec3> steerTargetFor(EntityId eid, glm::vec3 pos) {
		auto itSlot = m_formation.find(eid);
		if (itSlot == m_formation.end()) return std::nullopt;
		if (m_arrived[eid]) return std::nullopt;

		glm::ivec3 slot = itSlot->second;
		glm::vec3  slotC{slot.x + 0.5f, (float)slot.y, slot.z + 0.5f};

		float dSlot = std::sqrt(
			(pos.x - slotC.x) * (pos.x - slotC.x) +
			(pos.z - slotC.z) * (pos.z - slotC.z));
		if (dSlot < kArriveRadius) {
			m_arrived[eid] = true;
			return std::nullopt;
		}

		if (!m_flow) return slotC;   // naive until async plan lands

		if (dSlot < kFanOutRadius) {
			return slotC;
		}

		glm::ivec3 cell{
			(int)std::floor(pos.x),
			(int)std::floor(pos.y),
			(int)std::floor(pos.z)};
		auto it = m_flow->step.find(cell);
		if (it == m_flow->step.end()) {
			// Off-field — head to goal; field catches the unit when it re-enters.
			glm::ivec3 g = m_flow->goal;
			return glm::vec3{g.x + 0.5f, (float)g.y, g.z + 0.5f};
		}
		glm::ivec3 next = it->second.next;
		return glm::vec3{next.x + 0.5f, (float)next.y, next.z + 0.5f};
	}

	// Emit Move for every commanded entity except excludedId (possessed player).
	// Builder mode also injects jumps to climb 1-block ledges.
	void driveRemote(ServerInterface& server, EntityId excludedId,
	                 float walkSpeedFallback = 2.0f,
	                 float jumpVelocity      = 8.3f) {
		poll();

		ClientChunkWorldView view(server.chunks(), server.blockRegistry());
		std::vector<EntityId> done;
		for (auto& [eid, slot] : m_formation) {
			if (eid == excludedId) continue;
			Entity* e = server.getEntity(eid);
			if (!e) { done.push_back(eid); continue; }
			auto steer = steerTargetFor(eid, e->position);
			if (!steer) { done.push_back(eid); continue; }

			float walkSpeed = e->def().walk_speed;
			if (walkSpeed <= 0) walkSpeed = walkSpeedFallback;

			glm::vec3 d = *steer - e->position;
			d.y = 0;
			float len = std::sqrt(d.x * d.x + d.z * d.z);
			if (len < 0.001f) continue;
			d /= len;

			bool wantsJump = false;
			if (m_builderMode) {
				wantsJump = builderTickFor(eid, e->position, d, view);
			}

			ActionProposal move;
			move.type         = ActionProposal::Move;
			move.actorId      = eid;
			move.desiredVel   = {d.x * walkSpeed, 0, d.z * walkSpeed};
			move.hasClientPos = false;
			move.lookPitch    = e->lookPitch;
			move.lookYaw      = e->lookYaw;
			move.jump         = wantsJump;
			move.jumpVelocity = jumpVelocity;
			server.sendAction(move);
		}
		for (auto eid : done) cancel(eid);
	}

	void cancel(EntityId eid) {
		m_formation.erase(eid);
		m_arrived.erase(eid);
		m_builder.erase(eid);
		if (m_formation.empty()) {
			m_flow.reset();
			m_pending.reset();    // detached thread keeps running; result discarded
			m_builderMode = false;
		}
	}

	void cancelAll() {
		m_formation.clear();
		m_arrived.clear();
		m_builder.clear();
		m_flow.reset();
		m_pending.reset();
		m_builderMode = false;
	}

	bool has(EntityId eid) const { return m_formation.count(eid) > 0; }

	bool isPlanning() const { return (bool)m_pending; }

	// F3 debug viz: trace the flow field forward as a polyline from `from`.
	std::vector<glm::vec3> traceFlow(glm::ivec3 from, int maxSteps = 64) const {
		std::vector<glm::vec3> out;
		if (!m_flow) return out;
		glm::ivec3 cur = from;
		for (int i = 0; i < maxSteps; i++) {
			auto it = m_flow->step.find(cur);
			if (it == m_flow->step.end()) break;
			glm::ivec3 nx = it->second.next;
			out.push_back({nx.x + 0.5f, (float)nx.y, nx.z + 0.5f});
			if (nx == cur) break;   // goal cell points to itself
			cur = nx;
		}
		return out;
	}

	const FlowField* field() const { return m_flow.get(); }
	std::optional<glm::ivec3> formationSlot(EntityId eid) const {
		auto it = m_formation.find(eid);
		if (it == m_formation.end()) return std::nullopt;
		return it->second;
	}

private:
	static constexpr float kFanOutRadius = 4.0f;   // bypass field within this
	static constexpr float kArriveRadius = 0.9f;   // slightly > collision box

	struct PendingState {
		std::future<FlowField>                    future;
		std::vector<glm::ivec3>                   starts;
		glm::ivec3                                goal{};
		size_t                                    entityCount = 0;
		std::chrono::steady_clock::time_point     t0;
		double                                    setupMs = 0.0;
		CommandKind                               kind = CommandKind::Walk;
	};

	std::shared_ptr<FlowField>                m_flow;
	std::shared_ptr<PendingState>             m_pending;
	std::unordered_map<EntityId, glm::ivec3>  m_formation;
	std::unordered_map<EntityId, bool>        m_arrived;

	CommandKind                               m_kind        = CommandKind::Walk;
	bool                                      m_builderMode = false;

	struct BuilderUnitState {
		glm::ivec3 lastCell{INT_MIN, INT_MIN, INT_MIN};
		int        stuckTicks = 0;
		bool       phase2Logged = false;
	};
	std::unordered_map<EntityId, BuilderUnitState> m_builder;

	// Phase 1 stuck analysis: returns true to request a jump (climb 1-block ledge).
	bool builderTickFor(EntityId eid, glm::vec3 pos, glm::vec3 dirXZ,
	                    const WorldView& view) {
		auto& bs = m_builder[eid];

		glm::ivec3 cell{
			(int)std::floor(pos.x),
			(int)std::floor(pos.y),
			(int)std::floor(pos.z)};
		if (cell == bs.lastCell) bs.stuckTicks++;
		else { bs.stuckTicks = 0; bs.lastCell = cell; }

		glm::ivec3 step{0, 0, 0};
		if (std::abs(dirXZ.x) >= std::abs(dirXZ.z))
			step.x = dirXZ.x > 0 ? 1 : -1;
		else
			step.z = dirXZ.z > 0 ? 1 : -1;
		glm::ivec3 ahead       = cell + step;
		glm::ivec3 aheadHead   = ahead + glm::ivec3(0, 1, 0);
		glm::ivec3 aheadHead2  = ahead + glm::ivec3(0, 2, 0);

		bool ahead_solid      = view.isSolid(ahead);
		bool aheadHead_solid  = view.isSolid(aheadHead);
		bool aheadHead2_solid = view.isSolid(aheadHead2);

		// Jumpable 1-block ledge: foot solid, head+1 and head+2 clear.
		bool jumpable = ahead_solid && !aheadHead_solid && !aheadHead2_solid;

		bool blocked = ahead_solid || aheadHead_solid;
		if (!blocked) return false;

		if (jumpable) {
			if (bs.stuckTicks % 10 == 1) {   // ~1 Hz log throttle
				printf("[RTS-BUILDER] eid=%llu jump at (%d,%d,%d) → (%d,%d,%d)\n",
				       (unsigned long long)eid, cell.x, cell.y, cell.z,
				       ahead.x, ahead.y, ahead.z);
			}
			return true;
		}

		// Phase 2+ (stack blocks to bridge): log once per unit until implemented.
		if (bs.stuckTicks >= m_builderCfg.stuckTicksThresh && !bs.phase2Logged) {
			printf("[RTS-BUILDER] eid=%llu STUCK at (%d,%d,%d) — "
			       "Phase 2 (gather+stack) not yet implemented\n",
			       (unsigned long long)eid, cell.x, cell.y, cell.z);
			bs.phase2Logged = true;
		}
		return false;
	}

	// cols×rows grid, each cell snapped to nearest standable Y (±4) so slopes aren't unreachable.
	static std::vector<glm::ivec3> buildFormationGoals(
			glm::ivec3 clicked, int n, const WorldView& view) {
		std::vector<glm::ivec3> out;
		out.reserve(n);
		if (n == 1) { out.push_back(clicked); return out; }

		int cols = (int)std::ceil(std::sqrt((float)n));
		const int spacing = 2;
		int offX = (cols - 1) * spacing / 2;
		int rows = (n + cols - 1) / cols;
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

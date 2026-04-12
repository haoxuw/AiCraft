#pragma once

/**
 * DecideWorker — runs Python decide() on a background thread.
 *
 * TODO(decide-loop) Step 3: this file is currently a skeleton with
 * pseudocode in method bodies. It compiles but is not yet constructed
 * from AgentClient. Wire-up happens in Step 3 after Step 2 lands the
 * shared_mutex on ChunkStore (so blockQuery is safe off-thread).
 *
 * Contract:
 *   - Main thread snapshots entity + nearby state, pushes a DecideRequest.
 *   - Worker pops, acquires the Python GIL, calls PythonBridge::callDecide,
 *     pushes a DecideResult back.
 *   - Main thread drains DecideResults once per frame and installs plans.
 *   - Generation counter on DecideRequest filters stale results after
 *     interrupts supersede in-flight decides.
 *
 * Shared state safety:
 *   - entitySnapshot / nearbySnapshot / lastOutcome: owned by the request,
 *     immutable after push → no locking needed.
 *   - blockQuery / scanBlocks callbacks invoked from worker thread MUST
 *     take the ChunkStore shared_lock (Step 2).
 */

#include "shared/types.h"
#include "shared/entity.h"
#include "server/behavior.h"            // Plan, PlanStep
#include "server/python_bridge.h"       // BehaviorHandle, NearbyEntity
#include "agent/decision_queue.h"       // LastOutcome
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace modcraft {

// ── Request (main → worker) ─────────────────────────────────────────────

// Snapshot of the self-entity at decide time. Worker reads these as
// immutable values; main thread may mutate the real Entity concurrently.
struct EntitySnapshot {
	EntityId    id       = ENTITY_NONE;
	std::string typeId;
	glm::vec3   position = {0, 0, 0};
	glm::vec3   velocity = {0, 0, 0};
	float       yaw      = 0;
	int         hp       = 0;
	bool        onGround = false;
	// TODO(decide-loop): inventory copy (item id → count), extension props.
};

struct DecideRequest {
	EntityId                     eid        = ENTITY_NONE;
	uint32_t                     generation = 0;
	BehaviorHandle               handle     = -1;
	EntitySnapshot               self;
	std::vector<NearbyEntity>    nearby;
	float                        worldTime  = 0;
	float                        dt         = 0;
	LastOutcome                  lastOutcome;
};

// ── Result (worker → main) ──────────────────────────────────────────────

struct DecideResult {
	EntityId     eid        = ENTITY_NONE;
	uint32_t     generation = 0;
	Plan         plan;
	std::string  goalText;
	std::string  error;
};

// ── Worker ──────────────────────────────────────────────────────────────

class DecideWorker {
public:
	DecideWorker() = default;
	~DecideWorker() { stop(); }

	// Pseudocode:
	//   start():
	//       m_running = true
	//       m_thread  = std::thread([this]{ run(); })
	void start() {
		// TODO(decide-loop) Step 3: spawn worker thread.
	}

	// Pseudocode:
	//   stop():
	//       m_running = false
	//       m_cv.notify_all()              # wake popBlocking
	//       if m_thread.joinable(): m_thread.join()
	void stop() {
		// TODO(decide-loop) Step 3: signal + join.
	}

	// Main thread → worker.  Pseudocode:
	//   push(req):
	//       { lock m_reqMutex; m_requests.push_back(req); }
	//       m_cv.notify_one()
	void push(DecideRequest /*req*/) {
		// TODO(decide-loop) Step 3: enqueue request and wake worker.
	}

	// Worker → main (non-blocking).  Pseudocode:
	//   tryPop() -> optional<DecideResult>:
	//       lock m_resMutex
	//       if m_results.empty(): return {}
	//       pop front, return it
	bool tryPop(DecideResult& /*out*/) {
		// TODO(decide-loop) Step 3: drain one result; return false if empty.
		return false;
	}

private:
	// Worker-thread main loop.  Pseudocode:
	//   run():
	//       while m_running:
	//           req = popRequestBlocking()            # waits on m_cv
	//           if !m_running: break
	//           { py::gil_scoped_acquire gil;
	//             plan, goal, err = pythonBridge().callDecide(
	//                 req.handle, req.self, req.nearby, req.dt,
	//                 req.worldTime, req.lastOutcome,
	//                 /*blockQueryFn*/ chunkReader,   # shared_lock-ed
	//                 /*scanBlocksFn*/ chunkScanner); # shared_lock-ed
	//           }
	//           { lock m_resMutex;
	//             m_results.push_back({req.eid, req.generation, plan, goal, err});
	//           }
	void run() {
		// TODO(decide-loop) Step 3: implement per pseudocode above.
	}

	std::thread               m_thread;
	std::atomic<bool>         m_running{false};
	std::mutex                m_reqMutex;
	std::condition_variable   m_cv;
	std::deque<DecideRequest> m_requests;
	std::mutex                m_resMutex;
	std::deque<DecideResult>  m_results;
};

} // namespace modcraft

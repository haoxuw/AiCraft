#pragma once

// Runs Python decide() on a background thread. Generation counter filters stale
// results after interrupts. Callbacks read ChunkStore unlocked (visual-only race ok).

#include "logic/types.h"
#include "logic/entity.h"
#include "agent/behavior.h"             // Plan, PlanStep
#include "python/python_bridge.h"       // BehaviorHandle, NearbyEntity, EntitySnapshot
#include "agent/outcome.h"              // LastOutcome, StepOutcome
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace civcraft {

struct DecideRequest {
	// Decide = normal plan-completion / first-time discovery path.
	// React  = engine-detected signal (threat_nearby, …) → Behavior.react().
	enum class Kind { Decide, React };
	Kind                            kind       = Kind::Decide;
	std::string                     signalKind;                  // React only
	std::vector<std::pair<std::string, std::string>> signalPayload;

	EntityId                        eid        = ENTITY_NONE;
	uint32_t                        generation = 0;
	BehaviorHandle                  handle     = -1;
	EntitySnapshot                  self;
	std::vector<NearbyEntity>       nearby;
	float                           worldTime  = 0;
	float                           dt         = 0;
	LastOutcome                     lastOutcome;
	PythonBridge::BlockQueryFn        blockQuery;
	PythonBridge::AppearanceQueryFn   appearanceQuery;
	PythonBridge::ScanBlocksFn        scanBlocks;
	PythonBridge::ScanEntitiesFn      scanEntities;
	PythonBridge::ScanAnnotationsFn   scanAnnotations;
};

struct DecideResult {
	EntityId     eid        = ENTITY_NONE;
	uint32_t     generation = 0;
	// React returning None (ignore the signal) → fromReact=true, reactNoOp=true.
	// The orchestrator uses this to skip installing an empty plan.
	bool         fromReact  = false;
	bool         reactNoOp  = false;
	Plan         plan;
	std::string  goalText;
	std::string  error;
};

class DecideWorker {
public:
	DecideWorker() = default;
	~DecideWorker() { stop(); }

	void start() {
		if (m_running.exchange(true)) return;
		m_thread = std::thread([this]{ run(); });
	}

	void stop() {
		if (!m_running.exchange(false)) return;
		m_cv.notify_all();
		if (m_thread.joinable()) m_thread.join();
	}

	void push(DecideRequest req) {
		{
			std::lock_guard<std::mutex> lk(m_reqMutex);
			m_requests.push_back(std::move(req));
		}
		m_cv.notify_one();
	}

	bool tryPop(DecideResult& out) {
		std::lock_guard<std::mutex> lk(m_resMutex);
		if (m_results.empty()) return false;
		out = std::move(m_results.front());
		m_results.pop_front();
		return true;
	}

	size_t pendingRequests() const {
		std::lock_guard<std::mutex> lk(m_reqMutex);
		return m_requests.size();
	}

private:
	void run() {
		while (true) {
			DecideRequest req;
			{
				std::unique_lock<std::mutex> lk(m_reqMutex);
				m_cv.wait(lk, [this]{
					return !m_requests.empty() || !m_running.load();
				});
				if (!m_running.load() && m_requests.empty()) return;
				req = std::move(m_requests.front());
				m_requests.pop_front();
			}

			DecideResult res;
			res.eid = req.eid;
			res.generation = req.generation;

			if (req.kind == DecideRequest::Kind::React) {
				res.fromReact = true;
				bool reacted = pythonBridge().callReact(
					req.handle, req.self, req.nearby, req.dt,
					req.worldTime, req.signalKind, req.signalPayload,
					res.plan, res.goalText, res.error,
					std::move(req.blockQuery), std::move(req.scanBlocks),
					std::move(req.scanEntities),
					std::move(req.scanAnnotations),
					std::move(req.appearanceQuery));
				if (!reacted && res.error.empty()) res.reactNoOp = true;
			} else {
				// Success→"success", Failed→"failed", else "none". See python/local_world.py.
				std::string outcomeStr = "none";
				if (req.lastOutcome.outcome == StepOutcome::Success) outcomeStr = "success";
				else if (req.lastOutcome.outcome == StepOutcome::Failed) outcomeStr = "failed";

				res.plan = pythonBridge().callDecide(
					req.handle, req.self, req.nearby, req.dt,
					req.worldTime, res.goalText, res.error,
					std::move(req.blockQuery), std::move(req.scanBlocks),
					std::move(req.scanEntities),
					std::move(req.scanAnnotations),
					outcomeStr, req.lastOutcome.goalText, req.lastOutcome.reason,
					req.lastOutcome.execState,
					req.lastOutcome.failStreak,
					std::move(req.appearanceQuery));
			}

			{
				std::lock_guard<std::mutex> lk(m_resMutex);
				m_results.push_back(std::move(res));
			}
		}
	}

	std::thread               m_thread;
	std::atomic<bool>         m_running{false};
	mutable std::mutex        m_reqMutex;
	std::condition_variable   m_cv;
	std::deque<DecideRequest> m_requests;
	mutable std::mutex        m_resMutex;
	std::deque<DecideResult>  m_results;
};

} // namespace civcraft

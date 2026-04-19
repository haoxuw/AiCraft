#pragma once

// Runs Python decide() on a background thread. Generation counter filters stale
// results after interrupts. Callbacks read ChunkStore unlocked (visual-only race ok).

#include "logic/types.h"
#include "logic/entity.h"
#include "server/behavior.h"            // Plan, PlanStep
#include "server/python_bridge.h"       // BehaviorHandle, NearbyEntity, EntitySnapshot
#include "agent/decision_queue.h"       // LastOutcome
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace civcraft {

struct DecideRequest {
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
				std::move(req.appearanceQuery));

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

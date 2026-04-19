#pragma once

// Event-driven decide() trigger queue.
//
// Invariant held by callers: a decide is requested only when the Agent's
// current Plan has ended (Success/Failed) or been interrupted. There is no
// time-based gate in the queue itself — the plan-completion contract in
// AgentClient is what keeps decide rate bounded.
//
// Scheduling:
//   Two FIFO lanes — priority (HP drop, target_gone, player override,
//   world events, decide_error) and normal (plan completion, first-time
//   discovery). Priority drains before normal within each tick.
//
// Latest-wins per lane:
//   Re-requesting an eid already in its lane updates the cached outcome in
//   place WITHOUT moving it to the back. This is the fairness guarantee:
//   older waiters are never starved by newer refreshes.
//
// O(1) on every op via std::list + index map.

#include "logic/types.h"
#include <string>
#include <vector>
#include <utility>
#include <list>
#include <unordered_map>

namespace civcraft {

// Handed to next decide() so Python can branch on outcome.
enum class StepOutcome {
	InProgress,   // evaluator sentinel, never enqueued
	Success,
	Failed,
};

struct PlanStep;

struct LastOutcome {
	StepOutcome  outcome     = StepOutcome::Success;
	std::string  goalText;
	int          stepTypeRaw = 0;            // PlanStep::Type as int to avoid include cycle
	std::string  reason;                     // "stuck", "target_gone", "interrupt:...", …
};

class DecisionQueue {
public:
	struct Request {
		EntityId    eid         = ENTITY_NONE;
		LastOutcome outcome;
		float       requestTime = 0.0f;
		bool        priority    = false;

		// React-specific fields (ignored when isReact == false). A react
		// request triggers Python's Behavior.react(signal) instead of
		// decide() and replaces the current plan on a non-None return.
		bool        isReact     = false;
		std::string signalKind;
		std::vector<std::pair<std::string, std::string>> signalPayload;
	};

	// Plan completion / first-time discovery. No-op if already in priority lane.
	void requestNormal(EntityId eid, LastOutcome o, float now) {
		if (m_priorityIdx.count(eid)) return;       // priority covers us
		auto it = m_normalIdx.find(eid);
		if (it != m_normalIdx.end()) {
			it->second->outcome = std::move(o);     // latest-wins, keep position
			return;
		}
		Request req; req.eid = eid; req.outcome = std::move(o);
		req.requestTime = now; req.priority = false;
		m_normal.push_back(std::move(req));
		m_normalIdx[eid] = std::prev(m_normal.end());
	}

	// HP drop, target_gone, player override, world events, decide_error.
	// Upgrades from normal lane if present.
	void requestPriority(EntityId eid, LastOutcome o, float now) {
		auto nit = m_normalIdx.find(eid);
		if (nit != m_normalIdx.end()) {
			m_normal.erase(nit->second);
			m_normalIdx.erase(nit);
		}
		auto it = m_priorityIdx.find(eid);
		if (it != m_priorityIdx.end()) {
			it->second->outcome = std::move(o);     // latest-wins, keep position
			it->second->isReact = false;            // demote any pending react
			return;
		}
		Request req; req.eid = eid; req.outcome = std::move(o);
		req.requestTime = now; req.priority = true;
		m_priority.push_back(std::move(req));
		m_priorityIdx[eid] = std::prev(m_priority.end());
	}

	// Engine detected a signal (threat_nearby, …). React is priority lane;
	// replaces any pending decide request for this eid so we respond to
	// "right now" events instead of the plan-completion backlog. Latest-wins
	// per eid within the priority lane.
	void requestReact(EntityId eid, std::string signalKind,
	                  std::vector<std::pair<std::string, std::string>> payload,
	                  float now) {
		auto nit = m_normalIdx.find(eid);
		if (nit != m_normalIdx.end()) {
			m_normal.erase(nit->second);
			m_normalIdx.erase(nit);
		}
		Request req;
		req.eid            = eid;
		req.priority       = true;
		req.isReact        = true;
		req.signalKind     = std::move(signalKind);
		req.signalPayload  = std::move(payload);
		req.requestTime    = now;
		req.outcome.outcome = StepOutcome::Success;
		req.outcome.reason  = "signal:" + req.signalKind;

		auto pit = m_priorityIdx.find(eid);
		if (pit != m_priorityIdx.end()) {
			// Replace in place to preserve FIFO position.
			*pit->second = std::move(req);
			return;
		}
		m_priority.push_back(std::move(req));
		m_priorityIdx[eid] = std::prev(m_priority.end());
	}

	// Budget overrun: put the request back at the FRONT of its lane so it
	// keeps its turn next tick (it already won the draw; only the clock lost).
	void reinsertFront(Request req) {
		EntityId eid = req.eid;
		if (req.priority) {
			if (m_priorityIdx.count(eid)) return;   // already re-queued
			m_priority.push_front(std::move(req));
			m_priorityIdx[eid] = m_priority.begin();
		} else {
			if (m_normalIdx.count(eid)) return;
			m_normal.push_front(std::move(req));
			m_normalIdx[eid] = m_normal.begin();
		}
	}

	// Pops the caller's slice; caller enforces wall-clock budget and calls
	// reinsertFront(...) on any it didn't get to. Priority drains first.
	std::vector<Request> drain(int maxN) {
		std::vector<Request> out;
		out.reserve(std::min((size_t)maxN, m_priority.size() + m_normal.size()));
		while (!m_priority.empty() && (int)out.size() < maxN) {
			out.push_back(std::move(m_priority.front()));
			m_priorityIdx.erase(out.back().eid);
			m_priority.pop_front();
		}
		while (!m_normal.empty() && (int)out.size() < maxN) {
			out.push_back(std::move(m_normal.front()));
			m_normalIdx.erase(out.back().eid);
			m_normal.pop_front();
		}
		return out;
	}

	bool hasPending(EntityId eid) const {
		return m_normalIdx.count(eid) || m_priorityIdx.count(eid);
	}

	void cancel(EntityId eid) {
		auto pit = m_priorityIdx.find(eid);
		if (pit != m_priorityIdx.end()) {
			m_priority.erase(pit->second);
			m_priorityIdx.erase(pit);
		}
		auto nit = m_normalIdx.find(eid);
		if (nit != m_normalIdx.end()) {
			m_normal.erase(nit->second);
			m_normalIdx.erase(nit);
		}
	}

	size_t pendingCount() const { return m_normal.size() + m_priority.size(); }
	size_t priorityCount() const { return m_priority.size(); }
	size_t normalCount()  const { return m_normal.size(); }

	// Oldest-waiter wait time in seconds. 0 if empty.
	// Used to surface starvation via the agent diagnostic log.
	float oldestWaitSec(float now) const {
		float oldest = now;
		if (!m_priority.empty()) oldest = std::min(oldest, m_priority.front().requestTime);
		if (!m_normal.empty())   oldest = std::min(oldest, m_normal.front().requestTime);
		return now - oldest;
	}

	// Eid of the oldest waiter. ENTITY_NONE if empty.
	EntityId oldestWaiter() const {
		float t = 1e30f;
		EntityId eid = ENTITY_NONE;
		if (!m_priority.empty() && m_priority.front().requestTime < t) {
			t = m_priority.front().requestTime; eid = m_priority.front().eid;
		}
		if (!m_normal.empty() && m_normal.front().requestTime < t) {
			eid = m_normal.front().eid;
		}
		return eid;
	}

private:
	std::list<Request> m_priority;
	std::list<Request> m_normal;
	std::unordered_map<EntityId, std::list<Request>::iterator> m_priorityIdx;
	std::unordered_map<EntityId, std::list<Request>::iterator> m_normalIdx;
};

} // namespace civcraft

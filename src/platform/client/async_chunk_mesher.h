#pragma once

// AsyncChunkMesher: runs ChunkMesher::buildMeshFromSnapshot on a worker pool so
// the main thread never spends frame time inside the meshing loop. Main thread
// responsibilities (owner): capture the 18³ padded snapshot of the live world
// under whatever lock ChunkSource expects, then enqueue{cp, snapshot}. Workers
// dequeue and build verts. Main thread drains finished results each frame and
// calls the Vulkan create/update mesh calls (RHI isn't thread-safe).

#include "client/chunk_mesher.h"
#include "logic/block_registry.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace civcraft {

class AsyncChunkMesher {
public:
	struct Result {
		ChunkPos cp;
		std::vector<ChunkVertex> opaque;
		std::vector<ChunkVertex> transparent;
	};

	AsyncChunkMesher(const BlockRegistry& reg, int workerCount)
		: m_reg(&reg)
	{
		int n = std::max(1, workerCount);
		m_workers.reserve(n);
		for (int i = 0; i < n; i++) {
			m_workers.emplace_back([this]{ run(); });
		}
	}

	~AsyncChunkMesher() {
		{
			std::lock_guard<std::mutex> lk(m_mu);
			m_stop = true;
		}
		m_cv.notify_all();
		for (auto& w : m_workers) if (w.joinable()) w.join();
	}

	AsyncChunkMesher(const AsyncChunkMesher&) = delete;
	AsyncChunkMesher& operator=(const AsyncChunkMesher&) = delete;

	// Snapshot is passed by unique_ptr so we move a pointer, not 17KB of data.
	void enqueue(ChunkPos cp, std::unique_ptr<ChunkMesher::PaddedSnapshot> snap) {
		{
			std::lock_guard<std::mutex> lk(m_mu);
			m_jobs.push_back({cp, std::move(snap)});
		}
		m_cv.notify_one();
	}

	// Main-thread drain. Invokes cb(Result&&) for up to `maxItems` completed
	// jobs (0 = unbounded). Anything left over stays in the queue for the
	// next drain — prevents a burst of worker completions from turning into
	// a burst of Vulkan uploads on a single frame.
	template <typename F>
	size_t drain(F&& cb, size_t maxItems = 0) {
		std::vector<Result> batch;
		{
			std::lock_guard<std::mutex> lk(m_outMu);
			if (maxItems == 0 || m_results.size() <= maxItems) {
				batch.swap(m_results);
			} else {
				batch.reserve(maxItems);
				auto first = m_results.begin();
				auto last  = first + maxItems;
				std::move(first, last, std::back_inserter(batch));
				m_results.erase(first, last);
			}
		}
		for (auto& r : batch) cb(std::move(r));
		return batch.size();
	}

	size_t pendingResults() {
		std::lock_guard<std::mutex> lk(m_outMu);
		return m_results.size();
	}

private:
	struct Job {
		ChunkPos cp;
		std::unique_ptr<ChunkMesher::PaddedSnapshot> snapshot;
	};

	void run() {
		while (true) {
			Job j;
			{
				std::unique_lock<std::mutex> lk(m_mu);
				m_cv.wait(lk, [this]{ return m_stop || !m_jobs.empty(); });
				if (m_stop && m_jobs.empty()) return;
				j = std::move(m_jobs.front());
				m_jobs.pop_front();
			}
			auto [opaque, transparent] = ChunkMesher::buildMeshFromSnapshot(
				*j.snapshot, j.cp, *m_reg);
			{
				std::lock_guard<std::mutex> lk(m_outMu);
				m_results.push_back({j.cp, std::move(opaque), std::move(transparent)});
			}
		}
	}

	const BlockRegistry* m_reg;
	std::vector<std::thread> m_workers;

	std::mutex m_mu;
	std::condition_variable m_cv;
	std::deque<Job> m_jobs;
	bool m_stop = false;

	std::mutex m_outMu;
	std::vector<Result> m_results;
};

} // namespace civcraft

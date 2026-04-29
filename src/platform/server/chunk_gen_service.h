#pragma once

// Async chunk gen + zstd worker pool. Sync HELLO used to block tick 5+s
// (2000 getChunk+zstd calls), starving heartbeats → clients disconnected.
// Workers pre-frame S_CHUNK/S_CHUNK_Z messages; main thread drains + routes.

#include "logic/types.h"
#include "net/net_protocol.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_set>
#include <vector>

namespace solarium {

class World;

class ChunkGenService {
public:
	struct Result {
		ClientId cid;
		ChunkPos pos;
		std::vector<uint8_t> message; // framed S_CHUNK/S_CHUNK_Z
	};

	// numWorkers == 0 → max(1, hardware_concurrency()-1). Env SOLARIUM_CHUNK_WORKERS overrides.
	ChunkGenService(World& world, int numWorkers = 0);
	~ChunkGenService();

	ChunkGenService(const ChunkGenService&) = delete;
	ChunkGenService& operator=(const ChunkGenService&) = delete;

	// `clientChunk` is the requesting client's current chunk position. Used
	// to score this job's priority — smaller squared distance pops first
	// — so chunks the player is standing in arrive before chunks they
	// might walk to. Distance is frozen at enqueue time; if the player
	// teleports, the existing queue drains in old order but new submits
	// reflect the new position.
	void submit(ClientId cid, ChunkPos pos, bool useZstd, ChunkPos clientChunk);

	// Invalidates pending + in-flight + undrained results for the client.
	void cancelClient(ClientId cid);

	std::vector<Result> drain();

	size_t pendingJobs() const;
	int workerCount() const { return (int)m_workers.size(); }

private:
	struct Job {
		ClientId cid;
		ChunkPos pos;
		bool useZstd;
		int64_t distSq;   // (cx-px)^2 + (cy-py)^2 + (cz-pz)^2 at enqueue time
	};

	// Min-heap on distSq (smallest distance pops first). priority_queue's
	// default comparator gives a max-heap, so we invert.
	struct JobByDistance {
		bool operator()(const Job& a, const Job& b) const {
			return a.distSq > b.distSq;
		}
	};

	void workerLoop();
	void buildMessage(const Chunk& chunk, ChunkPos pos, bool useZstd,
	                  std::vector<uint8_t>& out) const;

	World& m_world;
	std::vector<std::thread> m_workers;

	mutable std::mutex m_jobMu;
	std::condition_variable m_jobCv;
	std::priority_queue<Job, std::vector<Job>, JobByDistance> m_jobs;

	mutable std::mutex m_resultMu;
	std::vector<Result> m_results;

	mutable std::mutex m_cancelMu;
	std::unordered_set<ClientId> m_cancelled;

	std::atomic<bool> m_stop{false};
};

} // namespace solarium

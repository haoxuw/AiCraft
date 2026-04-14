#pragma once

/**
 * ChunkGenService — async chunk generation + compression worker pool.
 *
 * Problem solved: the HELLO handler used to call world.getChunk() + zstd
 * 2000+ times synchronously, blocking the server tick loop for 5+ seconds.
 * That starved heartbeat sends and forced clients to disconnect before
 * they'd finished loading.
 *
 * Design: N worker threads pull (ClientId, ChunkPos) jobs from a queue,
 * call world.getChunk() (already mutex-protected) → serialize → zstd,
 * and push a fully-framed network message onto a result queue. The main
 * thread drains the result queue each tick and routes messages into each
 * client's pendingChunks for the normal TCP send loop.
 *
 * Lifecycle: constructed at server startup, destructor joins all workers.
 * Jobs for a disconnected client can be cancelled via cancelClient(cid);
 * workers check the cancel set before doing expensive work.
 */

#include "shared/types.h"
#include "shared/net_protocol.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_set>
#include <vector>

namespace civcraft {

class World;

class ChunkGenService {
public:
	struct Result {
		ClientId cid;
		ChunkPos pos;
		std::vector<uint8_t> message; // full framed S_CHUNK/S_CHUNK_Z (header + payload)
	};

	// numWorkers == 0 → auto (max(1, hardware_concurrency() - 1)).
	// Env var CIVCRAFT_CHUNK_WORKERS overrides the constructor arg.
	ChunkGenService(World& world, int numWorkers = 0);
	~ChunkGenService();

	ChunkGenService(const ChunkGenService&) = delete;
	ChunkGenService& operator=(const ChunkGenService&) = delete;

	// Enqueue a job; returns immediately. useZstd selects S_CHUNK_Z.
	void submit(ClientId cid, ChunkPos pos, bool useZstd);

	// Invalidate all pending + in-flight jobs for a client. Results already
	// produced but not yet drained are discarded too.
	void cancelClient(ClientId cid);

	// Non-blocking: returns all results ready for routing. Called from the
	// server tick loop.
	std::vector<Result> drain();

	// Introspection for diagnostics.
	size_t pendingJobs() const;
	int workerCount() const { return (int)m_workers.size(); }

private:
	struct Job {
		ClientId cid;
		ChunkPos pos;
		bool useZstd;
	};

	void workerLoop();
	void buildMessage(const Chunk& chunk, ChunkPos pos, bool useZstd,
	                  std::vector<uint8_t>& out) const;

	World& m_world;
	std::vector<std::thread> m_workers;

	mutable std::mutex m_jobMu;
	std::condition_variable m_jobCv;
	std::queue<Job> m_jobs;

	mutable std::mutex m_resultMu;
	std::vector<Result> m_results;

	mutable std::mutex m_cancelMu;
	std::unordered_set<ClientId> m_cancelled;

	std::atomic<bool> m_stop{false};
};

} // namespace civcraft

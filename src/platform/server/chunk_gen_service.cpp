#include "server/chunk_gen_service.h"

#include "server/world.h"
#include "logic/chunk.h"
#include "net/net_protocol.h"

#include <zstd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace solarium {

ChunkGenService::ChunkGenService(World& world, int numWorkers) : m_world(world) {
	// Priority: env var > arg > hw_concurrency-1 (reserve one core for tick).
	int n = numWorkers;
	if (const char* env = std::getenv("SOLARIUM_CHUNK_WORKERS")) {
		int envN = std::atoi(env);
		if (envN > 0) n = envN;
	}
	if (n <= 0) {
		unsigned hw = std::thread::hardware_concurrency();
		n = (hw > 1) ? (int)(hw - 1) : 1;
	}
	n = std::max(1, std::min(n, 16));

	m_workers.reserve(n);
	for (int i = 0; i < n; i++)
		m_workers.emplace_back([this] { workerLoop(); });

	std::fprintf(stderr, "[ChunkGenService] Started with %d worker(s)\n", n);
}

ChunkGenService::~ChunkGenService() {
	m_stop.store(true);
	m_jobCv.notify_all();
	for (auto& t : m_workers)
		if (t.joinable()) t.join();
}

void ChunkGenService::submit(ClientId cid, ChunkPos pos, bool useZstd, ChunkPos clientChunk) {
	const int64_t dx = (int64_t)pos.x - clientChunk.x;
	const int64_t dy = (int64_t)pos.y - clientChunk.y;
	const int64_t dz = (int64_t)pos.z - clientChunk.z;
	const int64_t distSq = dx*dx + dy*dy + dz*dz;
	{
		std::lock_guard<std::mutex> lk(m_jobMu);
		m_jobs.push(Job{cid, pos, useZstd, distSq});
	}
	m_jobCv.notify_one();
}

void ChunkGenService::cancelClient(ClientId cid) {
	{
		std::lock_guard<std::mutex> lk(m_cancelMu);
		m_cancelled.insert(cid);
	}
	// Drop already-produced results so drain() stays clean.
	std::lock_guard<std::mutex> lk(m_resultMu);
	m_results.erase(
		std::remove_if(m_results.begin(), m_results.end(),
			[cid](const Result& r) { return r.cid == cid; }),
		m_results.end());
}

std::vector<ChunkGenService::Result> ChunkGenService::drain() {
	std::lock_guard<std::mutex> lk(m_resultMu);
	std::vector<Result> out;
	out.swap(m_results);
	return out;
}

size_t ChunkGenService::pendingJobs() const {
	std::lock_guard<std::mutex> lk(m_jobMu);
	return m_jobs.size();
}

void ChunkGenService::workerLoop() {
	while (true) {
		Job job;
		{
			std::unique_lock<std::mutex> lk(m_jobMu);
			m_jobCv.wait(lk, [this] { return m_stop.load() || !m_jobs.empty(); });
			if (m_stop.load() && m_jobs.empty()) return;
			job = m_jobs.top();   // priority_queue: smallest distSq first
			m_jobs.pop();
		}

		{
			std::lock_guard<std::mutex> lk(m_cancelMu);
			if (m_cancelled.count(job.cid)) continue;
		}

		// getChunk serializes on World::m_mutex; compression below runs parallel.
		Chunk* chunk = m_world.getChunk(job.pos);
		if (!chunk) continue;

		// Re-check cancel — client may have disconnected during worldgen.
		{
			std::lock_guard<std::mutex> lk(m_cancelMu);
			if (m_cancelled.count(job.cid)) continue;
		}

		// Default-fill chunks (AIR-Lite at cy>=0, DIRT-Lite below) aren't sent;
		// client falls back to defaultBlock(cy) on lookup miss. Empty `msg`
		// with the same Result.pos signals "treat as sent, no bytes" to the
		// drain path so the prep counter still advances.
		std::vector<uint8_t> msg;
		if (m_world.isInteresting(job.pos)) {
			buildMessage(*chunk, job.pos, job.useZstd, msg);
		}

		std::lock_guard<std::mutex> lk(m_resultMu);
		m_results.push_back(Result{job.cid, job.pos, std::move(msg)});
	}
}

void ChunkGenService::buildMessage(const Chunk& chunk, ChunkPos pos, bool useZstd,
                                   std::vector<uint8_t>& out) const {
	// Payload: [i32 cx][i32 cy][i32 cz][u8 zone][u32×CHUNK_VOLUME][u8×CHUNK_VOLUME appearance]
	// — matches ClientManager::queueChunk. Annotation tail (if any) is appended
	// by the caller, after the appearance block. Zone byte added in protocol v10.
	net::WriteBuffer cb;
	cb.writeI32(pos.x);
	cb.writeI32(pos.y);
	cb.writeI32(pos.z);
	cb.writeU8(static_cast<uint8_t>(chunk.zone()));
	for (int i = 0; i < CHUNK_VOLUME; i++)
		cb.writeU32(((uint32_t)chunk.getRawParam2(i) << 16) | chunk.getRaw(i));
	for (int i = 0; i < CHUNK_VOLUME; i++)
		cb.writeU8(chunk.getRawAppearance(i));

	std::vector<uint8_t> payload;
	net::MsgType msgType = net::S_CHUNK;

	if (useZstd) {
		size_t srcSize  = cb.size();
		size_t dstBound = ZSTD_compressBound(srcSize);
		payload.resize(dstBound);
		size_t compSize = ZSTD_compress(payload.data(), dstBound,
			cb.data().data(), srcSize,
			1 /* fastest */);
		payload.resize(compSize);
		msgType = net::S_CHUNK_Z;
	} else {
		payload.assign(cb.data().begin(), cb.data().end());
	}

	// 8-byte frame header so main thread can push bytes straight to socket.
	out.resize(8 + payload.size());
	net::MsgHeader hdr{ (uint32_t)msgType, (uint32_t)payload.size() };
	std::memcpy(out.data(), &hdr, 8);
	std::memcpy(out.data() + 8, payload.data(), payload.size());
}

} // namespace solarium

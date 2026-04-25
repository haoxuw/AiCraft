#include "client/game_vk.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>
#include <thread>

// CivCraft chunk + mesher — civcraft-ui-vk now stores its world in real
// 16³ Chunks and renders them via ChunkMesher feeding the RHI's
// chunk-mesh pipeline (validates the path that the full Phase 3
// renderer.cpp port will use).
#include "client/chunk_mesher.h"
#include "client/game_logger.h"
#include "client/model_loader.h"
#include "client/network_server.h"
#include "client/raycast.h"
#include "net/server_interface.h"
#include "logic/physics.h"
#include "logic/action.h"
#include "logic/material_values.h"
// AgentClient now lives inside the player client (server stopped spawning
// civcraft-agent processes). Pulling these in lets civcraft-ui-vk drive
// every NPC the server hands us via in-process Python decide_plan().
#include "agent/agent_client.h"
#include "debug/perf_registry.h"
#include "server/behavior_store.h"
#include "python/python_bridge.h"

#include <unordered_set>

namespace civcraft::vk {

Tuning kTune;


// ─────────────────────────────────────────────────────────────────────────
// Player entity access (unified — no dual state)
// ─────────────────────────────────────────────────────────────────────────

civcraft::Entity* Game::playerEntity() {
	return m_server ? m_server->getEntity(m_server->localPlayerId()) : nullptr;
}

glm::vec3 Game::playerForward() const {
	float yaw = glm::radians(m_cam.lookYaw);
	return glm::vec3(std::cos(yaw), 0, std::sin(yaw));
}

// ─────────────────────────────────────────────────────────────────────────
// Game — init / shutdown / state transitions
// ─────────────────────────────────────────────────────────────────────────

Game::Game() = default;
Game::~Game() = default;

bool Game::init(rhi::IRhi* rhi, GLFWwindow* window) {
	m_rhi = rhi;
	m_window = window;

	if (!m_server) {
		std::fprintf(stderr, "[vk-game] FATAL: no server — call setServer() before init()\n");
		return false;
	}

	// Wire up S_BLOCK / S_INVENTORY / S_REMOVE side-effect callbacks.
	setupServerCallbacks();

	if (!civcraft::pythonBridge().init("python"))
		std::printf("[vk-game] WARN: Python bridge init failed; NPCs won't decide()\n");
	m_behaviorStore = std::make_unique<civcraft::BehaviorStore>();
	m_behaviorStore->init("artifacts/behaviors");
	m_agentClient = std::make_unique<civcraft::AgentClient>(
		*m_server, *m_behaviorStore, m_agentCfg);
	if (auto* net = dynamic_cast<civcraft::NetworkServer*>(m_server)) {
		civcraft::AgentClient* ac = m_agentClient.get();
		net->setInterruptHandlers(
			[ac](civcraft::EntityId eid, const std::string& reason) {
				ac->onInterrupt(eid, reason);
			},
			[ac](const std::string& kind, const std::string& payload) {
				ac->onWorldEvent(kind, payload);
			});
	}
	std::printf("[vk-game] agent client up — Python decide_plan() will run in-process\n");

	// Async chunk mesher — worker pool sized to leave main/net/agent room.
	// Min 2 workers, max 6 (diminishing returns past that; queue rarely deep).
	{
		unsigned hw = std::thread::hardware_concurrency();
		int workers = hw > 0 ? std::max(2, (int)hw / 2 - 1) : 2;
		if (workers > 6) workers = 6;
		m_asyncMesher = std::make_unique<civcraft::AsyncChunkMesher>(
			m_server->blockRegistry(), workers);
		std::printf("[vk-game] async chunk mesher: %d worker%s\n",
			workers, workers == 1 ? "" : "s");
	}

	// Register file-triggered debug helpers (respawn, dig, camera, …).
	registerDebugTriggers();

	m_artifactRegistry.loadAll("artifacts");

	// Load Python-defined BoxModels (one .py per creature/item under
	// artifacts/models/) — sword has 14 parts, pig has legs, beaver has
	// a tail, etc.
	m_models = civcraft::model_loader::loadAllModels("artifacts");
	std::printf("[vk-game] Loaded %zu box models\n", m_models.size());

	// In-process menu plaza — 3 mascots wandering on a tiny client-only
	// world that the menu camera looks at. Init order is load-bearing:
	// pythonBridge + behaviorStore + artifactRegistry must already exist.
	m_menuPlaza = std::make_unique<MenuPlaza>();
	m_menuPlaza->init(m_artifactRegistry, *m_behaviorStore);

	// Spatial audio. loadSoundsFrom walks resources/sounds/*/*.ogg|wav and
	// registers each subdirectory as a sound-group name (step_grass,
	// dig_stone, door_open, hit_sword, …). Non-fatal on failure — the game
	// just runs silent.
	if (m_audio.init()) {
		m_audio.loadSoundsFrom("resources/sounds");
		m_audio.setMasterVolume(0.6f);
		std::printf("[vk-game] audio up — %zu sound groups registered\n",
			m_audio.groupNames().size());
	} else {
		std::printf("[vk-game] audio init failed — running silent\n");
	}

	enterMenu();

	// Debug hook for UI iteration: CIVCRAFT_BOOT_MENU=character lands directly
	// on CharacterSelect with the first registered playable preselected, so
	// screenshots of that layout can be taken without scripted keyboard input.
	if (const char* boot = std::getenv("CIVCRAFT_BOOT_MENU")) {
		std::string s = boot;
		if (s == "character" || s == "characterselect") {
			m_menuScreen = MenuScreen::CharacterSelect;
			for (auto* e : m_artifactRegistry.byCategory("living")) {
				auto it = e->fields.find("playable");
				if (it == e->fields.end()) continue;
				if (it->second != "True" && it->second != "true") continue;
				m_shell.previewId = e->id;
				break;
			}
		} else if (s == "multiplayer") {
			m_menuScreen = MenuScreen::Multiplayer;
		} else if (s == "handbook") {
			m_handbook.reset();
			m_menuScreen = MenuScreen::Handbook;
		} else if (s == "connecting") {
			m_menuScreen = MenuScreen::Connecting;
			m_connecting = true;
			m_connectStartTime = m_wallTime;
		}
	}

	// Optional client-only AI sidecars (LLM + STT + TTS). Non-fatal if
	// a given sidecar's binaries/models aren't installed.
	initAiSidecars();

	return true;
}

void Game::enqueueMeshBuild(civcraft::ChunkPos cp) {
	if (!m_asyncMesher) return;
	// Snapshot on the main thread — ChunkSource's live data is only safe to
	// read here. Workers then mesh from the immutable copy.
	auto snap = std::make_unique<civcraft::ChunkMesher::PaddedSnapshot>();
	if (!civcraft::ChunkMesher::snapshotPadded(m_server->chunks(), cp, *snap))
		return;  // center chunk not loaded yet — retry next frame
	// Mark tracked: Pass 1 dedupes on m_chunkMeshes.count(), Pass 2 dedupes
	// on m_inFlightMesh. kInvalidMesh placeholder gets overwritten when the
	// result lands via applyMeshResult.
	auto it = m_chunkMeshes.find(cp);
	if (it == m_chunkMeshes.end())
		m_chunkMeshes[cp] = rhi::IRhi::kInvalidMesh;
	m_inFlightMesh.insert(cp);
	m_asyncMesher->enqueue(cp, std::move(snap));
}

void Game::applyMeshResult(civcraft::AsyncChunkMesher::Result&& r) {
	m_inFlightMesh.erase(r.cp);
	auto& opaque = r.opaque;
	(void)r.transparent;  // no glass/water streams yet

	auto it = m_chunkMeshes.find(r.cp);

	// Empty mesh — nothing to draw. Keep a kInvalidMesh sentinel so Pass 1
	// doesn't re-enqueue every frame; a later dirty event overwrites it.
	if (opaque.empty()) {
		if (it != m_chunkMeshes.end() && it->second != rhi::IRhi::kInvalidMesh) {
			m_rhi->destroyMesh(it->second);
		}
		m_chunkMeshes[r.cp] = rhi::IRhi::kInvalidMesh;
		return;
	}

	// ChunkVertex is layout-compatible with the 13-float stream the VK chunk
	// pipeline expects (see chunk_mesher.h + rhi.h Chunk meshes).
	m_meshUploadScratch.clear();
	m_meshUploadScratch.reserve(opaque.size() * 13);
	for (const auto& v : opaque) {
		m_meshUploadScratch.push_back(v.position.x); m_meshUploadScratch.push_back(v.position.y); m_meshUploadScratch.push_back(v.position.z);
		m_meshUploadScratch.push_back(v.color.x);    m_meshUploadScratch.push_back(v.color.y);    m_meshUploadScratch.push_back(v.color.z);
		m_meshUploadScratch.push_back(v.normal.x);   m_meshUploadScratch.push_back(v.normal.y);   m_meshUploadScratch.push_back(v.normal.z);
		m_meshUploadScratch.push_back(v.ao);
		m_meshUploadScratch.push_back(v.shade);
		m_meshUploadScratch.push_back(v.alpha);
		m_meshUploadScratch.push_back(v.glow);
	}
	uint32_t vc = (uint32_t)opaque.size();
	if (it == m_chunkMeshes.end() || it->second == rhi::IRhi::kInvalidMesh) {
		auto h = m_rhi->createChunkMesh(m_meshUploadScratch.data(), vc);
		if (h != rhi::IRhi::kInvalidMesh) m_chunkMeshes[r.cp] = h;
	} else {
		m_rhi->updateChunkMesh(it->second, m_meshUploadScratch.data(), vc);
	}
}

void Game::drainAsyncMeshes() {
	if (!m_asyncMesher) return;
	// Cap uploads per frame so a burst of worker completions doesn't
	// translate into a burst of createChunkMesh/updateChunkMesh calls.
	constexpr size_t kMaxUploadsPerFrame = 8;
	m_asyncMesher->drain(
		[this](civcraft::AsyncChunkMesher::Result&& r) {
			// Sync remesh beat us to it — worker snapshot is pre-predict,
			// uploading would un-break the block.
			if (m_staleInflightMeshes.erase(r.cp)) {
				m_inFlightMesh.erase(r.cp);
				return;
			}
			applyMeshResult(std::move(r));
		},
		kMaxUploadsPerFrame);
}

void Game::preloadVisibleChunks() {
	// Block briefly at state-entry so the player's first rendered frame
	// already has a full horizon instead of radial pop-in. We pump the
	// network, run streamServerChunks (enqueue+drain), and loop until either
	// (a) all reachable chunks in range are either meshed or the server
	// can't give us more, or (b) we've burned the time budget. Workers keep
	// running in the background — anything unfinished gets picked up by the
	// normal streamServerChunks loop next frame.
	using clock = std::chrono::steady_clock;
	const auto start = clock::now();
	const auto budget = std::chrono::milliseconds(1800);
	size_t lastMeshCount = 0;
	auto lastProgress = start;

	while (clock::now() - start < budget) {
		m_server->tick(0.0f);
		streamServerChunks();

		size_t meshed = m_chunkMeshes.size();
		if (meshed != lastMeshCount) {
			lastMeshCount = meshed;
			lastProgress = clock::now();
		}
		// Quiesce: nothing in-flight, nothing dirty, no new meshes for 200ms
		// → server has no more chunks to give us within range. Bail early.
		if (m_inFlightMesh.empty() && m_serverDirtyChunks.empty() &&
		    clock::now() - lastProgress > std::chrono::milliseconds(200))
			break;

		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}

	// Pick up any final worker results before the first rendered frame.
	drainAsyncMeshes();

	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		clock::now() - start).count();
	std::printf("[vk-game] preload: %zu chunk meshes in %lldms (%zu in-flight)\n",
		m_chunkMeshes.size(), (long long)elapsed, m_inFlightMesh.size());
}

void Game::updateLoadingGate(float dt) {
	// Signals are gathered here and pushed into m_loading; the screen object
	// owns all the policy (smoothing, sticky, monotone, dismiss).
	m_loading.setWelcome(m_server && m_server->pollWelcome());

	if (m_server && m_server->isServerReady()) {
		m_loading.setWorldPrepared(1.0f);
	} else if (m_server) {
		float p = m_server->preparingProgress();
		m_loading.setWorldPrepared(p < 0.0f ? 0.0f : p);
	}

	pumpChunkStream();
	m_loading.setChunkProgress(computeChunkStreamFrac(), updateChunkQuiesce(dt));

	if (m_agentClient) {
		auto ip = m_agentClient->initProgress();
		m_loading.setAgentProgress(ip.discoveryRan, ip.totalAgents,
		                           ip.settledAgents, dt);
	}

	m_loading.tick();
}

// Pump network + mesher up to 8 ms during the Connecting screen so terrain
// fills in fast without freezing the UI. Bails as soon as there's nothing
// more to do so a small map doesn't burn the whole budget.
void Game::pumpChunkStream() {
	using Clock = std::chrono::steady_clock;
	if (!(m_server && m_server->pollWelcome())) return;
	if (m_loading.gate().phases[LoadingGate::ChunksLoaded].progress >= 1.0f) return;
	const auto budgetEnd = Clock::now() + std::chrono::milliseconds(8);
	size_t lastMeshed = m_chunkMeshes.size();
	while (Clock::now() < budgetEnd) {
		m_server->tick(0.0f);
		streamServerChunks();
		drainAsyncMeshes();
		if (m_inFlightMesh.empty() &&
		    m_serverDirtyChunks.empty() &&
		    m_chunkMeshes.size() == lastMeshed)
			break;
		lastMeshed = m_chunkMeshes.size();
	}
}

// meshedDone / (meshedDone + pending), pinned at peak so the bar never
// slides back when new dirty chunks show up late.
float Game::computeChunkStreamFrac() {
	size_t totalEntries = m_chunkMeshes.size();
	size_t inFlight     = m_inFlightMesh.size();
	size_t dirty        = m_serverDirtyChunks.size();
	size_t meshedDone   = (totalEntries > inFlight) ? totalEntries - inFlight : 0;
	size_t denom        = meshedDone + inFlight + dirty;
	float frac          = (denom == 0) ? 0.0f : (float)meshedDone / (float)denom;
	m_chunkStreamPeak   = std::max(m_chunkStreamPeak, frac);
	return m_chunkStreamPeak;
}

// True once mesh growth has stalled for kChunkQuiesceSec with nothing
// pending — the authoritative "terrain done" signal.
bool Game::updateChunkQuiesce(float dt) {
	constexpr float  kChunkQuiesceSec  = 0.4f;
	constexpr size_t kMinMeshedChunks  = 8;
	size_t totalEntries = m_chunkMeshes.size();
	size_t inFlight     = m_inFlightMesh.size();
	size_t dirty        = m_serverDirtyChunks.size();
	size_t meshedDone   = (totalEntries > inFlight) ? totalEntries - inFlight : 0;
	bool   pending      = (inFlight > 0) || (dirty > 0);
	if (meshedDone != m_chunkMeshesLastSeen || pending ||
	    meshedDone < kMinMeshedChunks) {
		m_chunkMeshesLastSeen = meshedDone;
		m_chunkQuiesceAccum   = 0.0f;
	} else {
		m_chunkQuiesceAccum += dt;
	}
	return m_chunkQuiesceAccum >= kChunkQuiesceSec;
}

void Game::streamServerChunks() {
	// Sweep a render-radius box around the player's current chunk, enqueueing
	// snapshots for any chunk we haven't meshed yet. The worker pool runs
	// ChunkMesher::buildMeshFromSnapshot off the main thread so meshing never
	// costs frame time — we only pay the snapshot cost (17KB memcpy per chunk)
	// and the RHI upload (deferred to applyMeshResult on the main thread).
	constexpr int kRenderChunkRadius = 12;   // ≈192 blocks horizontal
	constexpr int kRenderChunkVertical = 4;  // ±4 chunks in Y (64 blocks)
	// Upload throttle — workers can mesh fast but each createChunkMesh/
	// updateChunkMesh is a Vulkan allocation on the main thread. Too many
	// per frame becomes the new spike source (see [perf-spike] chunks=12ms).
	constexpr int kMaxNewPerFrame = 8;
	constexpr int kMaxRemeshPerFrame = 4;

	drainAsyncMeshes();

	auto div = [](int a, int b) { return (a >= 0) ? a / b : (a - b + 1) / b; };
	const int CS = civcraft::CHUNK_SIZE;
	auto* me = playerEntity();
	glm::vec3 pPos = me ? me->position : m_cam.position;
	civcraft::ChunkPos center = {
		div((int)std::floor(pPos.x), CS),
		div((int)std::floor(pPos.y), CS),
		div((int)std::floor(pPos.z), CS)
	};

	// ── Pass 1: mesh any fresh server chunks in range, closest-first ──
	struct Pending { civcraft::ChunkPos cp; int distSq; };
	std::vector<Pending> pending;
	pending.reserve(128);
	auto& src = m_server->chunks();
	for (int dy = -kRenderChunkVertical; dy <= kRenderChunkVertical; dy++)
		for (int dz = -kRenderChunkRadius; dz <= kRenderChunkRadius; dz++)
			for (int dx = -kRenderChunkRadius; dx <= kRenderChunkRadius; dx++) {
				civcraft::ChunkPos cp = {center.x + dx, center.y + dy, center.z + dz};
				// Tracked (sentinel or real mesh) or in-flight → skip.
				if (m_chunkMeshes.count(cp)) continue;
				if (!src.getChunkIfLoaded(cp)) continue;  // server hasn't sent it yet
				pending.push_back({cp, dx*dx + dy*dy + dz*dz});
			}
	std::sort(pending.begin(), pending.end(),
		[](const Pending& a, const Pending& b) { return a.distSq < b.distSq; });

	int built = 0;
	for (auto& p : pending) {
		if (built >= kMaxNewPerFrame) break;
		enqueueMeshBuild(p.cp);
		// Meshing this chunk can affect face-cull continuity on its
		// neighbors (their cross-border faces may now be hidden or exposed).
		for (auto& d : std::initializer_list<civcraft::ChunkPos>{
				{ 1,0,0},{-1,0,0},{0, 1,0},{0,-1,0},{0,0, 1},{0,0,-1} }) {
			civcraft::ChunkPos np = {p.cp.x + d.x, p.cp.y + d.y, p.cp.z + d.z};
			if (m_chunkMeshes.count(np)) m_serverDirtyChunks.insert(np);
		}
		built++;
	}

	// ── Pass 2: re-mesh dirty chunks (block changes + neighbor fallout) ──
	int remeshed = 0;
	for (auto it = m_serverDirtyChunks.begin();
	     it != m_serverDirtyChunks.end() && remeshed < kMaxRemeshPerFrame; ) {
		const civcraft::ChunkPos cp = *it;
		// Not GPU-resident yet — Pass 1 owns it.
		if (!m_chunkMeshes.count(cp)) {
			it = m_serverDirtyChunks.erase(it);
			continue;
		}
		// Already building — leave in dirty set; next tick re-queues with
		// fresh block state once the in-flight result lands.
		if (m_inFlightMesh.count(cp)) { ++it; continue; }
		enqueueMeshBuild(cp);
		remeshed++;
		it = m_serverDirtyChunks.erase(it);
	}
}

void Game::shutdown() {
	if (!m_rhi) return;
	// Stop the AI sidecars first so their children get SIGTERM while this
	// process is still alive and can reap them cleanly. Order is indifferent;
	// all three are independent, and all three must die before we exit.
	// Tear clients down first so their worker threads don't race a dying
	// sidecar socket. AudioCapture::shutdown releases the mic device.
	// TtsVoiceMux owns both its sidecars and clients; reset handles both.
	m_ttsMux.reset();
	m_whisperClient.reset();
	if (m_audioCapture) m_audioCapture->shutdown();
	m_audioCapture.reset();
	m_llmClient.reset();
	m_whisperSidecar.reset();
	m_llmSidecar.reset();
	// Tear down the agent client BEFORE main.cpp drops the NetworkServer:
	// AgentClient holds a reference to ServerInterface and its decide-worker
	// thread may still be in flight when we hit ~AgentClient.
	m_agentClient.reset();
	m_behaviorStore.reset();
	// Stop workers before the server's BlockRegistry reference disappears.
	// Any results still in the queue are discarded — shutting down.
	m_asyncMesher.reset();
	m_inFlightMesh.clear();
	m_staleInflightMeshes.clear();
	m_audio.shutdown();
	for (auto& kv : m_chunkMeshes) {
		if (kv.second != rhi::IRhi::kInvalidMesh) m_rhi->destroyMesh(kv.second);
	}
	m_chunkMeshes.clear();
}

void Game::pushNotification(const std::string& text, glm::vec3 color, float lifetime) {
	while (m_notifs.size() >= 6) m_notifs.erase(m_notifs.begin());
	m_notifs.push_back({text, color, 0.0f, lifetime});
}

// Inspect-panel coord hyperlinks: click a (x,y,z) in an entity's Goal line
// and the camera flies to that spot with a ground marker. ESC restores the
// prior pose. Re-clicking while already peeking just re-aims without
// stacking — single-slot save.
void Game::enterCoordPeek(glm::ivec3 target) {
	if (!m_peekActive) {
		m_peekSavedCam = std::make_unique<civcraft::Camera>(m_cam);
		m_peekActive   = true;
	}
	m_peekTarget = target;

	// RTS-style bird's-eye over the target cell — most legible across the
	// four camera modes and keeps the marker cube in frame.
	glm::vec3 center{target.x + 0.5f, (float)target.y + 0.5f, target.z + 0.5f};
	m_cam.mode         = civcraft::CameraMode::RTS;
	m_cam.rtsCenter    = center;
	m_cam.rtsHeight    = 10.0f;
	m_cam.rtsHeightTarget = 10.0f;
	m_cam.rtsAngle     = 55.0f;
	m_cam.rtsOrbitYaw  = -90.0f;
	m_cam.rtsPanVel    = {0, 0, 0};
	m_cam.resetMouseTracking();
}

void Game::exitCoordPeek() {
	if (!m_peekActive) return;
	if (m_peekSavedCam) m_cam = *m_peekSavedCam;
	m_peekSavedCam.reset();
	m_peekActive = false;
	m_cam.resetMouseTracking();
}

bool Game::connectAs(const std::string& creatureType) {
	if (!m_server) { m_connectError = "no server"; return false; }
	if (m_server->isConnected()) return true;  // already connected (skipMenu re-entry)
	m_connecting = true;
	m_connectError.clear();
	m_server->setCreatureType(creatureType);
	bool ok = m_server->createGame(m_pendingSeed, m_pendingTemplate);
	m_connecting = false;
	if (!ok) {
		const std::string& err = m_server->lastError();
		m_connectError = err.empty() ? "failed to join server" : err;
		return false;
	}
	return true;
}

bool Game::beginConnectAs(const std::string& creatureType) {
	if (!m_server) { m_connectError = "no server"; return false; }
	if (m_server->isConnected()) return true;
	m_connectError.clear();
	m_server->setCreatureType(creatureType);
	if (!m_server->beginConnect(m_pendingSeed, m_pendingTemplate)) {
		const std::string& err = m_server->lastError();
		m_connectError = err.empty() ? "failed to begin connect" : err;
		return false;
	}
	m_connecting = true;
	m_connectStartTime = m_wallTime;
	// Fresh attempt → clear any progress from a previous run so the
	// checklist starts empty.
	m_loading.reset();
	m_chunkQuiesceAccum    = 0.0f;
	m_chunkMeshesLastSeen  = 0;
	m_chunkStreamPeak      = 0.0f;

	// Freeze the world: during the Connecting loading screen we still let
	// decide() run (so plans are ready), but no entity actually moves or
	// picks things up until enterPlaying() re-enables the executor.
	if (m_agentClient) m_agentClient->setExecutorEnabled(false);
	return true;
}

void Game::enterMenu() {
	m_state = GameState::Menu;
	m_menuScreen = MenuScreen::Main;
	// Release mouse so the menu can be clicked / the cursor is visible.
	if (m_window) {
		glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
	m_mouseCaptured = false;
}

void Game::enterPlaying() {
	m_state = GameState::Playing;
	// Unfreeze the world on the same tick the gameplay HUD comes up — the
	// loading screen held the executor off so NPCs stayed put during warmup;
	// enable it now so the first Playing frame sees live entities.
	if (m_agentClient) m_agentClient->setExecutorEnabled(true);
	// Hotbar persistence path, keyed by spawn seed so two worlds on the same
	// machine don't share a layout. Flushed on every drag and on shutdown.
	{
		char buf[128];
		std::snprintf(buf, sizeof(buf), "/tmp/civcraft_hotbar_%d.txt",
		              m_pendingSeed);
		m_hotbarSavePath = buf;
	}
	m_hotbarSeeded = false;  // first S_INVENTORY after entering will seed it
	m_invOpen = false;
	m_slotRectsLast.clear();
	m_slotRectsThis.clear();
	// Reset client-only physics/animation state.
	m_onGround  = false;
	m_walkDist  = 0.0f;
	m_attackCD  = 0.0f;
	m_placeCD   = 0.0f;
	m_regenIdle = 0.0f;
	m_playerBodyYawInit = false;
	m_sprintFovBoost = 0.0f;
	m_modeHintsShown = 0;
	m_climb = {};
	m_slashes.clear();
	m_floaters.clear();
	m_adminMode = false;
	m_flyMode = false;
	// Init camera in FPS by default (most games start here). V cycles to TPS/RPG/RTS.
	glm::vec3 spawnPos = m_server->spawnPos();
	m_cam.mode = civcraft::CameraMode::FirstPerson;
	m_cam.player.feetPos = spawnPos;
	// Village world places the monument in +Z from spawn (village.offset_z=45).
	// Yaw convention: forward = (cos(yaw), 0, sin(yaw)) — +Z ⇒ 90°. Sync every
	// camera mode's heading so V-cycling keeps the player staring at the
	// monument instead of snapping to a different direction.
	m_cam.player.yaw = 90.0f;
	m_cam.lookYaw = 90.0f;
	m_cam.orbitYaw = 90.0f;
	m_cam.orbitPitch = 25.0f;
	m_cam.godOrbitYaw = 90.0f;
	m_cam.rtsOrbitYaw = 90.0f;
	m_cam.orbitDistanceTarget = kTune.camDistance;
	m_cam.orbitDistance = kTune.camDistance;
	m_cam.farPlane = 500.0f;
	m_cam.rtsCenter = spawnPos;
	m_cam.resetSmoothing();
	m_cam.resetMouseTracking();
	if (m_window) {
		bool needCapture = (m_cam.mode == civcraft::CameraMode::FirstPerson ||
		                    m_cam.mode == civcraft::CameraMode::ThirdPerson);
		glfwSetInputMode(m_window, GLFW_CURSOR,
			needCapture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
		m_mouseCaptured = needCapture;
	}

	// First-time FPS hint — shown once per session when the player spawns in.
	m_modeHintsShown |= 1u << (int)civcraft::CameraMode::FirstPerson;
	pushNotification(
		"WASD move · mouse look · LMB attack · RMB place · Q drop · V=camera",
		glm::vec3(0.75f, 0.82f, 0.92f), 4.0f);

	// Chunk preload — pump the network + worker pool for up to ~1.8s so the
	// first rendered frame already shows a full horizon. Respawn re-enters
	// this path; most chunks are already in m_chunkMeshes, so the quiesce
	// check bails out fast.
	preloadVisibleChunks();
}

void Game::openGameMenu() {
	if (m_state != GameState::Playing) return;
	m_state = GameState::GameMenu;
	// Release mouse so the menu overlay can be clicked.
	if (m_window) {
		glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
	m_mouseCaptured = false;
	// Reset camera smoothing so the view doesn't lurch when the player resumes
	// control (orbit angles don't drift while the menu is up, but the
	// mouse-delta tracker would otherwise emit a big jump on the first frame
	// after close).
	m_cam.resetMouseTracking();
}

void Game::closeGameMenu() {
	if (m_state != GameState::GameMenu) return;
	m_state = GameState::Playing;
	if (m_window) {
		bool needCapture = (m_cam.mode == civcraft::CameraMode::FirstPerson ||
		                    m_cam.mode == civcraft::CameraMode::ThirdPerson);
		glfwSetInputMode(m_window, GLFW_CURSOR,
			needCapture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
		m_mouseCaptured = needCapture;
	}
	m_cam.resetMouseTracking();
}

void Game::enterDead(const char* cause) {
	m_state = GameState::Dead;
	m_lastDeathReason = cause ? cause : "You died.";
	if (m_window) {
		glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		m_mouseCaptured = false;
	}
}

void Game::respawn() { enterPlaying(); }

void Game::onChar(uint32_t codepoint) {
	// Always queue; processInput decides whether DialogPanel consumes it.
	// Cap to avoid a paste-bomb blowing up the queue between frames.
	if (m_charQueue.size() < 256) m_charQueue.push_back(codepoint);
}

void Game::onKey(int glfwKey, int action) {
	if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
	// Only the handful of keys DialogPanel cares about.
	if (glfwKey != GLFW_KEY_BACKSPACE && glfwKey != GLFW_KEY_ENTER &&
	    glfwKey != GLFW_KEY_ESCAPE) return;
	if (m_keyQueue.size() < 64) m_keyQueue.push_back(glfwKey);
}

void Game::onScroll(double xoff, double yoff) {
	(void)xoff;
	if (m_state != GameState::Playing) return;
	float y = (float)yoff;
	// Scroll cycles the hotbar slot in FPS (no camera zoom there). Down = next.
	if (m_cam.mode == civcraft::CameraMode::FirstPerson && y != 0.0f) {
		int step = y > 0 ? -1 : 1;
		int n = (m_hotbar.selected + step + Hotbar::SLOTS) % Hotbar::SLOTS;
		m_hotbar.selected = n;
		if (!m_hotbarSavePath.empty()) m_hotbar.saveToFile(m_hotbarSavePath);
		return;
	}
	switch (m_cam.mode) {
	case civcraft::CameraMode::FirstPerson:
		break;
	case civcraft::CameraMode::ThirdPerson:
		m_cam.orbitDistanceTarget = std::clamp(m_cam.orbitDistanceTarget - y, 2.0f, 20.0f);
		break;
	case civcraft::CameraMode::RPG:
		m_cam.godDistanceTarget = std::clamp(m_cam.godDistanceTarget - y * 2.0f, 3.0f, 50.0f);
		break;
	case civcraft::CameraMode::RTS:
		// Zoom pivots around rtsCenter (screen center).
		m_cam.pendingRtsZoom += y;
		break;
	}
}
// ─────────────────────────────────────────────────────────────────────────
// Main loop — state dispatch
// ─────────────────────────────────────────────────────────────────────────

void Game::runOneFrame(float dt, float wallTime) {
	m_wallTime = wallTime;
	m_frameDt  = dt;
	m_menuTitleT += dt;
	m_frameProbe.begin();

	// FPS counter — recompute displayed rate once per second.
	m_fpsWindowS      += dt;
	m_fpsWindowFrames += 1;
	if (m_fpsWindowS >= 1.0f) {
		m_fpsDisplay      = (float)m_fpsWindowFrames / m_fpsWindowS;
		m_fpsWindowS      = 0.0f;
		m_fpsWindowFrames = 0;
	}

	int w = 0, h = 0;
	glfwGetFramebufferSize(m_window, &w, &h);
	m_fbW = w; m_fbH = h;
	m_aspect = h > 0 ? (float)w / (float)h : 1.0f;

	// Pump the network connection, stream chunks, run AI.
	m_server->tick(dt);
	m_frameProbe.mark("net");

	// Drain LAN server broadcasts so the Multiplayer menu has a live list.
	// Only while in Menu — once we're Playing the browser's list isn't shown.
	if (m_state == GameState::Menu)
		m_lanBrowser.tick(wallTime);
	streamServerChunks();
	m_frameProbe.mark("chunks");
	// Tick the agent client during gameplay AND during the Connecting loading
	// screen once the server welcome has landed — that lets every owned NPC
	// burn through its first decide while the loading screen is still up,
	// instead of all firing together on the first frame of Playing (which
	// used to drop FPS for a few seconds right after the handoff).
	bool warmingUp = (m_state == GameState::Menu &&
	                  m_menuScreen == MenuScreen::Connecting &&
	                  m_server && m_server->pollWelcome());
	if (m_agentClient && (m_state == GameState::Playing || warmingUp))
		m_agentClient->tick(dt);
	// Menu plaza only ticks while the user is on a menu screen. Once they
	// hit Play and we transition to GameState::Playing the plaza freezes
	// (its entities continue to exist but stop deciding/moving) — at that
	// point the renderer also stops drawing them, so it's invisible.
	if (m_menuPlaza && m_state == GameState::Menu)
		m_menuPlaza->tickFrame(dt);
	m_frameProbe.mark("agent");

	// Loading screen: refresh signals, then hand off once ready and the
	// player acknowledges with any key / click.
	if (m_state == GameState::Menu &&
	    m_menuScreen == MenuScreen::Connecting &&
	    m_connecting) {
		updateLoadingGate(dt);
		if (m_loading.ready() && m_loading.pollDismiss(m_window)) {
			m_connecting = false;
			enterPlaying();
		}
	}

	// Event detection — derive DECIDE / COMBAT / DEATH from entity deltas.
	{
		std::unordered_set<civcraft::EntityId> seen;
		m_server->forEachEntity([&](civcraft::Entity& e) {
			seen.insert(e.id());

			// DECIDE: goal-text change
			{
				auto& prev = m_entityGoals[e.id()];
				if (!e.goalText.empty() && e.goalText != prev) {
					prev = e.goalText;
					std::string typeName = e.typeId();
					auto col = typeName.find(':');
					if (col != std::string::npos) typeName = typeName.substr(col + 1);
					if (!typeName.empty()) typeName[0] = (char)toupper((unsigned char)typeName[0]);
					civcraft::GameLogger::instance().emit("DECIDE",
						"%s #%u: %s", typeName.c_str(), e.id(), e.goalText.c_str());
				}
			}

			// COMBAT / DEATH: HP decrease
			{
				int curHP = e.hp();
				auto hpIt = m_prevEntityHP.find(e.id());
				if (hpIt != m_prevEntityHP.end() && curHP < hpIt->second) {
					int dmg = hpIt->second - curHP;
					bool dying = (curHP <= 0);
					std::string eName = e.def().display_name.empty()
						? e.typeId() : e.def().display_name;
					if (dying) {
						civcraft::GameLogger::instance().emit("DEATH", "%s #%u died",
							eName.c_str(), e.id());
						// Death blow — heavier hit sound at the dying entity.
						m_audio.play("hit_sword", e.position, 0.9f);
						// Notification if *we* killed it (hitmarker was fresh).
						if (m_hitmarkerTimer > 0.05f) {
							char buf[96];
							std::snprintf(buf, sizeof(buf), "Defeated %s", eName.c_str());
							pushNotification(buf, glm::vec3(1.0f, 0.70f, 0.25f), 4.0f);
						}
					} else {
						civcraft::GameLogger::instance().emit("COMBAT",
							"%s #%u took %d damage (%d→%d)",
							eName.c_str(), e.id(), dmg, hpIt->second, curHP);
						// Big hits get the sword slice, light hits the punch.
						m_audio.play(dmg >= 4 ? "hit_sword" : "hit_punch",
							e.position, 0.6f);
					}
					// Local player hit → trigger vignette + shake.
					if (e.id() == m_server->localPlayerId()) {
						int maxHp = e.def().max_hp > 0 ? e.def().max_hp : 100;
						float frac = std::min(1.0f, (float)dmg / (float)std::max(1, maxHp / 5));
						m_damageVignette = std::max(m_damageVignette, 0.6f + 0.4f * frac);
						m_cameraShake    = std::max(m_cameraShake,    0.25f + 0.20f * frac);
						m_shakeIntensity = std::max(m_shakeIntensity, 0.08f + 0.12f * frac);
					}
				}
				m_prevEntityHP[e.id()] = curHP;
			}
		});

		// Prune stale entries for removed entities
		for (auto it = m_entityGoals.begin(); it != m_entityGoals.end(); )
			it = seen.count(it->first) ? std::next(it) : m_entityGoals.erase(it);
		for (auto it = m_prevEntityHP.begin(); it != m_prevEntityHP.end(); )
			it = seen.count(it->first) ? std::next(it) : m_prevEntityHP.erase(it);
		for (auto it = m_prevInv.begin(); it != m_prevInv.end(); )
			it = seen.count(it->first) ? std::next(it) : m_prevInv.erase(it);
	}
	m_frameProbe.mark("events");

	// ── Sim ────────────────────────────────────────────────────────────
	processInput(dt);
	if (m_state == GameState::Playing) {
		tickPlayer(dt);
		tickCombat(dt);
		updatePickups(dt);
		tickFloaters(dt);
	} else if (m_state == GameState::Dead) {
		bool r = glfwGetKey(m_window, GLFW_KEY_R) == GLFW_PRESS;
		if (r) respawn();
	}

	// File-based debug triggers (compiled out in Release via NDEBUG).
	m_debugTriggers.poll();
	m_frameProbe.mark("sim");

	// ── GPU sync ───────────────────────────────────────────────────────
	// beginFrame() calls vkWaitForFences + vkAcquireNextImageKHR: the CPU
	// blocks until the prior in-flight frame's fence signals AND the
	// presenter hands back a swap image. On a GPU-bound or vsync-capped
	// session this is where the bulk of the frame budget goes — it's
	// CPU-idle time, not CPU work. Keeping it on its own probe is how we
	// tell "too much draw work" from "waiting on the GPU" in the rollup.
	bool rendered = m_rhi->beginFrame();
	m_frameProbe.mark("gpuWait");

	// ── Render ─────────────────────────────────────────────────────────
	if (rendered) {

	if (m_state == GameState::Menu) {
		// Render a calm ambient backdrop so the menu isn't just a
		// solid rect — reuse the sky + a slow camera orbit over the
		// menu plaza (grass + trees emitted by WorldRenderer).
		float menuAng = m_menuTitleT * 0.08f;
		glm::vec3 menuFocus(std::sin(menuAng) * 0.5f, 2.5f,
		                    std::cos(menuAng) * 0.5f);
		// CharacterSelect / Connecting pin the camera so the ScreenShell's
		// preview model holds steady in the preview area (flush-right of the
		// LeftBar, above the BottomBar). Other menu screens keep the slow
		// orbit. Pin condition: we're on a shell-driven screen AND the shell
		// actually has a preview id.
		bool previewing = (m_menuScreen == MenuScreen::CharacterSelect
		                || m_menuScreen == MenuScreen::Connecting
		                || m_menuScreen == MenuScreen::Handbook)
		               && !m_shell.previewId.empty();
		if (previewing) {
			// Pinned plaza-level camera: the preview character stands at world
			// (0, 1, 0) (top of the grass slab). Aim-point is shifted LEFT of
			// the character so the projection centers it within the shell's
			// preview area (roughly the right 3/4 of the screen).
			m_cam.position = glm::vec3(5.0f, 2.8f, -5.0f);
			glm::vec3 target(1.5f, 1.8f, 0.5f);
			glm::vec3 look = glm::normalize(target - m_cam.position);
			m_cam.lookYaw   = glm::degrees(std::atan2(look.z, look.x));
			m_cam.lookPitch = glm::degrees(std::asin(look.y));
		} else {
			// Manually drive camera for the menu backdrop — don't run the full
			// Camera::processInput (it would reset mouse-tracking every frame).
			// Wider orbit + slight tilt so the plaza + tree silhouettes read
			// as layered depth behind the menu chrome.
			float radius = 14.0f;
			float pitch  = 0.12f;   // slight downward tilt
			float yaw    = menuAng + 3.14f * 0.5f;
			glm::vec3 head = menuFocus + glm::vec3(0, 1.5f, 0);
			glm::vec3 dir(std::cos(pitch) * std::cos(yaw),
			              std::sin(pitch),
			              std::cos(pitch) * std::sin(yaw));
			m_cam.position = head - dir * radius;
			glm::vec3 look = glm::normalize(head - m_cam.position);
			m_cam.lookYaw   = glm::degrees(std::atan2(look.z, look.x));
			m_cam.lookPitch = glm::degrees(std::asin(look.y));
		}
		m_worldRenderer.renderWorld(wallTime);
		m_worldRenderer.renderEffects(wallTime);
		m_menuRenderer.renderMenu();
	} else if (m_state == GameState::Playing) {
		m_worldRenderer.renderWorld(wallTime);
		m_frameProbe.mark("world");
		m_worldRenderer.renderEntities(wallTime);
		m_frameProbe.mark("ents");
		m_worldRenderer.renderEffects(wallTime);
		m_frameProbe.mark("fx3d");
		// 3D pass for hotbar/inventory item previews. Reads last frame's slot
		// rects (items composite on top of their backing tiles in the main pass);
		// writes none.
		m_hudRenderer.renderInventoryItems3D();
		m_frameProbe.mark("inv3d");
		// Clear slot-rect collection — every slot drawn this frame re-records.
		m_slotRectsThis.clear();
		m_hudRenderer.renderHUD();
		m_hudRenderer.renderHotbarBar();
		m_hudRenderer.renderInventoryPanel();
		// Publish this frame's rects to be read by next frame's 3D pass.
		m_slotRectsLast = m_slotRectsThis;
		m_frameProbe.mark("hud");
		if (m_inspectedEntity != 0) m_entityUiRenderer.renderEntityInspect();
		if (m_showDebug) m_panelRenderer.renderDebugOverlay();
		if (m_showTuning) m_panelRenderer.renderTuningPanel();
		if (m_handbookOpen) m_panelRenderer.renderHandbook();
		if (m_dialogPanel.isOpen()) m_dialogPanel.render(m_rhi, m_aspect);
		m_entityUiRenderer.renderRTSSelect();
		m_entityUiRenderer.renderRTSDragCommand();
		m_frameProbe.mark("panels");
	} else if (m_state == GameState::GameMenu) {
		m_worldRenderer.renderWorld(wallTime);
		m_frameProbe.mark("world");
		m_worldRenderer.renderEntities(wallTime);
		m_frameProbe.mark("ents");
		m_worldRenderer.renderEffects(wallTime);
		m_frameProbe.mark("fx3d");
		m_hudRenderer.renderHUD();       // world keeps ticking underneath
		m_frameProbe.mark("hud");
		m_menuRenderer.renderGameMenu();
		if (m_showTuning) m_panelRenderer.renderTuningPanel();
		m_frameProbe.mark("panels");
	} else {  // Dead
		m_worldRenderer.renderWorld(wallTime);
		m_worldRenderer.renderEffects(wallTime);
		m_hudRenderer.renderHUD();       // still show world state behind the veil
		m_menuRenderer.renderDeath();
	}

	// F2 / file-trigger screenshot — must run mid-frame (needs active render
	// pass), so this one stays inline rather than in DebugTriggers.
	{
		static bool f2Held = false;
		bool f2Now = glfwGetKey(m_window, GLFW_KEY_F2) == GLFW_PRESS;
		bool take = (f2Now && !f2Held);
#ifndef NDEBUG
		if (!take && std::filesystem::exists("/tmp/civcraft_screenshot_request")) {
			std::filesystem::remove("/tmp/civcraft_screenshot_request");
			take = true;
		}
#endif
		if (take) {
			static int shotN = 0;
			auto path = "/tmp/civcraft_vk_screenshot_" + std::to_string(shotN++) + ".ppm";
			if (m_rhi->screenshot(path.c_str()))
				std::printf("[vk] wrote %s\n", path.c_str());
		}
		f2Held = f2Now;
	}

	m_rhi->endFrame();
	m_frameProbe.mark("present");
	} // end if (rendered)
	if (!rendered) m_frameProbe.mark("skip");

	// ── Perf rollup ────────────────────────────────────────────────────
	// Console prints only on spike frames; the rolling rollup moved to the
	// PERF_RECORD histograms (dumped on exit) so normal-path runs stay quiet.
	{
		double totalMs = std::chrono::duration<double, std::milli>(
			m_frameProbe.last - m_frameProbe.frameStart).count();

#ifdef CIVCRAFT_PERF
		// Session-long histograms + counters. Scoped to Playing *and* server
		// ready so main-menu, loading screens, and the pre-S_READY window
		// (chunks still streaming, player entity not yet pushed) don't
		// pollute p50/p99. First qualifying frame anchors the session-start
		// timestamp for the exit summary.
		if (m_state == GameState::Playing && m_server && m_server->isServerReady()) {
			if (m_perfSessionStart == 0.0) {
				m_perfSessionStart = std::chrono::duration<double>(
					std::chrono::steady_clock::now().time_since_epoch()).count();
			}
			PERF_RECORD_MS("client.frame.total_ms", totalMs);
			PERF_COUNT("client.frames.total");
			// 60Hz target → 16.67ms. "slow" = missed one vsync; "dropped" =
			// missed two (player-visible stutter).
			if (totalMs > 16.7) PERF_COUNT("client.frames.slow_16ms");
			if (totalMs > 33.3) PERF_COUNT("client.frames.dropped_33ms");

			// Per-section histograms. Cache histogram* by literal pointer —
			// mark() is always called with string literals so identity is
			// stable, and we avoid reallocating the "client.phase.<name>"
			// key every frame.
			static std::unordered_map<const char*, civcraft::perf::Histogram*> phaseCache;
			for (auto& [name, ms] : m_frameProbe.sections) {
				auto& h = phaseCache[name];
				if (!h) {
					char key[64];
					std::snprintf(key, sizeof(key), "client.phase.%s", name);
					h = &civcraft::perf::Registry::instance().histogram(key);
				}
				h->record(ms);
			}

			// Population gauges — what was on screen this frame. fx3d /
			// world / chunks costs scale with these, so correlate with
			// client.phase.* p99 to attribute blow-ups.
			//   * particles vector holds 8 floats per particle (XYZ size RGBA);
			//     it's cleared at the top of next frame's renderEffects, so
			//     reading here captures THIS frame's pushes.
			//   * meshed = total mesh entries minus in-flight placeholders.
			size_t particleCount = m_scratch.particles.size() / 8;
			size_t inFlight      = m_inFlightMesh.size();
			size_t totalEntries  = m_chunkMeshes.size();
			size_t meshed        = (totalEntries > inFlight)
			                       ? totalEntries - inFlight : 0;
			PERF_RECORD_MS("client.fx3d.particle_count",  (double)particleCount);
			PERF_RECORD_MS("client.chunks.mesh_count",    (double)meshed);
			PERF_RECORD_MS("client.chunks.inflight_count",(double)inFlight);
			if (m_server)
				PERF_RECORD_MS("client.entities.known_count",
				               (double)m_server->entityCount());
		}
#endif
		if (totalMs > m_frameProbe.spikeMs && !m_frameProbe.sections.empty()) {
			char line[512]; int n = 0;
			n += std::snprintf(line + n, sizeof(line) - n, "[perf-spike] %.1fms", totalMs);
			for (auto& [name, ms] : m_frameProbe.sections)
				n += std::snprintf(line + n, sizeof(line) - n, " %s=%.1f", name, ms);
			std::fprintf(stderr, "%s\n", line);
		}
	}
}

void Game::setupServerCallbacks() {
	m_server->setEffectCallbacks(
		[this](civcraft::ChunkPos cp) { m_serverDirtyChunks.insert(cp); },
		[this](glm::vec3 pos, const std::string& blockName) {
			char buf[160];
			snprintf(buf, sizeof(buf), "broke %s @(%d,%d,%d)",
				blockName.c_str(), (int)pos.x, (int)pos.y, (int)pos.z);
			civcraft::GameLogger::instance().emit("ACTION", "%s", buf);
			FloatText ft;
			ft.worldPos = pos + glm::vec3(0.5f, 1.5f, 0.5f);
			ft.color    = glm::vec3(0.85f, 0.75f, 0.55f);
			std::string name = blockName;
			if (!name.empty()) name[0] = (char)toupper((unsigned char)name[0]);
			for (auto& c : name) if (c == '_') c = ' ';
			ft.text     = name;
			ft.lifetime = 0.8f;
			m_floaters.push_back(ft);

			// Block-break sound — pick group from the block's display/id name.
			std::string snd = "dig_stone";
			const std::string& lb = blockName;
			auto has = [&](const char* s) { return lb.find(s) != std::string::npos; };
			if (has("wood") || has("log") || has("plank")) snd = "dig_wood";
			else if (has("leaf") || has("leaves"))         snd = "dig_leaves";
			else if (has("dirt") || has("grass"))          snd = "dig_dirt";
			else if (has("sand"))                          snd = "dig_sand";
			else if (has("snow") || has("ice"))            snd = "dig_snow";
			else if (has("glass"))                         snd = "dig_glass";
			else if (has("iron") || has("metal") || has("gold") || has("copper")) snd = "dig_metal";
			m_audio.play(snd, pos + glm::vec3(0.5f), 0.55f);

			// Minecraft-style debris burst. Color uses the block's top face
			// tint as a proxy for "what it looks like" — we don't run the
			// full per-face shader for particles, so one color is the closest
			// cheap match. Fires on BOTH player prediction and server-
			// observed breaks (villager chops, TNT) — one path, no duplicates.
			const civcraft::BlockDef* bdef = m_server->blockRegistry().find(blockName);
			glm::vec3 color = bdef ? bdef->color_top : glm::vec3(0.6f, 0.5f, 0.4f);
			spawnBreakBurst(pos + glm::vec3(0.5f), color);
		},
		// onBlockPlace: fires on AIR→X and X→Y (door toggle, water flow, etc.)
		[this](glm::vec3 pos, const std::string& newBlockId) {
			glm::vec3 worldPos = pos + glm::vec3(0.5f);
			// Door toggle gets its own sound + hinged-panel swing animation.
			// The ":door" / ":door_open" string_ids come from the server's
			// authoritative S_BLOCK broadcast, so both players see the swing.
			auto endsWith = [&](const char* s) {
				size_t n = strlen(s);
				return newBlockId.size() >= n &&
				       newBlockId.compare(newBlockId.size() - n, n, s) == 0;
			};
			bool isDoor     = endsWith(":door");
			bool isDoorOpen = endsWith(":door_open");
			if (isDoor || isDoorOpen) {
				m_audio.play("door_open", worldPos, 0.6f);
				// Walk the door column to find base + height, read hinge bit,
				// and push an anim. The chunk has already been updated with the
				// new block (network_server.h calls onBlockPlace after setBlock).
				glm::ivec3 bp{(int)std::floor(pos.x),
				              (int)std::floor(pos.y),
				              (int)std::floor(pos.z)};
				auto& chunks = m_server->chunks();
				auto& blocks = m_server->blockRegistry();
				auto isDoorBlock = [&](int x, int y, int z) {
					const auto& bd = blocks.get(chunks.getBlock(x, y, z));
					return bd.string_id.size() >= 4 &&
					       (bd.string_id.rfind(":door") == bd.string_id.size() - 5 ||
					        bd.string_id.rfind(":door_open") == bd.string_id.size() - 10);
				};
				// Walk down to base
				glm::ivec3 base = bp;
				while (base.y > 0 && isDoorBlock(base.x, base.y - 1, base.z)) base.y--;
				// Count height upward (max 8)
				int h = 0;
				for (int dy = 0; dy < 8; dy++) {
					if (isDoorBlock(base.x, base.y + dy, base.z)) h++;
					else break;
				}
				if (h < 1) h = 1;
				// Hinge bit lives in param2 bit 2 (matches chunk_mesher.cpp:217).
				civcraft::ChunkPos cp = {
					(base.x >= 0 ? base.x / civcraft::CHUNK_SIZE : (base.x - civcraft::CHUNK_SIZE + 1) / civcraft::CHUNK_SIZE),
					(base.y >= 0 ? base.y / civcraft::CHUNK_SIZE : (base.y - civcraft::CHUNK_SIZE + 1) / civcraft::CHUNK_SIZE),
					(base.z >= 0 ? base.z / civcraft::CHUNK_SIZE : (base.z - civcraft::CHUNK_SIZE + 1) / civcraft::CHUNK_SIZE)
				};
				civcraft::Chunk* ch = chunks.getChunk(cp);
				int lx = ((base.x % civcraft::CHUNK_SIZE) + civcraft::CHUNK_SIZE) % civcraft::CHUNK_SIZE;
				int ly = ((base.y % civcraft::CHUNK_SIZE) + civcraft::CHUNK_SIZE) % civcraft::CHUNK_SIZE;
				int lz = ((base.z % civcraft::CHUNK_SIZE) + civcraft::CHUNK_SIZE) % civcraft::CHUNK_SIZE;
				uint8_t p2 = ch ? ch->getParam2(lx, ly, lz) : 0;
				DoorAnim da;
				da.basePos    = base;
				da.height     = h;
				da.opening    = isDoorOpen;
				da.hingeRight = (p2 >> 2) & 1;
				const auto& bdef = blocks.get(chunks.getBlock(base.x, base.y, base.z));
				da.color      = bdef.color_side;
				m_doorAnims.push_back(da);
				return;
			}
			if (newBlockId.find("wood") != std::string::npos ||
			    newBlockId.find("log")  != std::string::npos ||
			    newBlockId.find("plank")!= std::string::npos)
				m_audio.play("place_wood", worldPos, 0.5f);
			else if (newBlockId.find("dirt") != std::string::npos ||
			         newBlockId.find("sand") != std::string::npos ||
			         newBlockId.find("grass")!= std::string::npos)
				m_audio.play("place_soft", worldPos, 0.5f);
			else
				m_audio.play("place_stone", worldPos, 0.5f);
		}
	);
	m_server->setInventoryCallback([this](civcraft::EntityId eid) {
		if (!m_server) return;
		civcraft::Entity* ent = m_server->getEntity(eid);
		if (!ent || !ent->inventory) return;
		auto prevIt = m_prevInv.find(eid);
		bool seedingFirstSnapshot = (prevIt == m_prevInv.end());
		auto& prev = m_prevInv[eid];
		std::unordered_map<std::string,int> cur;
		for (auto& [iid, cnt] : ent->inventory->items()) cur[iid] = cnt;
		std::string typeName = ent->typeId();
		auto col = typeName.find(':');
		if (col != std::string::npos) typeName = typeName.substr(col + 1);
		if (!typeName.empty()) typeName[0] = (char)toupper((unsigned char)typeName[0]);
		bool isLocalPlayer = (eid == m_server->localPlayerId());
		for (auto& [iid, cnt] : cur) {
			int was = 0;
			auto it = prev.find(iid);
			if (it != prev.end()) was = it->second;
			if (cnt != was) {
				int delta = cnt - was;
				civcraft::GameLogger::instance().emit("INV",
					"%s #%u %s %s x%d",
					typeName.c_str(), eid,
					seedingFirstSnapshot ? "Restored" :
					  (delta > 0 ? "Picked up" : "Dropped"),
					iid.c_str(), delta > 0 ? delta : -delta);
				// Initial S_INVENTORY arrives during handshake (while still
				// at menu) and during respawn/restore — items the player
				// already owned aren't new pickups, so don't blip/notify.
				if (delta > 0 && isLocalPlayer && !seedingFirstSnapshot) {
					m_audio.play("item_pickup", 0.5f);
					std::string pretty = iid;
					auto colon = pretty.find(':');
					if (colon != std::string::npos) pretty = pretty.substr(colon + 1);
					for (auto& c : pretty) if (c == '_') c = ' ';
					if (!pretty.empty()) pretty[0] = (char)toupper((unsigned char)pretty[0]);
					char buf[96];
					std::snprintf(buf, sizeof(buf), "+%d %s", delta, pretty.c_str());
					pushNotification(buf, glm::vec3(0.60f, 0.95f, 0.55f), 3.0f);
				}
			}
		}
		for (auto& [iid, was] : prev) {
			if (cur.find(iid) == cur.end() && was != 0) {
				civcraft::GameLogger::instance().emit("INV",
					"%s #%u Dropped %s x%d",
					typeName.c_str(), eid, iid.c_str(), was);
			}
		}
		prev.swap(cur);

		// Keep the hotbar alias layer in sync with the local player's
		// inventory. First delivery primes the slots by priority order; every
		// subsequent S_INVENTORY merges in new picks and clears slots for
		// items the player no longer carries. Drag layout (set via UI) is
		// persisted to m_hotbarSavePath and preserved across merges.
		if (isLocalPlayer && ent->inventory) {
			if (!m_hotbarSeeded) {
				bool loaded = !m_hotbarSavePath.empty()
				    && m_hotbar.loadFromFile(m_hotbarSavePath);
				if (!loaded) m_hotbar.repopulateFrom(*ent->inventory);
				m_hotbarSeeded = true;
			}
			m_hotbar.mergeFrom(*ent->inventory);
			if (!m_hotbarSavePath.empty())
				m_hotbar.saveToFile(m_hotbarSavePath);
		}
	});

	// Entity-removal FX: server tags each S_REMOVE with a reason byte
	// (EntityRemovalReason). OwnerOffline → gentle puff + no SFX so a
	// disconnecting friend's mobs fade quietly. Died/Despawned fall through;
	// death SFX, if any, should live on the attacker path, not here.
	m_server->setEntityRemoveCallback(
		[this](civcraft::EntityId /*eid*/, glm::vec3 pos, uint8_t reason) {
			if (reason == (uint8_t)civcraft::EntityRemovalReason::OwnerOffline) {
				spawnBreakBurst(pos + glm::vec3(0.0f, 0.8f, 0.0f),
				                glm::vec3(0.85f, 0.88f, 0.95f));
			}
		});

	std::printf("[vk-game] chunks will stream from %s\n",
		m_server->isConnected() ? "network" : "???");
}

void Game::registerDebugTriggers() {
	// Register file-based debug triggers (no-op in Release builds).
	m_debugTriggers.addTrigger("/tmp/civcraft_respawn_request", [this] {
		if (m_state == GameState::Dead) respawn();
	});
	m_debugTriggers.addTrigger("/tmp/civcraft_vk_dig_request", [this] {
		digInFront();
	});
	// Debug: break the nearest non-spawn_point solid block below the player.
	m_debugTriggers.addTrigger("/tmp/civcraft_vk_dig_feet_request", [this] {
		auto* me = playerEntity();
		if (!me) { std::fprintf(stderr, "[vk-debug] dig_feet: no player\n"); return; }
		auto& reg = m_server->blockRegistry();
		glm::ivec3 bp{(int)std::floor(me->position.x),
		              (int)std::floor(me->position.y) - 1,
		              (int)std::floor(me->position.z)};
		civcraft::BlockId bid = civcraft::BLOCK_AIR;
		for (int dy = 0; dy < 6; dy++) {
			glm::ivec3 cand = bp - glm::ivec3(0, dy, 0);
			civcraft::BlockId cbid = m_server->chunks().getBlock(cand.x, cand.y, cand.z);
			const auto& cdef = reg.get(cbid);
			if (cbid != civcraft::BLOCK_AIR && cdef.string_id != "spawn_point") {
				bp = cand; bid = cbid; break;
			}
		}
		const auto& bdef = reg.get(bid);
		std::fprintf(stderr, "[vk-debug] dig_feet: target=(%d,%d,%d) bid=%u id=%s drop=%s\n",
			bp.x, bp.y, bp.z, bid, bdef.string_id.c_str(), bdef.drop.c_str());
		if (bid == civcraft::BLOCK_AIR) return;
		civcraft::ActionProposal p;
		p.actorId     = m_server->localPlayerId();
		p.type        = civcraft::ActionProposal::Convert;
		p.fromItem    = bdef.string_id;
		p.toItem      = bdef.drop.empty() ? bdef.string_id : bdef.drop;
		p.fromCount   = 1;
		p.toCount     = 1;
		p.convertFrom = civcraft::Container::block(bp);
		p.convertInto = civcraft::Container::ground();
		m_server->sendAction(p);
	});
	m_debugTriggers.addTrigger("/tmp/civcraft_vk_attack_request", [this] {
		auto* me = playerEntity(); if (!me) return;
		glm::vec3 from = me->position + glm::vec3(0, kTune.playerHeight * 0.6f, 0);
		m_slashes.push_back({ from, playerForward() });
		if (!tryServerAttack())
			std::printf("[vk-game] [trigger] swing — no target in cone\n");
	});
	m_debugTriggers.addTrigger("/tmp/civcraft_vk_camera_request", [this] {
		m_cam.cycleMode();
		const char* names[] = {"FPS", "ThirdPerson", "RPG", "RTS"};
		std::printf("[vk-game] [trigger] camera → %s\n", names[(int)m_cam.mode]);
	});
	m_debugTriggers.addTrigger("/tmp/civcraft_vk_lookup_request", [this] {
		m_cam.lookPitch  = 55.0f;   // FPS / ThirdPerson free-look
		m_cam.orbitPitch = -40.0f;  // TPS orbit (negative = view aims up)
		std::printf("[vk-game] [trigger] look up (lookPitch=55, orbitPitch=-40)\n");
	});
	m_debugTriggers.addTrigger("/tmp/civcraft_vk_face_east_request", [this] {
		m_cam.lookYaw    = 0.0f;    // +X (toward sunrise)
		m_cam.lookPitch  = 35.0f;   // horizon slightly low, most of frame = sky + clouds
		m_cam.orbitYaw   = 0.0f;
		m_cam.orbitPitch = -25.0f;
		m_cam.resetSmoothing();
		std::printf("[vk-game] [trigger] face east (sunrise view)\n");
	});
	m_debugTriggers.addTrigger("/tmp/civcraft_vk_noon_request", [this] {
		if (m_server) {
			// no client-side setter; this only works in the single-process test
			// scenario — cast through if available, else noop
			std::printf("[vk-game] [trigger] noon request — set time is server-side\n");
		}
	});
	m_debugTriggers.addTrigger("/tmp/civcraft_vk_place_request", [this] {
		placeBlock();
	});
	m_debugTriggers.addTrigger("/tmp/civcraft_vk_admin_request", [this] {
		m_adminMode = !m_adminMode;
		if (!m_adminMode) m_flyMode = false;
		std::printf("[vk-game] [trigger] admin mode %s\n", m_adminMode ? "ON" : "OFF");
	});
	m_debugTriggers.addTrigger("/tmp/civcraft_vk_fly_request", [this] {
		if (m_adminMode) {
			m_flyMode = !m_flyMode;
			std::printf("[vk-game] [trigger] fly mode %s\n", m_flyMode ? "ON" : "OFF");
		}
	});
	m_debugTriggers.addTrigger("/tmp/civcraft_vk_pause_request", [this] {
		if (m_state == GameState::Playing) openGameMenu();
		else if (m_state == GameState::GameMenu) closeGameMenu();
	});
	m_debugTriggers.addPayloadTrigger("/tmp/civcraft_vk_goto_request", [this](const std::string& line) {
		float tx = 0, ty = 0, tz = 0;
		if (sscanf(line.c_str(), "%f %f %f", &tx, &ty, &tz) >= 3) {
			glm::vec3 target(tx, ty, tz);
			// Direct-drive: virtual joystick tick steers toward the target.
			m_hasMoveOrder    = true;
			m_moveOrderTarget = target;
			std::printf("[vk-game] [trigger] goto (%.1f,%.1f,%.1f)\n", tx, ty, tz);
		}
	});
	m_debugTriggers.addTrigger("/tmp/civcraft_vk_ascend_request", [this] {
		auto* me = playerEntity();
		if (m_flyMode && me) {
			me->position.y += 5.0f;
			std::printf("[vk-game] [trigger] ascend +5 → y=%.1f\n", me->position.y);
		}
	});
	m_debugTriggers.addTrigger("/tmp/civcraft_vk_handbook_request", [this] {
		m_handbookOpen = !m_handbookOpen;
		std::printf("[vk-game] [trigger] handbook %s\n", m_handbookOpen ? "OPEN" : "CLOSED");
	});
	m_debugTriggers.addTrigger("/tmp/civcraft_vk_inventory_request", [this] {
		m_invOpen = !m_invOpen;
		std::printf("[vk-game] [trigger] inventory %s\n", m_invOpen ? "OPEN" : "CLOSED");
	});
	// Test drag: place cursor on an inventory slot (given slot index 0..47) and
	// drop on hotbar slot `to` (0..9). Format: "<from_inv_idx> <to_hotbar_idx>".
	// No LMB events are synthesized — we call into the hotbar directly since
	// this is a dev shortcut for screenshot verification.
	m_debugTriggers.addPayloadTrigger("/tmp/civcraft_vk_cursor_request",
		[this](const std::string& line) {
			float x, y;
			if (sscanf(line.c_str(), "%f %f", &x, &y) == 2) {
				m_mouseNdcX = x;
				m_mouseNdcY = y;
				std::printf("[vk-game] [trigger] cursor=(%.2f,%.2f)\n", x, y);
			}
		});
	m_debugTriggers.addTrigger("/tmp/civcraft_vk_tuning_request", [this] {
		m_showTuning = !m_showTuning;
		std::printf("[vk-game] [trigger] tuning %s\n", m_showTuning ? "OPEN" : "CLOSED");
	});
	m_debugTriggers.addPayloadTrigger("/tmp/civcraft_vk_inspect_request", [this](const std::string& line) {
		int eid = 0;
		if (sscanf(line.c_str(), "%d", &eid) == 1 && eid > 0) {
			m_inspectedEntity = (civcraft::EntityId)eid;
			std::printf("[vk-game] [trigger] inspect entity #%d\n", eid);
		}
	});
}

void Game::initAiSidecars() {
	// Spawn all three AI sidecars so `make game` brings up dialog + voice
	// out of the box. Each is independent: LLM can run without STT, STT can
	// run without TTS. If the user hasn't run the corresponding `make *_setup`,
	// the missing sidecar degrades gracefully (dialog still works, just text-
	// only on the input side or silent NPCs on the output side). Client-only
	// (Rule 5); server never sees any of these processes.
	{
		civcraft::llm::LlmSidecar::Paths lp;
		if (civcraft::llm::LlmSidecar::probe(lp)) {
			m_llmSidecar = std::make_unique<civcraft::llm::LlmSidecar>();
			if (!m_llmSidecar->start(lp)) {
				std::fprintf(stderr, "[vk-game] llm sidecar failed to start; NPC dialog disabled\n");
				m_llmSidecar.reset();
			}
		} else {
			std::printf("[vk-game] no llama-server/model found — NPC dialog disabled. "
			            "Run 'make llm_setup' to enable.\n");
		}

		civcraft::llm::WhisperSidecar::Paths wp;
		if (civcraft::llm::WhisperSidecar::probe(wp)) {
			m_whisperSidecar = std::make_unique<civcraft::llm::WhisperSidecar>();
			if (!m_whisperSidecar->start(wp)) {
				std::fprintf(stderr, "[vk-game] whisper sidecar failed to start; STT disabled\n");
				m_whisperSidecar.reset();
			} else {
				// Only bring up the mic + HTTP client when the sidecar is live.
				// If the mic is missing (headless box), we still leave the HTTP
				// client around so future device hotplug could work — but
				// DialogPanel gates push-to-talk on AudioCapture::isReady().
				m_audioCapture = std::make_unique<civcraft::AudioCapture>();
				if (!m_audioCapture->init()) {
					std::fprintf(stderr, "[vk-game] mic init failed; push-to-talk disabled\n");
					m_audioCapture.reset();
				}
				civcraft::llm::WhisperClient::Config wc;
				wc.host = "127.0.0.1";
				wc.port = 8081;
				m_whisperClient = std::make_unique<civcraft::llm::WhisperClient>(wc);
			}
		} else {
			std::printf("[vk-game] no whisper-server/model found — STT disabled. "
			            "Run 'make whisper_setup' to enable push-to-talk.\n");
		}

		// TTS voice mux: one piper child per distinct `dialog_voice` value.
		// We don't eagerly spawn any here — the first NPC conversation will
		// instantiate its voice on demand.
		m_ttsMux = std::make_unique<civcraft::llm::TtsVoiceMux>();
		if (!m_ttsMux->init()) {
			std::printf("[vk-game] no piper/voice found — NPC voice disabled. "
			            "Run 'make tts_setup' to enable.\n");
			m_ttsMux.reset();
		} else {
			auto names = m_ttsMux->voiceNames();
			std::string list;
			for (size_t i = 0; i < names.size(); ++i) {
				if (i) list += ", ";
				list += names[i];
			}
			std::printf("[vk-game] tts voices available: %s\n", list.c_str());
		}
	}
}


} // namespace civcraft::vk

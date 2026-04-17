#include "client/game_vk.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>

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
#include "logic/entity_physics.h"
#include "logic/action.h"
#include "logic/material_values.h"
// AgentClient now lives inside the player client (server stopped spawning
// civcraft-agent processes). Pulling these in lets civcraft-ui-vk drive
// every NPC the server hands us via in-process Python decide().
#include "agent/agent_client.h"
#include "server/behavior_store.h"
#include "server/python_bridge.h"

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
		// Refresh hotbar slot map on local-player inventory updates.
		// First update after a fresh session either restores the saved
		// layout from disk or populates by priority; subsequent updates
		// merge so drag-drop assignments survive pickups / drops. Save
		// right after the first populate so a fresh install still gets
		// a file on disk even if the user never drag-drops.
		if (eid == m_server->localPlayerId()) {
			if (!m_hotbarLoaded) {
				if (!m_hotbar.loadFromFile(m_hotbarSavePath))
					m_hotbar.repopulateFrom(*ent->inventory);
				m_hotbar.mergeFrom(*ent->inventory);
				m_hotbar.saveToFile(m_hotbarSavePath);
				m_hotbarLoaded = true;
			} else {
				m_hotbar.mergeFrom(*ent->inventory);
			}
		}
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
	});

	std::printf("[vk-game] chunks will stream from %s\n",
		m_server->isConnected() ? "network" : "???");

	if (!civcraft::pythonBridge().init("python"))
		std::printf("[vk-game] WARN: Python bridge init failed; NPCs won't decide()\n");
	m_behaviorStore = std::make_unique<civcraft::BehaviorStore>();
	m_behaviorStore->init("artifacts/behaviors");
	m_agentClient = std::make_unique<civcraft::AgentClient>(
		*m_server, *m_behaviorStore);
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
	std::printf("[vk-game] agent client up — Python decide() will run in-process\n");

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
		if (m_state == GameState::Playing) enterPaused();
		else if (m_state == GameState::Paused) resumeFromPause();
	});
	m_debugTriggers.addTrigger("/tmp/civcraft_vk_inventory_request", [this] {
		if (m_state != GameState::Playing) return;
		m_invOpen = !m_invOpen;
		if (!m_invOpen && m_chestUI.open) m_chestUI.open = false;
		std::printf("[vk-game] [trigger] inventory %s\n", m_invOpen ? "OPEN" : "CLOSED");
	});
	m_debugTriggers.addPayloadTrigger("/tmp/civcraft_vk_goto_request", [this](const std::string& line) {
		float tx = 0, ty = 0, tz = 0;
		if (sscanf(line.c_str(), "%f %f %f", &tx, &ty, &tz) >= 3) {
			glm::vec3 target(tx, ty, tz);
			m_server->sendSetGoal(m_server->localPlayerId(), target);
			m_hasMoveOrder    = true;
			m_moveOrderTarget = target;
			std::printf("[vk-game] [trigger] goto (%.1f,%.1f,%.1f)\n", tx, ty, tz);
		}
	});
	m_debugTriggers.addPayloadTrigger("/tmp/civcraft_vk_hotbar_request", [this](const std::string& line) {
		int slot = 0;
		if (sscanf(line.c_str(), "%d", &slot) == 1 && slot >= 0 && slot <= 9) {
			m_hotbarSlot = slot;
			std::printf("[vk-game] [trigger] hotbar slot → %d\n", slot);
		}
	});
	m_debugTriggers.addTrigger("/tmp/civcraft_vk_ascend_request", [this] {
		auto* me = playerEntity();
		if (m_flyMode && me) {
			me->position.y += 5.0f;
			std::printf("[vk-game] [trigger] ascend +5 → y=%.1f\n", me->position.y);
		}
	});
	m_artifactRegistry.loadAll("artifacts");

	// Load Python-defined BoxModels (one .py per creature/item under
	// artifacts/models/). Shared with GL — same data, same parser — so
	// sword has 14 parts, pig has legs, beaver has a tail, etc.
	m_models = civcraft::model_loader::loadAllModels("artifacts");
	std::printf("[vk-game] Loaded %zu box models\n", m_models.size());

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
	return true;
}

void Game::uploadChunkMesh(civcraft::ChunkPos cp) {
	civcraft::ChunkSource* src = &m_server->chunks();
	civcraft::ChunkMesher mesher;
	auto [opaque, transparent] = mesher.buildMesh(*src, cp);
	(void)transparent; // no glass/water streams yet

	auto it = m_chunkMeshes.find(cp);

	// Empty mesh — nothing to draw. Release any GPU buffer, but keep a
	// sentinel entry (kInvalidMesh) so Pass 1 won't re-discover this chunk
	// every frame. A later dirty event will overwrite the sentinel via
	// createChunkMesh once the chunk has real geometry again.
	if (opaque.empty()) {
		if (it != m_chunkMeshes.end() && it->second != rhi::IRhi::kInvalidMesh) {
			m_rhi->destroyMesh(it->second);
		}
		m_chunkMeshes[cp] = rhi::IRhi::kInvalidMesh;
		return;
	}

	// ChunkVertex is layout-compatible with the 13-float stream the VK
	// chunk pipeline expects (see chunk_mesher.h + rhi.h Chunk meshes).
	std::vector<float> scratch;
	scratch.reserve(opaque.size() * 13);
	for (const auto& v : opaque) {
		scratch.push_back(v.position.x); scratch.push_back(v.position.y); scratch.push_back(v.position.z);
		scratch.push_back(v.color.x);    scratch.push_back(v.color.y);    scratch.push_back(v.color.z);
		scratch.push_back(v.normal.x);   scratch.push_back(v.normal.y);   scratch.push_back(v.normal.z);
		scratch.push_back(v.ao);
		scratch.push_back(v.shade);
		scratch.push_back(v.alpha);
		scratch.push_back(v.glow);
	}
	uint32_t vc = (uint32_t)opaque.size();
	if (it == m_chunkMeshes.end() || it->second == rhi::IRhi::kInvalidMesh) {
		auto h = m_rhi->createChunkMesh(scratch.data(), vc);
		if (h != rhi::IRhi::kInvalidMesh) m_chunkMeshes[cp] = h;
	} else {
		m_rhi->updateChunkMesh(it->second, scratch.data(), vc);
	}
}

void Game::streamServerChunks() {
	// Sweep a render-radius box around the player's current chunk, asking
	// the server for chunks we haven't meshed yet. ChunkSource::getChunk
	// returns null for chunks the server hasn't delivered — those just get
	// retried next frame. Throttled so a fresh spawn doesn't stall.
	constexpr int kRenderChunkRadius = 6;    // ≈96 blocks horizontal
	constexpr int kRenderChunkVertical = 3;  // ±3 chunks in Y (48 blocks)
	constexpr int kMaxNewPerFrame = 12;
	constexpr int kMaxRemeshPerFrame = 6;

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
	pending.reserve(64);
	auto& src = m_server->chunks();
	for (int dy = -kRenderChunkVertical; dy <= kRenderChunkVertical; dy++)
		for (int dz = -kRenderChunkRadius; dz <= kRenderChunkRadius; dz++)
			for (int dx = -kRenderChunkRadius; dx <= kRenderChunkRadius; dx++) {
				civcraft::ChunkPos cp = {center.x + dx, center.y + dy, center.z + dz};
				if (m_chunkMeshes.count(cp)) continue;
				if (!src.getChunkIfLoaded(cp)) continue;  // server hasn't sent it yet
				pending.push_back({cp, dx*dx + dy*dy + dz*dz});
			}
	std::sort(pending.begin(), pending.end(),
		[](const Pending& a, const Pending& b) { return a.distSq < b.distSq; });

	int built = 0;
	for (auto& p : pending) {
		if (built >= kMaxNewPerFrame) break;
		uploadChunkMesh(p.cp);
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
		// Only re-mesh if already GPU-resident; otherwise Pass 1 will pick
		// it up in-range next frame.
		if (m_chunkMeshes.count(*it)) {
			uploadChunkMesh(*it);
			remeshed++;
		}
		it = m_serverDirtyChunks.erase(it);
	}
}

void Game::shutdown() {
	if (!m_rhi) return;
	// Persist hotbar layout so the player's drag-drop arrangement survives
	// across sessions. loadFromFile() on next start will pick this up.
	if (m_hotbarLoaded) m_hotbar.saveToFile(m_hotbarSavePath);
	// Tear down the agent client BEFORE main.cpp drops the NetworkServer:
	// AgentClient holds a reference to ServerInterface and its decide-worker
	// thread may still be in flight when we hit ~AgentClient.
	m_agentClient.reset();
	m_behaviorStore.reset();
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

void Game::enterMenu() {
	m_state = GameState::Menu;
	// Release mouse so the menu can be clicked with ImGui.
	if (m_window) {
		glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
	m_mouseCaptured = false;
}

void Game::enterPlaying() {
	m_state = GameState::Playing;
	// Reset client-only physics/animation state.
	m_onGround  = false;
	m_walkDist  = 0.0f;
	m_attackCD  = 0.0f;
	m_placeCD   = 0.0f;
	m_dropCD    = 0.0f;
	m_regenIdle = 0.0f;
	m_playerBodyYawInit = false;
	m_sprintFovBoost = 0.0f;
	m_modeHintsShown = 0;
	m_climb = {};
	m_coins = 0;
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
}

void Game::enterPaused() {
	if (m_state != GameState::Playing) return;
	m_state = GameState::Paused;
	// Release mouse so the pause overlay can be clicked.
	if (m_window) {
		glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
	m_mouseCaptured = false;
	// Reset camera smoothing so the view doesn't lurch when gameplay resumes
	// (orbit angles don't drift during pause, but the mouse-delta tracker
	// would otherwise emit a big jump on the first post-pause frame).
	m_cam.resetMouseTracking();
}

void Game::resumeFromPause() {
	if (m_state != GameState::Paused) return;
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

void Game::onWindowFocus(bool focused) {
	m_windowFocused = focused;
	if (!focused && m_state == GameState::Playing && !std::getenv("CIVCRAFT_NO_FOCUS_PAUSE"))
		enterPaused();
}

void Game::onScroll(double xoff, double yoff) {
	ImGuiIO& io = ImGui::GetIO();
	io.AddMouseWheelEvent((float)xoff, (float)yoff);
	if (io.WantCaptureMouse) return;
	if (m_state != GameState::Playing) return;
	float y = (float)yoff;
	switch (m_cam.mode) {
	case civcraft::CameraMode::FirstPerson: {
		// Hotbar cycle — same convention as GL client (Prop::SelectedSlot).
		if (auto* pe = playerEntity()) {
			int slot = pe->getProp<int>(civcraft::Prop::SelectedSlot, 0);
			slot = ((slot - (int)yoff) % 10 + 10) % 10;
			pe->setProp(civcraft::Prop::SelectedSlot, slot);
		}
		break;
	}
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
	m_menuTitleT += dt;

	int w = 0, h = 0;
	glfwGetFramebufferSize(m_window, &w, &h);
	m_fbW = w; m_fbH = h;
	m_aspect = h > 0 ? (float)w / (float)h : 1.0f;

	// Pump the network connection, stream chunks, run AI.
	m_server->tick(dt);
	streamServerChunks();
	if (m_agentClient && m_state == GameState::Playing)
		m_agentClient->tick(dt);

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

	// ── Sim ────────────────────────────────────────────────────────────
	processInput(dt);
	if (m_state == GameState::Playing) {
		tickPlayer(dt);
		tickCombat(dt);
		updatePickups(dt);
		tickFloaters(dt);
		if (m_chestUI.open) {
			civcraft::Entity* ce = m_server->getEntity(m_chestUI.chestEid);
			auto* chestMe = playerEntity();
			if (!ce || ce->removed) {
				m_chestUI.open = false;
			} else if (chestMe) {
				float d = glm::length(ce->position - chestMe->position);
				if (d > 6.0f)
					m_chestUI.open = false;
			}
		}
	} else if (m_state == GameState::Dead) {
		bool r = glfwGetKey(m_window, GLFW_KEY_R) == GLFW_PRESS;
		if (r) respawn();
	}

	// File-based debug triggers (compiled out in Release via NDEBUG).
	m_debugTriggers.poll();

	// ── Render ─────────────────────────────────────────────────────────
	if (!m_rhi->beginFrame()) return;

	if (m_state == GameState::Menu) {
		// Render a calm ambient backdrop so the menu isn't just a
		// solid rect — reuse the sky + a faraway camera orbit.
		float menuAng = m_menuTitleT * 0.08f;
		glm::vec3 menuFocus(std::sin(menuAng) * 2.0f, 7.0f, std::cos(menuAng) * 2.0f);
		// Manually drive camera for the menu backdrop — don't run the full
		// Camera::processInput (it would reset mouse-tracking every frame).
		float radius = kTune.camDistance * 1.2f;
		float pitch = 0.15f;
		float yaw   = menuAng + 3.14f * 0.5f;
		glm::vec3 head = menuFocus + glm::vec3(0, kTune.camHeight, 0);
		glm::vec3 dir(std::cos(pitch) * std::cos(yaw),
		              std::sin(pitch),
		              std::cos(pitch) * std::sin(yaw));
		m_cam.position = head - dir * radius;
		glm::vec3 look = glm::normalize(head - m_cam.position);
		m_cam.lookYaw   = glm::degrees(std::atan2(look.z, look.x));
		m_cam.lookPitch = glm::degrees(std::asin(look.y));
		renderWorld(wallTime);
		renderEffects(wallTime);
		m_rhi->imguiNewFrame();
		renderMenu();
		m_rhi->imguiRender();
	} else if (m_state == GameState::Playing) {
		renderWorld(wallTime);
		renderEntities(wallTime);
		renderEffects(wallTime);
		renderHotbarItems3D();  // 3D inventory icons — must precede HUD 2D
		m_rhi->imguiNewFrame();
		renderHUD();
		if (m_chestUI.open) renderChestUI();
		if (m_inspectedEntity != 0) renderEntityInspect();
		if (m_showDebug) renderDebugOverlay();
		if (m_showTuning) renderTuningPanel();
		if (m_handbookOpen) renderHandbook();
		renderRTSSelect();
		m_rhi->imguiRender();
	} else if (m_state == GameState::Paused) {
		renderWorld(wallTime);
		renderEntities(wallTime);
		renderEffects(wallTime);
		renderHotbarItems3D();
		m_rhi->imguiNewFrame();
		renderHUD();       // world state frozen underneath
		renderPaused();
		if (m_showTuning) renderTuningPanel();
		m_rhi->imguiRender();
	} else {  // Dead
		renderWorld(wallTime);
		renderEffects(wallTime);
		m_rhi->imguiNewFrame();
		renderHUD();       // still show world state behind the veil
		renderDeath();
		m_rhi->imguiRender();
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
}

} // namespace civcraft::vk

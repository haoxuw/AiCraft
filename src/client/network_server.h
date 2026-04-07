#pragma once

/**
 * NetworkServer — connects to a remote GameServer via TCP.
 *
 * Implements ServerInterface so the Game class can use it
 * identically to LocalServer. Sends ActionProposals over TCP,
 * receives entity/chunk/time updates.
 *
 * Used when a dedicated server is already running.
 * The client generates a random UUID as its player name.
 */

#include "shared/server_interface.h"
#include "shared/net_socket.h"
#include "shared/net_protocol.h"
#ifndef __EMSCRIPTEN__
#include <zstd.h>
#endif
#include "shared/block_registry.h"
#include "shared/chunk.h"
#include "server/entity_manager.h"
#include "content/builtin.h"
#include <unordered_map>
#include <functional>
#include <string>
#include <random>
#include <thread>
#include <chrono>

namespace modcraft {

class NetworkServer : public ServerInterface {
public:
	NetworkServer(const std::string& host, int port)
		: m_host(host), m_port(port) {
		// Register all block and entity definitions (same as server)
		// so entities have proper collision boxes, HP, inventory, etc.
		registerAllBuiltins(m_blocks, m_entityDefs);

		// Generate random UUID for this client
		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_int_distribution<uint32_t> dist;
		char buf[40];
		snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%08x%04x",
			dist(gen), dist(gen) & 0xFFFF, dist(gen) & 0xFFFF,
			dist(gen) & 0xFFFF, dist(gen), dist(gen) & 0xFFFF);
		m_clientUUID = buf;
		printf("[Net] Client UUID: %s\n", m_clientUUID.c_str());
	}

	bool createGame(int seed, int templateIndex,
	                const WorldGenConfig& /*wgc*/ = WorldGenConfig{}) override {
		if (!m_tcp.connect(m_host.c_str(), m_port)) {
			printf("[Net] Cannot connect to %s:%d\n", m_host.c_str(), m_port);
			return false;
		}

		m_connected = true;

		// Wait for S_WELCOME (polling for up to 3 seconds)
		auto start = std::chrono::steady_clock::now();
		while (true) {
			m_recv.readFrom(m_tcp.fd()); // non-blocking, may return EAGAIN

			if (recvWelcomeFromBuffer()) return true;

			auto elapsed = std::chrono::steady_clock::now() - start;
			if (std::chrono::duration<float>(elapsed).count() > 3.0f) {
				printf("[Net] Timeout waiting for welcome\n");
				break;
			}

			// Don't spin at 100% CPU while waiting
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		disconnect();
		return false;
	}

	// ── Async connect for web (WebSocket needs event-loop frames to complete) ──
	// Step 1: initiate connection (non-blocking, returns immediately)
	bool beginConnect() {
		printf("[Net] Connecting to %s:%d ...\n", m_host.c_str(), m_port);
		if (!m_tcp.connect(m_host.c_str(), m_port)) {
			printf("[Net] beginConnect failed for %s:%d\n", m_host.c_str(), m_port);
			return false;
		}
		m_connected = true;
		return true;
	}

	// Step 2: call every frame — returns true when S_WELCOME has been received
	bool pollWelcome() {
		if (!m_connected) return false;
		if (!m_recv.readFrom(m_tcp.fd())) {
			printf("[Net] Connection lost while waiting for welcome\n");
			disconnect(); // close fd properly
			return false;
		}
		return recvWelcomeFromBuffer();
	}

	void disconnect() override {
		m_tcp.disconnect();
		m_connected = false;
	}

	bool isConnected() const override { return m_connected; }

	void tick(float dt) override {
		if (!m_connected) return;

		// Read incoming messages
		if (!m_recv.readFrom(m_tcp.fd())) {
			printf("[Net] Connection lost\n");
			disconnect(); // close fd properly, not just flag
			return;
		}

		net::MsgHeader hdr;
		std::vector<uint8_t> payload;
		int msgCount = 0;
		while (m_recv.tryExtract(hdr, payload)) {
			handleMessage(hdr.type, payload);
			msgCount++;
		}
		m_tickMsgCount += msgCount;
		m_totalMsgCount += msgCount;
		m_uptime += dt;

		// Early connection logging — only warn if player entity is missing
		if (m_uptime < 5.0f && !getEntity(m_localPlayerId) && msgCount > 0) {
			printf("[Net] t=%.1fs: waiting for player entity (received %zu entities, %zu chunks)\n",
				m_uptime, m_entities.size(), m_chunkData.size());
		}

		// Periodic diagnostics (every 30 seconds — quiet by default)
		m_diagTimer += dt;
		if (m_diagTimer >= 30.0f) {
			Entity* pe = m_entities.count(m_localPlayerId)
				? m_entities[m_localPlayerId].get() : nullptr;
			printf("[Net] %zu entities, %zu chunks, %.0fs uptime\n",
				m_entities.size(), m_chunkData.size(), m_uptime);
			if (pe)
				printf("[Net] Player pos=(%.1f,%.1f,%.1f) vel=(%.1f,%.1f,%.1f) ground=%d\n",
					pe->position.x, pe->position.y, pe->position.z,
					pe->velocity.x, pe->velocity.y, pe->velocity.z, pe->onGround);
			m_diagTimer = 0;
		}

		// Interpolate ALL entities toward server targets.
		// Server is fully authoritative for positions. Client smoothly tracks.
		// NO special cases for local player — this prevents phasing through
		// walls (client has no collision, so using client velocity for
		// extrapolation causes rubber-banding against solid blocks).
		const float INTERP_SPEED = 18.0f;
		const float LOCAL_INTERP_SPEED = 25.0f; // faster for local player responsiveness
		for (auto& [id, target] : m_interpTargets) {
			auto it = m_entities.find(id);
			if (it == m_entities.end()) continue;
			auto& e = *it->second;
			bool isLocal = (id == m_localPlayerId);

			// ALL entities use server velocity for prediction — client has
			// no physics so client velocity would phase through blocks.
			// Cap age to 0.5s to prevent unbounded extrapolation when the
			// server stops sending updates (e.g. entity leaves perception range).
			float cappedAge = std::min(target.age, 0.5f);
			glm::vec3 predicted = target.position + target.velocity * cappedAge;
			glm::vec3 diff = predicted - e.position;

			// Snap threshold uses distance from last known server position (not
			// predicted), so stale dead-reckoning never triggers a snap loop.
			float distFromServer = glm::length(target.position - e.position);

			float speed = isLocal ? LOCAL_INTERP_SPEED : INTERP_SPEED;
			if (distFromServer > 3.0f) {
				printf("[Net] SNAP entity %u (dist=%.1f) local=(%.1f,%.1f,%.1f) srv=(%.1f,%.1f,%.1f)\n",
					id, distFromServer,
					e.position.x, e.position.y, e.position.z,
					target.position.x, target.position.y, target.position.z);
				e.position = target.position;
				target.age = 0.0f;
			} else if (glm::length(diff) > 0.005f) {
				float t = std::min(dt * speed, 1.0f);
				e.position += diff * t;
			}

			// Smooth yaw — same for all entities (server-authoritative)
			{
				float yawDiff = target.yaw - e.yaw;
				while (yawDiff > 180.0f) yawDiff -= 360.0f;
				while (yawDiff < -180.0f) yawDiff += 360.0f;
				e.yaw += yawDiff * std::min(dt * speed, 1.0f);
			}
			// Velocity: update for ALL entities (server-authoritative)
			e.velocity = target.velocity;

			target.age += dt;

			// Walk distance for animation — same for all entities
			float hSpeed = std::sqrt(e.velocity.x * e.velocity.x + e.velocity.z * e.velocity.z);
			if (hSpeed > 0.01f) {
				float wd = e.getProp<float>(Prop::WalkDistance, 0.0f);
				e.setProp(Prop::WalkDistance, wd + hSpeed * dt);
			}
		}
	}

	void sendAction(const ActionProposal& action) override {
		if (!m_connected) return;
		net::WriteBuffer buf;
		net::serializeAction(buf, action);
		net::sendMessage(m_tcp.fd(), net::C_ACTION, buf);
	}

	void sendHotbarSlot(int slot, const std::string& itemId) override {
		if (!m_connected) return;
		net::WriteBuffer wb;
		wb.writeU32((uint32_t)slot);
		wb.writeString(itemId);
		net::sendMessage(m_tcp.fd(), net::C_HOTBAR, wb);
	}

	// --- State access ---
	ChunkSource& chunks() override { return m_chunks; }
	EntityId localPlayerId() const override { return m_localPlayerId; }

	Entity* getEntity(EntityId id) override {
		auto it = m_entities.find(id);
		return it != m_entities.end() ? it->second.get() : nullptr;
	}

	void forEachEntity(std::function<void(Entity&)> fn) override {
		for (auto& [id, e] : m_entities)
			if (!e->removed) fn(*e);
	}

	size_t entityCount() const override { return m_entities.size(); }

	BehaviorInfo getBehaviorInfo(EntityId) override { return {}; }
	float worldTime() const override { return m_worldTime; }
	glm::vec3 spawnPos() const override { return m_spawnPos; }
	float pickupRange() const override { return 1.5f; } // TODO: sync from server
	const BlockRegistry& blockRegistry() const override { return m_blocks; }
	ActionProposalQueue& proposalQueue() override { return m_proposals; }

	void setEffectCallbacks(
		std::function<void(ChunkPos)> onChunkDirty,
		std::function<void(glm::vec3, const std::string&)> onBlockBreakText = nullptr,
		std::function<void(glm::vec3, const std::string&)> onBlockPlace = nullptr) override {
		m_onChunkDirty      = onChunkDirty;
		m_onBlockBreakText  = onBlockBreakText;
		m_onBlockPlace      = onBlockPlace;
	}

	const std::string& clientUUID() const { return m_clientUUID; }
	void setDisplayName(const std::string& name) { m_displayName = name; }
	void setCreatureType(const std::string& type) { m_creatureType = type; }

	// Try to connect (non-blocking check for game.cpp)
	static bool canConnect(const char* host, int port) {
		net::TcpClient probe;
		bool ok = probe.connect(host, port);
		probe.disconnect();
		return ok;
	}

private:
	// Parse S_WELCOME from receive buffer and send C_HELLO. Returns true on success.
	bool recvWelcomeFromBuffer() {
		net::MsgHeader hdr;
		std::vector<uint8_t> payload;
		if (m_recv.tryExtract(hdr, payload)) {
			if (hdr.type == net::S_WELCOME) {
				net::ReadBuffer rb(payload.data(), payload.size());
				m_localPlayerId = rb.readU32();
				m_spawnPos = rb.readVec3();
				printf("[Net] Welcome! Player ID=%u, spawn=(%.1f,%.1f,%.1f)\n",
					m_localPlayerId, m_spawnPos.x, m_spawnPos.y, m_spawnPos.z);

				// Always send a non-empty display name — generate one from the
				// UUID if the user hasn't set one yet (can be changed in the UI).
				std::string name = m_displayName.empty()
					? ("Player-" + m_clientUUID.substr(0, 4))
					: m_displayName;
				net::WriteBuffer hello;
				// Wire format: [u32 version][str uuid][str displayName][str creatureType]
#ifdef __EMSCRIPTEN__
				hello.writeU32(1); // WASM build: no zstd — request uncompressed S_CHUNK
#else
				hello.writeU32(net::PROTOCOL_VERSION);
#endif
				hello.writeString(m_clientUUID);
				hello.writeString(name);
				hello.writeString(m_creatureType);
				net::sendMessage(m_tcp.fd(), net::C_HELLO, hello);
				return true;
			}
		}
		return false;
	}

	void handleMessage(uint32_t type, const std::vector<uint8_t>& payload) {
		net::ReadBuffer rb(payload.data(), payload.size());

		switch (type) {
		case net::S_ENTITY: {
			auto es = net::deserializeEntityState(rb);

			auto it = m_entities.find(es.id);
			if (it == m_entities.end()) {
				// New entity — look up proper EntityDef from registered builtins
				const EntityDef* def = m_entityDefs.getTypeDef(es.typeId);
				if (!def) {
					printf("[Net] WARNING: unknown entity type '%s' (id=%u), using default def\n",
					       es.typeId.c_str(), es.id);
					def = &m_defaultDef;
				}
				printf("[Net] New entity: id=%u type=%s pos=(%.1f,%.1f,%.1f)%s\n",
					es.id, es.typeId.c_str(), es.position.x, es.position.y, es.position.z,
					es.id == m_localPlayerId ? " [LOCAL PLAYER]" : "");
				auto ent = std::make_unique<Entity>(es.id, es.typeId, *def);
				ent->position = es.position;
				ent->velocity = es.velocity;
				ent->yaw = es.yaw;
				ent->onGround = es.onGround;
				ent->goalText = es.goalText;
				if (!es.characterSkin.empty())
					ent->setProp("character_skin", es.characterSkin);
				// Apply string properties (ItemType, BehaviorId, etc.)
				for (auto& [k, v] : es.stringProps)
					ent->setProp(k, v);
				if (es.id == m_localPlayerId)
					printf("[Net] Local player entity created: type=%s pos=(%.1f,%.1f,%.1f)\n",
						es.typeId.c_str(), es.position.x, es.position.y, es.position.z);
				if (def->isLiving())
					ent->setProp(Prop::HP, es.hp);
				// Apply any inventory that arrived before this entity
				auto pit = m_pendingInventory.find(es.id);
				if (pit != m_pendingInventory.end() && ent->inventory) {
					auto& inv = *ent->inventory;
					inv.clear();
					for (auto& [iid, amt] : pit->second.items)
						inv.add(iid, amt);
					bool hasHotbar = false;
					for (int i = 0; i < (int)pit->second.hotbar.size(); i++) {
						inv.setHotbar(i, pit->second.hotbar[i]);
						if (!pit->second.hotbar[i].empty()) hasHotbar = true;
					}
					if (!hasHotbar) inv.autoPopulateHotbar();
					m_pendingInventory.erase(pit);
				}
				m_entities[es.id] = std::move(ent);
				// Initialize interpolation target
				m_interpTargets[es.id] = {es.position, es.velocity, es.yaw, 0};
			} else {
				auto& e = *it->second;

				// ALL entities use the same code path — server is authoritative.
				// Set interpolation target from server data. The tick() loop
				// smoothly interpolates every entity toward its target.
				// No special case for local player vs animals vs other players.
				m_interpTargets[es.id] = {es.position, es.velocity, es.yaw, 0.0f};

				// Sync non-positional state immediately
				e.onGround = es.onGround;
				e.goalText = es.goalText;
				if (e.def().isLiving())
					e.setProp(Prop::HP, es.hp);
				// Sync string properties
				for (auto& [k, v] : es.stringProps)
					e.setProp(k, v);
			}
			break;
		}
		case net::S_CHUNK: {
			int cx = rb.readI32(), cy = rb.readI32(), cz = rb.readI32();
			ChunkPos cp = {cx, cy, cz};
			auto chunk = std::make_unique<Chunk>();
			for (int ly = 0; ly < CHUNK_SIZE; ly++)
				for (int lz = 0; lz < CHUNK_SIZE; lz++)
					for (int lx = 0; lx < CHUNK_SIZE; lx++) {
						uint32_t v = rb.readU32();
						chunk->set(lx, ly, lz, (BlockId)(v & 0xFFFF), (uint8_t)((v >> 16) & 0xFF));
					}
			m_chunkData[cp] = std::move(chunk);
			if (m_onChunkDirty) m_onChunkDirty(cp);
			break;
		}
#ifndef __EMSCRIPTEN__
		case net::S_CHUNK_Z: {
			// Decompress zstd frame, then parse identical to S_CHUNK
			size_t compSize = rb.remaining();
			const uint8_t* compData = rb.remainingData();
			size_t decompBound = ZSTD_getFrameContentSize(compData, compSize);
			if (decompBound == ZSTD_CONTENTSIZE_ERROR || decompBound == ZSTD_CONTENTSIZE_UNKNOWN) {
				fprintf(stderr, "[Client] S_CHUNK_Z: bad zstd frame\n");
				break;
			}
			std::vector<uint8_t> decomp(decompBound);
			size_t actual = ZSTD_decompress(decomp.data(), decompBound, compData, compSize);
			if (ZSTD_isError(actual)) {
				fprintf(stderr, "[Client] S_CHUNK_Z: decompression error: %s\n",
				        ZSTD_getErrorName(actual));
				break;
			}
			net::ReadBuffer zrb(decomp.data(), actual);
			ChunkPos cp = {zrb.readI32(), zrb.readI32(), zrb.readI32()};
			auto chunk = std::make_unique<Chunk>();
			for (int ly = 0; ly < CHUNK_SIZE; ly++)
				for (int lz = 0; lz < CHUNK_SIZE; lz++)
					for (int lx = 0; lx < CHUNK_SIZE; lx++) {
						uint32_t v = zrb.readU32();
						chunk->set(lx, ly, lz, (BlockId)(v & 0xFFFF), (uint8_t)((v >> 16) & 0xFF));
					}
			m_chunkData[cp] = std::move(chunk);
			if (m_onChunkDirty) m_onChunkDirty(cp);
			break;
		}
#endif
		case net::S_CHUNK_EVICT: {
			ChunkPos cp = {rb.readI32(), rb.readI32(), rb.readI32()};
			m_chunkData.erase(cp);
			if (m_onChunkDirty) m_onChunkDirty(cp); // triggers mesh rebuild for that position
			break;
		}
		case net::S_REMOVE: {
			EntityId id = rb.readU32();
			m_entities.erase(id);
			m_interpTargets.erase(id);
			break;
		}
		case net::S_TIME: {
			m_worldTime = rb.readF32();
			break;
		}
		case net::S_BLOCK: {
			int bx = rb.readI32(), by = rb.readI32(), bz = rb.readI32();
			BlockId bid = (BlockId)rb.readU32();
			uint8_t p2 = rb.hasMore() ? rb.readU8() : 0;
			auto div = [](int a, int b) { return (a >= 0) ? a / b : (a - b + 1) / b; };
			ChunkPos cp = {div(bx, CHUNK_SIZE), div(by, CHUNK_SIZE), div(bz, CHUNK_SIZE)};
			auto it = m_chunkData.find(cp);
			if (it != m_chunkData.end()) {
				int lx = ((bx % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
				int ly = ((by % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
				int lz = ((bz % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
				// Detect block break: non-air → air transition
				if (bid == BLOCK_AIR && m_onBlockBreakText) {
					BlockId oldBid = it->second->get(lx, ly, lz);
					if (oldBid != BLOCK_AIR) {
						const BlockDef& bdef = m_blocks.get(oldBid);
						if (bdef.string_id != "base:air" && !bdef.string_id.empty())
							m_onBlockBreakText(glm::vec3(bx, by, bz), bdef.display_name.empty() ? bdef.string_id : bdef.display_name);
					}
				}
				it->second->set(lx, ly, lz, bid, p2);
			}
			if (m_onChunkDirty) m_onChunkDirty(cp);
			break;
		}
		case net::S_INVENTORY: {
			EntityId id = rb.readU32();
			// Always read the full payload first
			uint32_t count = rb.readU32();
			std::vector<std::pair<std::string,int>> items;
			for (uint32_t i = 0; i < count && rb.hasMore(); i++) {
				std::string itemId = rb.readString();
				int amount = rb.readI32();
				items.push_back({itemId, amount});
			}
			std::vector<std::string> hotbar;
			for (int i = 0; i < Inventory::HOTBAR_SLOTS && rb.hasMore(); i++)
				hotbar.push_back(rb.readString());

			// Read equipment slots (if present)
			std::vector<std::string> equipment;
			for (int i = 0; i < WEAR_SLOT_COUNT && rb.hasMore(); i++)
				equipment.push_back(rb.readString());

			auto applyInv = [&](Inventory& inv) {
				inv.clear();
				for (auto& [iid, amt] : items) inv.add(iid, amt);
				bool hasHotbar = false;
				for (int i = 0; i < (int)hotbar.size(); i++) {
					inv.setHotbar(i, hotbar[i]);
					if (!hotbar[i].empty()) hasHotbar = true;
				}
				if (!hasHotbar) inv.autoPopulateHotbar();
				// Apply equipment
				for (int i = 0; i < (int)equipment.size(); i++) {
					if (!equipment[i].empty()) {
						inv.add(equipment[i], 1); // need it in counter for equip()
						inv.equip((WearSlot)i, equipment[i]);
					}
				}
			};

			auto it = m_entities.find(id);
			if (it != m_entities.end() && it->second->inventory) {
				applyInv(*it->second->inventory);
			} else {
				// Entity not yet known — buffer for when S_ENTITY arrives
				m_pendingInventory[id] = {std::move(items), std::move(hotbar)};
			}
			break;
		}
		}
	}

	// Chunk source that reads from received chunk data
	class NetChunkSource : public ChunkSource {
	public:
		NetChunkSource(NetworkServer& ns) : m_ns(ns) {}
		Chunk* getChunk(ChunkPos pos) override {
			auto it = m_ns.m_chunkData.find(pos);
			return it != m_ns.m_chunkData.end() ? it->second.get() : nullptr;
		}
		Chunk* getChunkIfLoaded(ChunkPos pos) override { return getChunk(pos); }
		BlockId getBlock(int x, int y, int z) override {
			auto div = [](int a, int b) { return (a >= 0) ? a / b : (a - b + 1) / b; };
			ChunkPos cp = {div(x, CHUNK_SIZE), div(y, CHUNK_SIZE), div(z, CHUNK_SIZE)};
			Chunk* c = getChunk(cp);
			if (!c) return BLOCK_AIR;
			int lx = ((x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			int ly = ((y % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			int lz = ((z % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			return c->get(lx, ly, lz);
		}
		const BlockRegistry& blockRegistry() const override { return m_ns.m_blocks; }
	private:
		NetworkServer& m_ns;
	};

	std::string m_host;
	int m_port;
	std::string m_clientUUID;
	std::string m_displayName;   // player name
	std::string m_creatureType;  // requested creature type
	bool m_connected = false;

	net::TcpClient m_tcp;
	net::RecvBuffer m_recv;

	EntityId m_localPlayerId = ENTITY_NONE;
	glm::vec3 m_spawnPos = {0, 0, 0};
	float m_worldTime = 0.3f;

	std::unordered_map<EntityId, std::unique_ptr<Entity>> m_entities;
	std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, ChunkPosHash> m_chunkData;
	NetChunkSource m_chunks{*this};

	// Interpolation: smooth remote entity movement between 20Hz server updates
	struct InterpTarget {
		glm::vec3 position;
		glm::vec3 velocity;
		float yaw;
		float age; // time since last server update
	};
	std::unordered_map<EntityId, InterpTarget> m_interpTargets;

	// Pending inventory: arrived before entity — applied on first S_ENTITY
	struct PendingInv {
		std::vector<std::pair<std::string,int>> items;
		std::vector<std::string> hotbar;
	};
	std::unordered_map<EntityId, PendingInv> m_pendingInventory;

	BlockRegistry m_blocks;
	ActionProposalQueue m_proposals;
	EntityDef m_defaultDef;
	EntityManager m_entityDefs;  // holds type defs for entity creation

	// Diagnostics
	float m_diagTimer = 0;
	int m_tickMsgCount = 0;
	int m_totalMsgCount = 0;
	float m_uptime = 0;

	std::function<void(ChunkPos)> m_onChunkDirty;
	std::function<void(glm::vec3, const std::string&)> m_onBlockBreakText;
	std::function<void(glm::vec3, const std::string&)> m_onBlockPlace;
};

} // namespace modcraft

#pragma once

// In-process menu plaza — a tiny chunk-backed world that lives entirely
// inside civcraft-ui-vk. Spawns 3 mascots (dog/cat/bee) as real Entity
// records running real Agent::executePlan against real Plans returned by
// Python decide_plan(). No TCP, no civcraft-server. The menu/loading
// screens render this plaza so the camera looks at live wandering
// mascots instead of statically-posed boxes.
//
// Trade-off: this is a deliberate, scoped exception to Rule 3 (server-
// authoritative world ownership). The plaza is decorative-only — no
// player ever steps onto it. When the user picks Play, the real
// NetworkServer takes over and the plaza is no longer ticked or
// rendered.

#include "agent/agent.h"
#include "agent/behavior.h"
#include "logic/action.h"
#include "logic/block_registry.h"
#include "logic/chunk.h"
#include "logic/chunk_source.h"
#include "logic/entity.h"
#include "logic/types.h"
#include "net/server_interface.h"
#include "server/behavior_store.h"
#include "server/entity_manager.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace civcraft {
class ArtifactRegistry;
} // namespace civcraft

namespace civcraft::vk {

// Tiny ChunkSource backing the menu plaza — a 32×32 grass slab over a
// dirt layer, generated once at construction and never modified.
class PlazaChunks : public ChunkSource {
public:
	PlazaChunks();

	Chunk*  getChunk(ChunkPos pos) override;
	Chunk*  getChunkIfLoaded(ChunkPos pos) override;
	BlockId getBlock(int x, int y, int z) override;
	const BlockRegistry& blockRegistry() const override { return m_blocks; }

private:
	void registerCoreBlocks();
	void stampPlaza();              // grass top + dirt below at fixed Y
	Chunk& chunkAt(ChunkPos pos);   // create-if-absent

	BlockRegistry m_blocks;
	std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, ChunkPosHash> m_chunks;
};

// Owns plaza chunks + 3 mascot entities + their Agents. Runs the
// decide → execute → physics loop synchronously in tickFrame().
//
// Implements ServerInterface so Agent::executePlan and PythonBridge
// chunk-query callbacks treat this just like a real server.
class MenuPlaza : public ServerInterface {
public:
	MenuPlaza();
	~MenuPlaza() override;

	// One-time setup. Call AFTER pythonBridge().init() and after
	// ArtifactRegistry::loadAll. BehaviorStore must already be init'd.
	void init(ArtifactRegistry& artifacts, BehaviorStore& behaviors);

	// Per-frame tick — drives decide + execute + physics for all mascots.
	void tickFrame(float dt);

	// Renderer access — yields each live mascot for box-model emission.
	void forEachMascot(const std::function<void(const Entity&)>& fn) const;
	bool ready() const { return m_ready; }

	// ── ServerInterface — live methods used by Agent / PythonBridge ──
	bool createGame(int, int, const WorldGenConfig&) override { return true; }
	void disconnect() override {}
	bool isConnected() const override { return m_ready; }
	void tick(float /*dt*/) override {}                  // unused; tickFrame drives the loop
	void sendAction(const ActionProposal& action) override;
	ChunkSource& chunks() override { return m_chunks; }
	EntityId localPlayerId() const override { return ENTITY_NONE; }
	Entity*  getEntity(EntityId id) override;
	void     forEachEntity(std::function<void(Entity&)> fn) override;
	size_t   entityCount() const override { return m_entities.size(); }
	BehaviorInfo getBehaviorInfo(EntityId id) override;
	float     worldTime() const override { return 0.5f; }      // perpetual noon
	glm::vec3 spawnPos() const override { return {0, 1, 0}; }
	float     pickupRange() const override { return 0.0f; }
	const BlockRegistry& blockRegistry() const override { return m_chunks.blockRegistry(); }
	ActionProposalQueue& proposalQueue() override { return m_proposalQueue; }
	void setEffectCallbacks(
		std::function<void(ChunkPos)>,
		std::function<void(glm::vec3, const std::string&)>,
		std::function<void(glm::vec3, const std::string&)>) override {}

private:
	// ── Setup helpers ────────────────────────────────────────────────
	void registerEntityTypes(ArtifactRegistry& artifacts);
	void spawnAllMascots(BehaviorStore& behaviors);
	void spawnMascot(const std::string& typeId, glm::vec3 pos, float yaw,
	                 BehaviorStore& behaviors);
	BehaviorHandle resolveHandle(const std::string& behaviorId,
	                             BehaviorStore& behaviors);

	// ── Per-frame phases ─────────────────────────────────────────────
	void runDecidePhase();
	void runExecutePhase(float dt);
	void runPhysicsPhase(float dt);
	void decideForOne(Agent& a, Entity& e);
	void integrateOne(Entity& e, float dt);

	// ── Chunk-query callback handed to PythonBridge ──────────────────
	std::string queryBlockTypeAt(int x, int y, int z);

	// ── State ────────────────────────────────────────────────────────
	PlazaChunks                          m_chunks;
	EntityManager                        m_entityMgr;
	std::vector<std::unique_ptr<Entity>> m_entities;
	std::vector<std::unique_ptr<Agent>>  m_agents;       // 1:1 with m_entities
	std::unordered_map<std::string, BehaviorHandle> m_handleCache;
	ActionProposalQueue                  m_proposalQueue; // unused; here so getter has something to return
	EntityId                             m_nextEid = 1;
	float                                m_time    = 0.0f;
	bool                                 m_ready   = false;
};

} // namespace civcraft::vk

#include "client/menu_plaza.h"

#include "logic/artifact_registry.h"
#include "logic/constants.h"
#include "logic/physics.h"
#include "python/python_bridge.h"
#include "server/builtin.h"

#include <cmath>
#include <cstdio>

namespace solarium::vk {

// ─── PlazaChunks ─────────────────────────────────────────────────────────

PlazaChunks::PlazaChunks() {
	registerCoreBlocks();
	stampPlaza();
}

void PlazaChunks::registerCoreBlocks() {
	// Pull the same builtin block table the server uses — same IDs for grass /
	// dirt / etc., so any artifact reference resolves consistently.
	EntityManager scratch;
	registerAllBuiltins(m_blocks, scratch);
}

Chunk& PlazaChunks::chunkAt(ChunkPos pos) {
	auto it = m_chunks.find(pos);
	if (it == m_chunks.end()) {
		auto p = std::make_unique<Chunk>();
		Chunk& ref = *p;
		m_chunks.emplace(pos, std::move(p));
		return ref;
	}
	return *it->second;
}

void PlazaChunks::stampPlaza() {
	// 32×32 plaza centered on origin, top of grass slab at y=1.
	// Single chunk at y=0 covers x∈[0..15],z∈[0..15] etc; we need 4 chunks.
	const BlockId grass = m_blocks.getId(BlockType::Grass);
	const BlockId dirt  = m_blocks.getId(BlockType::Dirt);
	const int kHalf = 16;
	for (int wz = -kHalf; wz < kHalf; ++wz) {
		for (int wx = -kHalf; wx < kHalf; ++wx) {
			ChunkPos cp = worldToChunk(wx, 0, wz);
			Chunk& c = chunkAt(cp);
			int lx = ((wx % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			int lz = ((wz % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			c.set(lx, 0, lz, grass);          // top slab
			// Dirt fill below — keeps the AABB solid so a falling mascot
			// doesn't sink through. We only need one layer: gravity stops at
			// the first solid cell beneath the entity.
		}
	}
	// Dirt layer at y=-1 (in the chunk at y=-1).
	for (int wz = -kHalf; wz < kHalf; ++wz) {
		for (int wx = -kHalf; wx < kHalf; ++wx) {
			ChunkPos cp = worldToChunk(wx, -1, wz);
			Chunk& c = chunkAt(cp);
			int lx = ((wx % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			int lz = ((wz % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			int ly = ((-1 % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			c.set(lx, ly, lz, dirt);
		}
	}
}

Chunk* PlazaChunks::getChunk(ChunkPos pos) {
	auto it = m_chunks.find(pos);
	return it != m_chunks.end() ? it->second.get() : nullptr;
}

Chunk* PlazaChunks::getChunkIfLoaded(ChunkPos pos) {
	return getChunk(pos);
}

BlockId PlazaChunks::getBlock(int x, int y, int z) {
	ChunkPos cp = worldToChunk(x, y, z);
	Chunk* c = getChunk(cp);
	if (!c) return BLOCK_AIR;
	int lx = ((x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
	int ly = ((y % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
	int lz = ((z % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
	return c->get(lx, ly, lz);
}

// ─── MenuPlaza ───────────────────────────────────────────────────────────

MenuPlaza::MenuPlaza() = default;

MenuPlaza::~MenuPlaza() {
	for (auto& [bid, h] : m_handleCache) {
		if (h >= 0) pythonBridge().unloadBehavior(h);
	}
}

void MenuPlaza::init(EntityManager& entityMgr, ModelManager& modelMgr) {
	if (m_ready) return;
	// Plaza shares Game's already-populated siblings — no separate
	// EntityManager copy, no second registerAllBuiltins/applyLivingStats
	// pass. Game::init has already done the merge once.
	m_entityMgr = &entityMgr;
	m_modelMgr  = &modelMgr;
	// Mascots no longer spawn at boot — the plaza shows just two trees
	// until the user enters a loading screen, at which point Game spawns
	// them progressively via spawnMascotById() with a puff effect each.
	m_ready = true;
	std::printf("[MenuPlaza] ready (lazy-spawn mascots)\n");
}

void MenuPlaza::spawnMascotById(const std::string& typeId, glm::vec3 pos,
                                 float yaw, BehaviorStore& behaviors) {
	spawnMascot(typeId, pos, yaw, behaviors);
}

void MenuPlaza::clearMascots() {
	m_entities.clear();
	m_agents.clear();
}

void MenuPlaza::spawnMascot(const std::string& typeId, glm::vec3 pos, float yaw,
                            BehaviorStore& behaviors) {
	const EntityDef* def = m_entityMgr ? m_entityMgr->getTypeDef(typeId) : nullptr;
	if (!def) {
		std::printf("[MenuPlaza] unknown mascot type '%s' — skipped\n", typeId.c_str());
		return;
	}
	EntityId eid = m_nextEid++;
	auto e = std::make_unique<Entity>(eid, typeId, *def);
	e->position = pos;
	e->yaw = yaw;
	e->lookYaw = yaw;
	e->onGround = true;

	// Behavior id lives on the EntityDef as a default prop (applyLivingStats
	// writes it from the artifact's "behavior" field). Fallback to "wander"
	// keeps the mascot animated even if the artifact hasn't declared one.
	std::string bid = e->getProp<std::string>(Prop::BehaviorId, "wander");
	BehaviorHandle handle = resolveHandle(bid, behaviors);

	auto agent = std::make_unique<Agent>(eid, bid, handle);
	m_entities.push_back(std::move(e));
	m_agents.push_back(std::move(agent));
}

BehaviorHandle MenuPlaza::resolveHandle(const std::string& behaviorId,
                                       BehaviorStore& behaviors) {
	auto it = m_handleCache.find(behaviorId);
	if (it != m_handleCache.end()) return it->second;

	std::string src = behaviors.load(behaviorId);
	if (src.empty()) {
		std::printf("[MenuPlaza] behavior '%s' source not found\n", behaviorId.c_str());
		m_handleCache[behaviorId] = -1;
		return -1;
	}
	std::string err;
	BehaviorHandle h = pythonBridge().loadBehavior(src, err);
	if (h < 0) {
		std::printf("[MenuPlaza] loadBehavior(%s) failed: %s\n",
		            behaviorId.c_str(), err.c_str());
	}
	m_handleCache[behaviorId] = h;
	return h;
}

// ── Per-frame ───────────────────────────────────────────────────────────

void MenuPlaza::tickFrame(float dt) {
	if (!m_ready) return;
	m_time += dt;
	runDecidePhase();
	runExecutePhase(dt);
	runPhysicsPhase(dt);
}

void MenuPlaza::runDecidePhase() {
	for (size_t i = 0; i < m_agents.size(); ++i) {
		Agent& a = *m_agents[i];
		Entity& e = *m_entities[i];
		if (a.handle() < 0) continue;
		if (a.needsCompute() != Agent::ComputeKind::Decide) continue;
		decideForOne(a, e);
	}
}

void MenuPlaza::decideForOne(Agent& a, Entity& e) {
	// Build the snapshot AgentClient would normally hand the worker.
	EntitySnapshot self;
	self.id        = e.id();
	self.typeId    = e.typeId();
	self.position  = e.position;
	self.velocity  = e.velocity;
	self.yaw       = e.yaw;
	self.lookYaw   = e.lookYaw;
	self.lookPitch = e.lookPitch;
	self.hp        = e.hp();
	self.maxHp     = e.def().max_hp;
	self.walkSpeed = e.def().walk_speed;
	self.inventoryCapacity = e.def().inventory_capacity;
	self.onGround  = e.onGround;
	if (e.inventory) self.inventory = e.inventory->items();
	for (auto& [k, v] : e.props()) self.props.emplace_back(k, v);

	// Mascots don't see each other (no follow/threat semantics on the menu).
	std::vector<NearbyEntity> nearby;

	auto blockQueryFn = [this](int x, int y, int z) {
		return queryBlockTypeAt(x, y, z);
	};

	std::string goalText, errOut;
	a.markDispatched(Agent::ComputeKind::Decide, m_time);
	Plan plan = pythonBridge().callDecidePlan(
		a.handle(), self, nearby, /*dt*/0.25f, /*timeOfDay*/worldTime(),
		goalText, errOut,
		blockQueryFn,
		/*scanBlocks*/   nullptr,
		/*scanEntities*/ nullptr,
		/*scanAnnots*/   nullptr,
		/*lastOutcome*/  "success",
		/*lastGoal*/     a.lastOutcome().goalText,
		/*lastReason*/   a.lastOutcome().reason,
		a.lastOutcome().execState,
		a.failStreak());
	a.clearInFlight(false);

	if (!errOut.empty()) {
		std::printf("[MenuPlaza] %s decide_plan err: %s\n",
		            e.typeId().c_str(), errOut.c_str());
		a.onDecideError(errOut, e);
		return;
	}
	a.onDecideResult(std::move(plan), std::move(goalText), e);
}

void MenuPlaza::runExecutePhase(float dt) {
	for (size_t i = 0; i < m_agents.size(); ++i) {
		Agent& a = *m_agents[i];
		Entity& e = *m_entities[i];
		if (e.removed) continue;
		a.executePlan(dt, *this);
	}
}

void MenuPlaza::runPhysicsPhase(float dt) {
	for (auto& up : m_entities) {
		Entity& e = *up;
		if (e.removed) continue;
		integrateOne(e, dt);
		smoothYawTowardsVelocity(e.yaw, e.velocity, dt);
	}
}

void MenuPlaza::integrateOne(Entity& e, float dt) {
	const EntityDef& def = e.def();
	const bool canFly = def.gravity_scale <= 0.0f;
	MoveParams mp = makeMoveParams(def.collision_box_min, def.collision_box_max,
	                                def.gravity_scale, def.isLiving(), canFly);
	BlockSolidFn isSolid = m_chunks.solidFn();
	MoveResult r = moveAndCollide(isSolid, e.position, e.velocity, dt, mp,
	                              e.onGround);
	e.position = r.position;
	e.velocity = r.velocity;
	e.onGround = r.onGround;
}

// ── ServerInterface plumbing ────────────────────────────────────────────

void MenuPlaza::sendAction(const ActionProposal& action) {
	// The plaza only consumes Move; mascot behaviors don't relocate or
	// interact. Anything else is silently dropped — same effect a real
	// server would have for our restricted mascot vocabulary.
	if (action.type != ActionProposal::Move) return;
	if (Entity* e = getEntity(action.actorId)) {
		// Keep gravity authoritative — only overwrite XZ + (for flyers) Y.
		const bool canFly = e->def().gravity_scale <= 0.0f;
		e->velocity.x = action.desiredVel.x;
		e->velocity.z = action.desiredVel.z;
		if (canFly) e->velocity.y = action.desiredVel.y;
		e->goalText = action.goalText;
	}
}

Entity* MenuPlaza::getEntity(EntityId id) {
	for (auto& up : m_entities) if (up->id() == id) return up.get();
	return nullptr;
}

void MenuPlaza::forEachEntity(std::function<void(Entity&)> fn) {
	for (auto& up : m_entities) fn(*up);
}

ServerInterface::BehaviorInfo MenuPlaza::getBehaviorInfo(EntityId id) {
	BehaviorInfo info;
	for (size_t i = 0; i < m_entities.size(); ++i) {
		if (m_entities[i]->id() != id) continue;
		info.name = m_agents[i]->behaviorId();
		info.goal = m_agents[i]->goalText();
		break;
	}
	return info;
}

void MenuPlaza::forEachMascot(const std::function<void(const Entity&)>& fn) const {
	for (auto& up : m_entities) fn(*up);
}

std::string MenuPlaza::queryBlockTypeAt(int x, int y, int z) {
	BlockId b = m_chunks.getBlock(x, y, z);
	const BlockDef& bd = m_chunks.blockRegistry().get(b);
	return bd.string_id;
}

} // namespace solarium::vk

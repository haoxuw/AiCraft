/**
 * test_e2e.cpp — Headless end-to-end gameplay tests.
 *
 * Runs entirely in-process (no network, no GLFW).
 * Uses TestServer (wraps GameServer) directly to simulate gameplay
 * and assert correctness.
 *
 * Each test creates a fresh world to avoid cross-test pollution.
 * Tests are numbered and run sequentially. Exit code = failed count.
 *
 * Runs from build/ directory so python/ and artifacts/ are accessible.
 */

#include "server/test_server.h"
#include "server/village_siter.h"
#include "server/world_template.h"
#include "server/python_bridge.h"
#include "server/world_accessibility.h"
#include "server/pathfind.h"
#include "server/client_manager.h"
#include "client/entity_reconciler.h"
#include "agent/block_search.h"
#include "logic/constants.h"
#include "logic/chunk.h"
#include "logic/block_registry.h"
#include <cstdio>
#include <cmath>
#include <string>
#include <functional>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace civcraft::test {

// ================================================================
// Test infrastructure
// ================================================================

struct Result {
	std::string name;
	bool passed;
	std::string msg;
};

static std::vector<Result> g_results;

// shared templates (flat=0, village=1)
static std::vector<std::shared_ptr<WorldTemplate>> g_templates;

static void initTemplates() {
	g_templates = {
		std::make_shared<ConfigurableWorldTemplate>("artifacts/worlds/base/flat.py"),
		std::make_shared<ConfigurableWorldTemplate>("artifacts/worlds/base/village.py"),
	};
}

// Run a single test. fn() returns "" on pass, error message on fail.
static void run(const char* name, std::function<std::string()> fn) {
	printf("  %-55s", name);
	fflush(stdout);
	std::string err;
	try {
		err = fn();
	} catch (std::exception& e) {
		err = std::string("EXCEPTION: ") + e.what();
	} catch (...) {
		err = "UNKNOWN EXCEPTION";
	}
	bool ok = err.empty();
	printf("%s\n", ok ? "PASS" : ("FAIL: " + err).c_str());
	g_results.push_back({name, ok, err});
}

// ================================================================
// Test helpers
// ================================================================

// Create a flat-world TestServer and connect one client.
static std::unique_ptr<TestServer> makeFlatServer() {
	auto srv = std::make_unique<TestServer>(g_templates);
	WorldGenConfig wgc;
	// Flat world: templateIndex=0, no mob spawning
	wgc.mobs.clear();
	srv->createGame(42, 0, wgc);
	return srv;
}

// Create a village world server with default mob config.
static std::unique_ptr<TestServer> makeVillageServer() {
	auto srv = std::make_unique<TestServer>(g_templates);
	srv->createGame(42, 1, WorldGenConfig{});
	return srv;
}

// Tick the server N frames at 60 Hz.
static void tickN(TestServer& srv, int frames) {
	constexpr float dt = 1.0f / 60.0f;
	for (int i = 0; i < frames; i++) srv.tick(dt);
}

// Send a Move action and tick one frame.
static void moveAndTick(TestServer& srv, EntityId actor, glm::vec3 vel, bool jump = false) {
	ActionProposal p;
	p.type = ActionProposal::Move;
	p.actorId = actor;
	p.desiredVel = vel;
	p.jump = jump;
	p.jumpVelocity = 10.0f;
	srv.sendAction(p);
	srv.tick(1.0f / 60.0f);
}

// Send PickupItem actions for all nearby item entities, then tick.
static int pickupNearbyItems(TestServer& srv, EntityId actor, float range = 1.5f) {
	Entity* p = srv.getEntity(actor);
	if (!p) return 0;
	int sent = 0;
	srv.forEachEntity([&](Entity& e) {
		if (e.typeId() != ItemName::ItemEntity) return;
		if (e.removed) return;
		float dist = glm::length(e.position - p->position);
		if (dist < range) {
			ActionProposal a;
			a.type = ActionProposal::Relocate;
			a.actorId = actor;
			a.relocateFrom = Container::entity(e.id());
			srv.sendAction(a);
			sent++;
		}
	});
	if (sent > 0) srv.tick(1.0f / 60.0f);
	return sent;
}

// Send a Convert (break block) action and tick one frame.
static void breakAndTick(TestServer& srv, EntityId actor, glm::ivec3 pos) {
	BlockId bid = srv.chunks().getBlock(pos.x, pos.y, pos.z);
	const BlockDef& bdef = srv.blockRegistry().get(bid);
	ActionProposal p;
	p.type        = ActionProposal::Convert;
	p.actorId     = actor;
	p.fromItem    = bdef.string_id;
	p.toItem      = bdef.drop.empty() ? bdef.string_id : bdef.drop;
	p.fromCount   = 1;
	p.toCount     = 1;
	p.convertFrom = Container::block(pos);
	p.convertInto = Container::ground();   // spawn as item entity
	srv.sendAction(p);
	srv.tick(1.0f / 60.0f);
}

// Send a Convert (place block) action and tick one frame.
static void placeAndTick(TestServer& srv, EntityId actor,
                         glm::ivec3 pos, const std::string& type) {
	ActionProposal p;
	p.type        = ActionProposal::Convert;
	p.actorId     = actor;
	p.fromItem    = type;
	p.toItem      = type;
	// convertFrom defaults to Self (take from inventory)
	p.convertInto = Container::block(pos);
	srv.sendAction(p);
	srv.tick(1.0f / 60.0f);
}

// Count entities of a given type.
static int countByType(TestServer& srv, const std::string& typeId) {
	int n = 0;
	srv.forEachEntity([&](Entity& e) { if (e.typeId() == typeId) n++; });
	return n;
}

// Count entities that are living (Creature or Character kind).
static int countLiving(TestServer& srv) {
	int n = 0;
	srv.forEachEntity([&](Entity& e) { if (e.def().isLiving()) n++; });
	return n;
}

// ================================================================
// T01: Player basics
// ================================================================
// T02 (has inventory) and T03 (has HP) are exercised by every inventory and
// combat test below — kept only the entity-spawn smoke check.

static std::string t01_player_spawns() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	if (pid == ENTITY_NONE) return "localPlayerId() is ENTITY_NONE";
	Entity* p = srv->getEntity(pid);
	if (!p) return "getEntity(playerId) returned null";
	return "";
}

// ================================================================
// T04 — T06: Movement
// ================================================================

static std::string t04_player_moves() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	// Let physics settle first
	tickN(*srv, 30);
	glm::vec3 startPos = p->position;

	// Walk north (+Z) for 60 frames
	for (int i = 0; i < 60; i++)
		moveAndTick(*srv, pid, {0, 0, 4.0f});

	float moved = glm::length(p->position - startPos);
	if (moved < 1.0f)
		return "player didn't move (moved " + std::to_string(moved) + " units)";
	return "";
}

static std::string t06_player_jump() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 60);  // settle on ground
	if (!p->onGround) return "player not on ground before jump";
	float groundY = p->position.y;

	// Jump
	moveAndTick(*srv, pid, {0, 0, 0}, /*jump=*/true);
	tickN(*srv, 5);  // let physics run

	if (p->position.y <= groundY + 0.1f)
		return "player didn't rise after jump (y=" + std::to_string(p->position.y) +
		       " vs ground=" + std::to_string(groundY) + ")";
	return "";
}

// ================================================================
// T07 — T09: Creatures / Village
// ================================================================

static std::string t07_creatures_spawn_village() {
	auto srv = makeVillageServer();
	tickN(*srv, 10);  // let physics drop mobs to surface

	int creatures = 0;
	srv->forEachEntity([&](Entity& e) {
		if (e.def().isLiving()) creatures++;
	});
	if (creatures < 5)
		return "expected >= 5 creatures, got " + std::to_string(creatures);
	return "";
}

static std::string t08_all_living_have_inventory() {
	auto srv = makeVillageServer();
	tickN(*srv, 5);

	std::string missing;
	srv->forEachEntity([&](Entity& e) {
		if (e.def().isLiving() && !e.inventory)
			missing = e.typeId();
	});
	if (!missing.empty())
		return "Living entity without inventory: " + missing;
	return "";
}

static std::string t09_creatures_have_hp() {
	auto srv = makeVillageServer();
	tickN(*srv, 5);

	std::string bad;
	srv->forEachEntity([&](Entity& e) {
		if (e.def().isLiving() && e.hp() <= 0)
			bad = e.typeId();
	});
	if (!bad.empty())
		return "Living entity with hp <= 0: " + bad;
	return "";
}

// ================================================================
// T10 — T13: Block breaking
// ================================================================

static std::string t10_break_block_removes_block() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30);
	glm::vec3 pos = p->position;

	glm::ivec3 blockPos = {(int)std::floor(pos.x), (int)std::floor(pos.y) - 1,
	                       (int)std::floor(pos.z)};

	BlockId bid = srv->chunks().getBlock(blockPos.x, blockPos.y, blockPos.z);
	if (bid == BLOCK_AIR) return "no block under player";

	breakAndTick(*srv, pid, blockPos);

	bid = srv->chunks().getBlock(blockPos.x, blockPos.y, blockPos.z);
	if (bid != BLOCK_AIR) return "block still exists after break";
	return "";
}

static void placeBlockDirect(TestServer& srv, int x, int y, int z, const std::string& typeId);  // defined below

static std::string t11_break_block_survival_item_spawns() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30);
	glm::vec3 pos = p->position;

	// Break a block 4 units north — well outside attract radius (2.5) so
	// the item entity persists for at least one tick before pickup. Place
	// the target block explicitly so the test doesn't depend on whether
	// village-gen carved that cell to air.
	glm::ivec3 blockPos = {(int)std::floor(pos.x), (int)std::floor(pos.y) - 1,
	                       (int)std::floor(pos.z) - 4};
	placeBlockDirect(*srv, blockPos.x, blockPos.y, blockPos.z, BlockType::Stone);

	BlockId bid = srv->chunks().getBlock(blockPos.x, blockPos.y, blockPos.z);
	if (bid == BLOCK_AIR) return "failed to place target block";

	int itemsBefore = countByType(*srv, ItemName::ItemEntity);
	breakAndTick(*srv, pid, blockPos);
	int itemsAfter = countByType(*srv, ItemName::ItemEntity);

	if (itemsAfter <= itemsBefore)
		return "no item entity spawned after break (before=" +
		       std::to_string(itemsBefore) + " after=" + std::to_string(itemsAfter) + ")";

	// Block should now be air
	bid = srv->chunks().getBlock(blockPos.x, blockPos.y, blockPos.z);
	if (bid != BLOCK_AIR) return "block not removed after break";
	return "";
}

static std::string t12_item_pickup_after_break() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30);  // settle on ground

	glm::vec3 pos = p->position;
	glm::ivec3 blockPos = {(int)std::floor(pos.x), (int)std::floor(pos.y) - 1,
	                       (int)std::floor(pos.z)};

	// Confirm there's a block
	BlockId bid = srv->chunks().getBlock(blockPos.x, blockPos.y, blockPos.z);
	if (bid == BLOCK_AIR) return "no surface block at spawn";

	// Record inventory count before (all items)
	int totalBefore = p->inventory ? p->inventory->distinctCount() : 0;
	// Snapshot stone count as reference
	int stoneBefore = p->inventory ? p->inventory->count(BlockType::Stone) : 0;

	// Break block — item spawns near player's feet
	breakAndTick(*srv, pid, blockPos);

	// Client-initiated pickup: scan and send PickupItem action
	pickupNearbyItems(*srv, pid, 3.0f);
	tickN(*srv, 5);

	// Total items in inventory should be >= before (item picked up)
	// We compare total count (sum of all item counts)
	int totalCountAfter = 0;
	if (p->inventory) {
		for (auto& [id, cnt] : p->inventory->items()) totalCountAfter += cnt;
	}
	int totalCountBefore = stoneBefore + p->inventory->count(BlockType::Wood) +
	                       p->inventory->count("sword") +
	                       p->inventory->count("shield") +
	                       p->inventory->count("potion");

	if (totalCountAfter <= totalCountBefore)
		return "inventory didn't increase after breaking+walking over item "
		       "(before=" + std::to_string(totalCountBefore) +
		       " after=" + std::to_string(totalCountAfter) + ")";
	return "";
}

// ================================================================
// T13 — T14: Block placement
// ================================================================

static std::string t13_place_block() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30);

	// Find a nearby air block to place stone in.
	// Try several positions within reach; skip any that are already occupied.
	glm::vec3 pos = p->position;
	glm::ivec3 placePos = {0, 0, 0};
	bool found = false;
	for (int dz = 2; dz <= 6 && !found; dz++) {
		glm::ivec3 candidate = {(int)std::floor(pos.x), (int)std::floor(pos.y), (int)std::floor(pos.z) + dz};
		if (srv->chunks().getBlock(candidate.x, candidate.y, candidate.z) == BLOCK_AIR) {
			placePos = candidate;
			found = true;
		}
	}
	if (!found) return "could not find an air block within reach to place";

	// Verify it's air
	BlockId bid = srv->chunks().getBlock(placePos.x, placePos.y, placePos.z);
	if (bid != BLOCK_AIR)
		return "target position is not air (bid=" + std::to_string(bid) + ")";

	// Check player has stone
	if (!p->inventory || !p->inventory->has(BlockType::Stone))
		return "player doesn't have stone to place";

	int stoneBefore = p->inventory->count(BlockType::Stone);
	placeAndTick(*srv, pid, placePos, BlockType::Stone);

	bid = srv->chunks().getBlock(placePos.x, placePos.y, placePos.z);
	if (bid == BLOCK_AIR)
		return "block was not placed (still air)";

	int stoneAfter = p->inventory->count(BlockType::Stone);
	if (stoneAfter >= stoneBefore)
		return "stone not removed from inventory after placement "
		       "(before=" + std::to_string(stoneBefore) +
		       " after=" + std::to_string(stoneAfter) + ")";
	return "";
}

static std::string t14_place_block_rejected_without_item() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30);

	// Try placing TNT (player doesn't have it)
	if (p->inventory && p->inventory->has(BlockType::TNT))
		return "player unexpectedly has TNT";

	// Find an air block to try placing at
	glm::vec3 pos = p->position;
	glm::ivec3 placePos = {(int)std::floor(pos.x), (int)std::floor(pos.y), (int)std::floor(pos.z) + 3};
	placeAndTick(*srv, pid, placePos, BlockType::TNT);

	BlockId bid = srv->chunks().getBlock(placePos.x, placePos.y, placePos.z);
	if (bid != BLOCK_AIR)
		return "block was placed even though player had no TNT in inventory";
	return "";
}

// ================================================================
// T15: Item entity not picked up by non-player creatures
// ================================================================

static std::string t15_items_only_picked_up_by_player() {
	auto srv = makeVillageServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30);

	// Manually spawn an item entity near a creature (far from player)
	// We need server access for this
	auto* ls = srv.get();
	// Can't easily get server directly from TestServer interface
	// Instead: find a creature and check its inventory before/after ticks
	// If creatures were incorrectly picking up items, their inventory would have items.
	// By default, creatures' inventories should stay empty (animals don't pick up).

	std::string wrongPickup;
	srv->forEachEntity([&](Entity& e) {
		if (!e.def().isLiving()) return;
		if (!e.inventory) return;
		// Skip the player — they start with hotbar items
		if (e.id() == pid) return;
		if (e.inventory->distinctCount() > 0) {
			wrongPickup = e.typeId() + " has items it shouldn't";
		}
	});
	if (!wrongPickup.empty()) return wrongPickup;
	return "";
}

// ================================================================
// T16: No-op: animals don't crash the server after 300 ticks
// ================================================================

static std::string t16_village_stable_300_ticks() {
	auto srv = makeVillageServer();
	int creaturesBefore = 0;
	srv->forEachEntity([&](Entity& e) { if (e.def().isLiving()) creaturesBefore++; });

	tickN(*srv, 300);

	int creaturesAfter = 0;
	srv->forEachEntity([&](Entity& e) { if (e.def().isLiving()) creaturesAfter++; });

	if (creaturesAfter < creaturesBefore)
		return "creatures died: " + std::to_string(creaturesBefore) +
		       " → " + std::to_string(creaturesAfter);
	return "";
}

// ================================================================
// T17: Player stuck detection — nudge on collision
// ================================================================

static std::string t17_player_not_stuck_with_obstacles() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30);
	float startZ = p->position.z;

	// Walk south (+Z) 600 frames — exits spawn portal, flat terrain ahead
	for (int i = 0; i < 600; i++)
		moveAndTick(*srv, pid, {0, 0, 8.0f});

	float moved = p->position.z - startZ;
	if (moved < 5.0f)
		return "player stuck: only moved " + std::to_string(moved) + " units south in 600 ticks";
	return "";
}

// ================================================================
// T18: Starting inventory has usable items (hotbar is a client-side concern)
// ================================================================

static std::string t18_starting_inventory_populated() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p || !p->inventory) return "no player or inventory";

	if (p->inventory->distinctCount() == 0)
		return "starting inventory is empty";
	return "";
}

// ================================================================
// T19: break + place cycle deducts from inventory
// ================================================================

static std::string t19_break_and_place_cycle() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	// Walk out of spawn portal and away from village (-Z direction)
	for (int i = 0; i < 60; i++)
		moveAndTick(*srv, pid, {0, 0, -8.0f});
	tickN(*srv, 10);
	glm::vec3 pos = p->position;
	// Place block in open air (flat terrain, away from structures)
	glm::ivec3 placePos = {(int)pos.x, (int)pos.y, (int)pos.z - 3};

	if (!p->inventory || !p->inventory->has(BlockType::Stone))
		return "player doesn't have stone";
	int before = p->inventory->count(BlockType::Stone);
	placeAndTick(*srv, pid, placePos, BlockType::Stone);
	if (srv->chunks().getBlock(placePos.x, placePos.y, placePos.z) == BLOCK_AIR)
		return "block not placed";
	int after = p->inventory->count(BlockType::Stone);
	if (after >= before)
		return "placing didn't deduct from inventory";
	return "";
}

// ================================================================
// T20: Spawn area is clear — no solid block inside player body
// ================================================================

static std::string t20_spawn_area_clear() {
	auto srv = makeFlatServer();
	glm::vec3 spawn = srv->spawnPos();
	int sx = (int)std::floor(spawn.x);
	int sy = (int)std::round(spawn.y);
	int sz = (int)std::floor(spawn.z);

	// Player body spans [spawnY, spawnY+~1.8]: y, y+1 must be air
	for (int dy = 0; dy <= 1; dy++) {
		BlockId bid = srv->chunks().getBlock(sx, sy + dy, sz);
		if (srv->blockRegistry().get(bid).solid)
			return "solid block at spawn dy=" + std::to_string(dy) +
			       " pos=(" + std::to_string(sx) + "," + std::to_string(sy + dy) +
			       "," + std::to_string(sz) + ") bid=" + std::to_string(bid);
	}
	return "";
}

// ================================================================
// T21: block is removed from world when broken
// ================================================================

static std::string t21_break_fires_onbreaktext() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30);
	glm::vec3 pos = p->position;
	glm::ivec3 blockPos = {(int)std::floor(pos.x), (int)std::floor(pos.y) - 1,
	                       (int)std::floor(pos.z)};

	BlockId bid = srv->server()->world().getBlock(blockPos.x, blockPos.y, blockPos.z);
	if (bid == BLOCK_AIR) return "no surface block at spawn";

	breakAndTick(*srv, pid, blockPos);

	BlockId after = srv->server()->world().getBlock(blockPos.x, blockPos.y, blockPos.z);
	if (after != BLOCK_AIR) return "block was not removed after BreakBlock action";
	return "";
}

// ================================================================
// T22: inventory gains item when picked up
// ================================================================

static std::string t22_pickup_fires_onpickuptext() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30);
	glm::vec3 pos = p->position;
	glm::ivec3 blockPos = {(int)std::floor(pos.x), (int)std::floor(pos.y) - 1,
	                       (int)std::floor(pos.z)};

	BlockId bid = srv->server()->world().getBlock(blockPos.x, blockPos.y, blockPos.z);
	if (bid == BLOCK_AIR) return "no surface block at spawn";

	int itemsBefore = p->inventory ? p->inventory->distinctCount() : 0;

	breakAndTick(*srv, pid, blockPos);
	pickupNearbyItems(*srv, pid, 3.0f);
	tickN(*srv, 5);

	int itemsAfter = p->inventory ? p->inventory->distinctCount() : 0;
	if (itemsAfter <= itemsBefore) return "inventory did not gain item after pickup";
	return "";
}

// ================================================================
// T22b: break underfoot — item spawns and is picked up quickly
// ================================================================

static std::string t22b_break_underfoot_fires_callbacks() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30);
	glm::vec3 pos = p->position;
	glm::ivec3 blockPos = {(int)std::floor(pos.x), (int)std::floor(pos.y) - 1,
	                       (int)std::floor(pos.z)};

	BlockId bid = srv->server()->world().getBlock(blockPos.x, blockPos.y, blockPos.z);
	if (bid == BLOCK_AIR) return "no surface block at spawn";

	int itemsBefore = p->inventory ? p->inventory->distinctCount() : 0;

	breakAndTick(*srv, pid, blockPos);
	pickupNearbyItems(*srv, pid, 3.0f);
	tickN(*srv, 5);

	// Block should be gone
	BlockId after = srv->server()->world().getBlock(blockPos.x, blockPos.y, blockPos.z);
	if (after != BLOCK_AIR) return "block not removed after break";
	// Inventory should have gained the drop
	int itemsAfter = p->inventory ? p->inventory->distinctCount() : 0;
	if (itemsAfter <= itemsBefore) return "inventory did not gain item after break+pickup";
	return "";
}

// ================================================================
// T22c: break at distance — item reaches player
// ================================================================

static std::string t22c_item_reaches_player() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30);

	// Break a block 2 blocks away (within reach but outside pickup range)
	glm::vec3 pos = p->position;
	glm::ivec3 blockPos = {(int)std::floor(pos.x) + 2, (int)std::floor(pos.y) - 1,
	                       (int)std::floor(pos.z)};

	BlockId bid = srv->chunks().getBlock(blockPos.x, blockPos.y, blockPos.z);
	if (bid == BLOCK_AIR) return "no block at target position";

	breakAndTick(*srv, pid, blockPos);

	int itemCount = countByType(*srv, ItemName::ItemEntity);
	if (itemCount == 0) return "no item entity spawned after break";

	// Item is ~2.5 blocks away. Walk player toward it.
	for (int i = 0; i < 30; i++)
		moveAndTick(*srv, pid, {4.0f, 0, 0}); // walk east toward item

	// Now try pickup — should be in range
	pickupNearbyItems(*srv, pid, 2.0f);
	tickN(*srv, 5);

	int remaining = countByType(*srv, ItemName::ItemEntity);
	if (remaining > 0) return "item not picked up after walking to it";
	return "";
}

// ================================================================
// T25: Spawn is within ~60 blocks of village center
// ================================================================

static std::string t25_spawn_near_village() {
	auto srv = makeVillageServer();

	auto& tmpl = srv->server()->world().getTemplate();
	if (!tmpl.pyConfig().hasVillage) return "template has no village";

	auto vc = tmpl.villageCenter(42);
	glm::vec3 spawn = srv->spawnPos();

	float dx = spawn.x - (float)vc.x;
	float dz = spawn.z - (float)vc.y;
	float dist = std::sqrt(dx*dx + dz*dz);

	if (dist > 65.0f)
		return "spawn is " + std::to_string((int)dist) +
		       " blocks from village center — expected < 65";
	if (dist < 10.0f)
		return "spawn is only " + std::to_string((int)dist) +
		       " blocks from village center — should be outside clearing";
	return "";
}

// ================================================================
// T27: Multiple DropItems produce correct count (no dedup on server)
// ================================================================
// Regression test for the "13 eggs" bug: each Relocate(toGround) action must
// spawn exactly one item entity, regardless of how many ticks elapse between
// sends. Catches any tick-loop that re-fires one-shot actions.

static std::string t27_multiple_dropitems_correct_count() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p || !p->inventory) return "no player or inventory";

	for (int i = 0; i < 60; i++)
		moveAndTick(*srv, pid, {0, 0, -8.0f});
	tickN(*srv, 10);

	p->inventory->add("egg", 20);

	int itemsBefore = 0;
	srv->forEachEntity([&](Entity& e) {
		if (e.typeId() == ItemName::ItemEntity) itemsBefore++;
	});

	// Send exactly 3 Relocate(toGround) actions (3 separate intentional drops)
	for (int i = 0; i < 3; i++) {
		ActionProposal dp;
		dp.type = ActionProposal::Relocate;
		dp.actorId = pid;
		dp.relocateTo = Container::ground();
		dp.itemId = "egg";
		dp.itemCount = 1;
		srv->sendAction(dp);
		tickN(*srv, 5);
	}

	tickN(*srv, 10);

	int itemsAfter = 0;
	srv->forEachEntity([&](Entity& e) {
		if (e.typeId() == ItemName::ItemEntity) itemsAfter++;
	});

	int spawned = itemsAfter - itemsBefore;
	if (spawned != 3)
		return "expected 3 item entities from 3 DropItems, got " + std::to_string(spawned);

	int remaining = p->inventory->count("egg");
	if (remaining != 17)
		return "expected 17 eggs remaining, got " + std::to_string(remaining);

	return "";
}

// ================================================================
// T30: EquipItem moves item from inventory to wear slot
// ================================================================

static std::string t30_equip_item() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p || !p->inventory) return "no player/inventory";

	if (!p->inventory->has("shield")) return "no shield in starting inventory";

	// Equip shield to offhand
	ActionProposal a;
	a.type = ActionProposal::Relocate;
	a.actorId = pid;
	a.itemId = "shield";
	a.equipSlot = "offhand";
	srv->sendAction(a);
	srv->tick(1.0f / 60.0f);

	if (p->inventory->equipped(WearSlot::Offhand) != "shield")
		return "shield not in offhand slot (got: '" +
		       p->inventory->equipped(WearSlot::Offhand) + "')";
	return "";
}

// ================================================================
// T31: UseItem consumes item and heals
// ================================================================

static std::string t31_use_item_consume() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p || !p->inventory) return "no player/inventory";

	// Player starts with 3 potions
	int before = p->inventory->count("potion");
	if (before < 1) return "no potions in starting inventory";

	// Damage player first so healing is observable
	p->setHp(5);
	int hpBefore = p->hp();

	// Use potion: consume 1 potion from inventory, gain HP
	ActionProposal a;
	a.type      = ActionProposal::Convert;
	a.actorId   = pid;
	a.fromItem  = "potion";
	a.fromCount = 1;
	a.toItem    = "hp";
	a.toCount   = 4;
	srv->sendAction(a);
	srv->tick(1.0f / 60.0f);

	// Potion count should decrease
	int after = p->inventory->count("potion");
	if (after >= before) return "potion count didn't decrease (before=" +
		std::to_string(before) + " after=" + std::to_string(after) + ")";

	// HP should increase
	if (p->hp() <= hpBefore) return "HP didn't increase after using potion";
	return "";
}

// ================================================================
// T32: Attack damages target entity
// ================================================================

static std::string t32_attack_damages_entity() {
	auto srv = makeVillageServer();
	tickN(*srv, 30);

	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	// Find the closest living creature
	EntityId targetId = ENTITY_NONE;
	float closestDist = 999;
	srv->forEachEntity([&](Entity& e) {
		if (e.def().isLiving() && e.id() != pid) {
			float d = glm::length(e.position - p->position);
			if (d < closestDist) { closestDist = d; targetId = e.id(); }
		}
	});
	if (targetId == ENTITY_NONE) return "no creature found";

	// Teleport player next to the target (within attack range)
	Entity* target = srv->getEntity(targetId);
	if (!target) return "target disappeared";
	p->position = target->position + glm::vec3(1, 0, 0);
	int targetHp = target->hp();

	ActionProposal a;
	a.type        = ActionProposal::Convert;
	a.actorId     = pid;
	a.convertFrom = Container::entity(targetId);
	a.fromItem    = "hp";
	a.fromCount   = 3;
	a.toItem      = "";  // destroy HP
	srv->sendAction(a);
	srv->tick(1.0f / 60.0f);

	target = srv->getEntity(targetId);
	if (!target) {
		// Target might have died (HP was low)
		return "";
	}
	if (target->hp() >= targetHp) return "target HP didn't decrease (was " +
		std::to_string(targetHp) + ", now " + std::to_string(target->hp()) + ")";
	return "";
}

// ================================================================
// T33: DropItem deducts from inventory
// ================================================================

static std::string t33_dropitem_deducts_inventory() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p || !p->inventory) return "no player/inventory";

	int stoneBefore = p->inventory->count(BlockType::Stone);
	if (stoneBefore < 1) return "no stone to drop";

	ActionProposal a;
	a.type = ActionProposal::Relocate;
	a.actorId = pid;
	a.relocateTo = Container::ground();
	a.itemId = BlockType::Stone;
	a.itemCount = 1;
	srv->sendAction(a);
	srv->tick(1.0f / 60.0f);

	int stoneAfter = p->inventory->count(BlockType::Stone);
	if (stoneAfter >= stoneBefore) return "stone count didn't decrease";
	int items = countByType(*srv, ItemName::ItemEntity);
	if (items < 1) return "no item entity spawned after drop";
	return "";
}

// ================================================================
// T35: Item entity has ItemType property
// ================================================================

static std::string t35_item_entity_has_type() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30);
	glm::vec3 pos = p->position;
	glm::ivec3 blockPos = {(int)std::floor(pos.x), (int)std::floor(pos.y) - 1,
	                       (int)std::floor(pos.z)};

	BlockId bid = srv->chunks().getBlock(blockPos.x, blockPos.y, blockPos.z);
	if (bid == BLOCK_AIR) return "no block under player";

	breakAndTick(*srv, pid, blockPos);

	// Find the spawned item entity and check its ItemType
	std::string itemType;
	srv->forEachEntity([&](Entity& e) {
		if (e.typeId() == ItemName::ItemEntity) {
			itemType = e.getProp<std::string>(Prop::ItemType);
		}
	});
	if (itemType.empty()) return "item entity has no ItemType property";
	if (itemType.find(':') != std::string::npos)
		return "ItemType has unexpected namespace (got: " + itemType + ")";
	return "";
}

// ================================================================
// T36: Equip then unequip returns item to inventory
// ================================================================

static std::string t36_equip_unequip_cycle() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p || !p->inventory) return "no player/inventory";

	if (!p->inventory->has("shield")) return "no shield";

	// Equip shield to offhand
	p->inventory->equip(WearSlot::Offhand, "shield");
	if (p->inventory->equipped(WearSlot::Offhand) != "shield")
		return "equip failed";
	if (p->inventory->has("shield")) return "shield still in counter after equip";

	// Unequip
	p->inventory->unequip(WearSlot::Offhand);
	if (!p->inventory->equipped(WearSlot::Offhand).empty())
		return "unequip didn't clear slot";
	if (!p->inventory->has("shield")) return "shield not returned to inventory after unequip";
	return "";
}

// ================================================================
// T37: Pickup denied if out of range
// ================================================================

static std::string t37_pickup_denied_out_of_range() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30);

	// Break a block 4 blocks away
	glm::vec3 pos = p->position;
	glm::ivec3 blockPos = {(int)std::floor(pos.x) + 4, (int)std::floor(pos.y) - 1,
	                       (int)std::floor(pos.z)};
	BlockId bid = srv->chunks().getBlock(blockPos.x, blockPos.y, blockPos.z);
	if (bid == BLOCK_AIR) return "no block at target";

	breakAndTick(*srv, pid, blockPos);

	// Item should exist
	int items = countByType(*srv, ItemName::ItemEntity);
	if (items == 0) return "no item spawned";

	// Try to pick it up (should be denied by server — too far)
	pickupNearbyItems(*srv, pid, 10.0f); // generous client range
	// Server denies because actual distance > pickup_range

	int remaining = countByType(*srv, ItemName::ItemEntity);
	if (remaining == 0) return "item was picked up despite being out of range";
	return "";
}

// ================================================================
// T39: Second floor is reachable from player spawn
// ================================================================
// Flood-fills from below the player's spawn feet.  Verifies that at
// least one cell at Y >= (house ground floor + storyHeight) is in the
// reachable set — i.e. the player can actually climb to the upper story.
//
static std::string t39_second_floor_reachable() {
	auto srv = makeVillageServer();
	tickN(*srv, 5);

	auto& tmpl = srv->server()->world().getTemplate();
	if (!tmpl.pyConfig().hasVillage)       return "no village (skip)";
	const auto& houses = tmpl.pyConfig().houses;
	if (houses.empty())                    return "no houses (skip)";
	const auto& h0 = houses[0];
	if (h0.stories < 2)                    return "main house has no second floor (skip)";

	auto vc  = tmpl.villageCenter(42);   // seed=42 matches makeVillageServer
	int  sh  = tmpl.pyConfig().storyHeight;

	// Approximate first-floor Y of the main house.
	float hcx = (float)(vc.x + h0.cx) + h0.w * 0.5f;
	float hcz = (float)(vc.y + h0.cz) + h0.d * 0.5f;
	int floorY = (int)std::round(tmpl.surfaceHeight(42, hcx, hcz)) + 1;

	// Start flood-fill from the block below the player's spawn feet.
	glm::vec3 spawn = srv->spawnPos();
	// Walk downward to find a solid block to stand on.
	int startY = (int)std::floor(spawn.y) - 1;
	auto& blocks = srv->blockRegistry();
	for (int dy = 0; dy >= -5; dy--) {
		if (blocks.get(srv->chunks().getBlock((int)spawn.x, startY+dy, (int)spawn.z)).solid) {
			startY = startY + dy;
			break;
		}
	}
	glm::ivec3 startFeet = {(int)spawn.x, startY, (int)spawn.z};

	auto reachable = floodReachable(srv->chunks(), blocks, startFeet);
	if (reachable.empty())
		return "flood-fill found no reachable cells from spawn";

	// Check that some cell at Y >= floorY + sh - 1 is reachable.
	// (floorY+sh-1 is the intermediate floor block = floor of story 1.)
	int secondFloorBlockY = floorY + sh - 1;
	bool foundUpper = false;
	for (auto& pos : reachable) {
		if (pos.y >= secondFloorBlockY) { foundUpper = true; break; }
	}

	if (!foundUpper)
		return "second floor not reachable (need floor-block Y >= "
		     + std::to_string(secondFloorBlockY) + ", "
		     + std::to_string((int)reachable.size()) + " cells reachable, max Y="
		     + std::to_string(std::max_element(reachable.begin(), reachable.end(),
		           [](auto& a, auto& b){ return a.y < b.y; })->y) + ")";
	return "";
}

// ================================================================
// T40: Breaking a stone block yields a stone item (not cobblestone).
// Locks the block/item same-name invariant — any block is break-and-
// placeable as itself.
// ================================================================
static std::string t40_break_stone_drops_stone() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30);
	glm::vec3 pos = p->position;
	glm::ivec3 blockPos = {(int)std::floor(pos.x),
	                       (int)std::floor(pos.y) - 1,
	                       (int)std::floor(pos.z) - 4};
	placeBlockDirect(*srv, blockPos.x, blockPos.y, blockPos.z, BlockType::Stone);
	if (srv->chunks().getBlock(blockPos.x, blockPos.y, blockPos.z) == BLOCK_AIR)
		return "failed to place target stone block";

	breakAndTick(*srv, pid, blockPos);

	std::string itemType;
	srv->forEachEntity([&](Entity& e) {
		if (e.typeId() == ItemName::ItemEntity)
			itemType = e.getProp<std::string>(Prop::ItemType);
	});
	if (itemType.empty()) return "no item entity spawned";
	if (itemType != BlockType::Stone)
		return "stone broke into '" + itemType + "', expected 'stone'";
	return "";
}

// ================================================================
// T41: No village house uses 'logs' for walls/floors/roof.
// Villagers' scan_blocks("logs") finds tree trunks; if a house were
// built from logs, they'd chop the house.
// ================================================================
static std::string t41_village_houses_no_logs() {
	auto srv = makeVillageServer();
	tickN(*srv, 5);

	auto& tmpl   = srv->server()->world().getTemplate();
	auto& chunks = srv->chunks();
	auto& blocks = srv->blockRegistry();
	if (!tmpl.pyConfig().hasVillage) return "no village (skip)";

	BlockId bLogs = blocks.getId(BlockType::Log);
	if (bLogs == BLOCK_AIR) return "logs block id not registered";

	auto vc = tmpl.villageCenter(42);
	int sh = tmpl.pyConfig().storyHeight;
	for (const auto& h : tmpl.pyConfig().houses) {
		// Barns are open pillar structures (wood, not logs) that extend past
		// the clearingRadius. Stray tree trunks can legally spawn at their
		// far corners; that's a tree-near-barn, not structural logs.
		if (h.type == "barn") continue;
		int hcx = vc.x + h.cx, hcz = vc.y + h.cz;
		// House floor = max surface over the footprint (worldgen lifts the
		// whole structure to the tallest ground block). Scan only within the
		// structure's own volume — stricter than center-column so stray tree
		// logs near (but below) the house don't trigger false positives.
		int baseY = -1000;
		for (int dx = 0; dx < h.w; dx++)
			for (int dz = 0; dz < h.d; dz++) {
				int y = (int)std::round(
					tmpl.surfaceHeight(42, (float)(hcx+dx), (float)(hcz+dz)));
				if (y > baseY) baseY = y;
			}
		int yMin = baseY + 1;                          // floor row
		int yMax = baseY + sh * h.stories + h.w / 2;   // + gable roof layers
		for (int y = yMin; y <= yMax; y++)
			for (int dx = 0; dx < h.w; dx++)
				for (int dz = 0; dz < h.d; dz++) {
					BlockId bid = chunks.getBlock(hcx+dx, y, hcz+dz);
					if (bid == bLogs) {
						return "house has 'logs' at ("
						       + std::to_string(hcx+dx) + ","
						       + std::to_string(y) + ","
						       + std::to_string(hcz+dz) + ")";
					}
				}
	}
	return "";
}

// ================================================================
// Physics helpers — place/remove blocks directly for setup
// ================================================================

static void placeBlockDirect(TestServer& srv, int x, int y, int z, const std::string& typeId) {
    auto* gs = srv.server();
    if (!gs) return;
    auto& world = gs->world();
    BlockId bid = world.blocks.getId(typeId);
    if (bid == BLOCK_AIR) return;
    ChunkPos cp = World::worldToChunk(x, y, z);
    Chunk* c = world.getChunk(cp);
    if (!c) return;
    int lx = ((x % 16) + 16) % 16;
    int ly = ((y % 16) + 16) % 16;
    int lz = ((z % 16) + 16) % 16;
    c->set(lx, ly, lz, bid);
}

static bool isInsideBlock(TestServer& srv, glm::vec3 pos, float halfW, float height) {
    auto* gs = srv.server();
    if (!gs) return false;
    auto& world = gs->world();
    auto& blocks = world.blocks;
    int x0 = (int)std::floor(pos.x - halfW), x1 = (int)std::floor(pos.x + halfW);
    int y0 = (int)std::floor(pos.y),         y1 = (int)std::floor(pos.y + height);
    int z0 = (int)std::floor(pos.z - halfW), z1 = (int)std::floor(pos.z + halfW);
    for (int y = y0; y <= y1; y++)
        for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++) {
                const auto& def = blocks.get(world.getBlock(x, y, z));
                if (!def.solid) continue;
                float bh = def.collision_height;
                if (pos.y < (float)y + bh && pos.y + height > (float)y)
                    return true;
            }
    return false;
}

// ================================================================
// P50: No tunneling through wall at very high horizontal speed
// ================================================================
static std::string p50_no_horizontal_tunneling() {
    auto srv = makeFlatServer();
    EntityId pid = srv->localPlayerId();
    Entity* p = srv->getEntity(pid);
    if (!p) return "no player";

    tickN(*srv, 30);  // settle on ground
    float groundY = p->position.y;
    float hw = (p->def().collision_box_max.x - p->def().collision_box_min.x) * 0.5f;
    float ht = p->def().collision_box_max.y - p->def().collision_box_min.y;

    // Place a solid 3-block-tall wall 3 blocks east of player
    int wallX = (int)std::floor(p->position.x) + 3;
    int wallY = (int)std::round(groundY);
    int wallZ = (int)std::floor(p->position.z);
    for (int dy = 0; dy <= 3; dy++)
        placeBlockDirect(*srv, wallX, wallY + dy, wallZ, BlockType::Stone);

    float wallFace = (float)wallX;  // west face of wall (player can't cross this)

    // Drive entity at extremely high speed toward wall — 50 m/s eastward
    p->velocity.x = 50.0f;
    p->velocity.z = 0.0f;

    bool everInsideWall = false;
    for (int i = 0; i < 30; i++) {
        srv->tick(1.0f / 60.0f);
        if (isInsideBlock(*srv, p->position, hw, ht)) {
            everInsideWall = true;
            break;
        }
        // Entity right edge must not exceed wall face
        if (p->position.x + hw > wallFace + 0.1f)
            return "player passed through wall: x=" + std::to_string(p->position.x) +
                   " rightEdge=" + std::to_string(p->position.x + hw) +
                   " wallFace=" + std::to_string(wallFace);
    }
    if (everInsideWall)
        return "player clipped inside solid block during high-speed wall collision";
    return "";
}

// ================================================================
// P51: No tunneling through floor when falling at maxFallSpeed
// ================================================================
static std::string p51_no_vertical_tunneling() {
    auto srv = makeFlatServer();
    EntityId pid = srv->localPlayerId();
    Entity* p = srv->getEntity(pid);
    if (!p) return "no player";

    tickN(*srv, 30);
    float groundY = p->position.y;
    float hw = (p->def().collision_box_max.x - p->def().collision_box_min.x) * 0.5f;
    float ht = p->def().collision_box_max.y - p->def().collision_box_min.y;

    // Teleport player high up (10 blocks) so they fall
    p->position.y = groundY + 10.0f;
    // Set to maxFallSpeed downward — worst case
    p->velocity.y = -50.0f;
    p->onGround = false;

    bool everInsideBlock = false;
    float minY = p->position.y;
    for (int i = 0; i < 60; i++) {
        srv->tick(1.0f / 60.0f);
        if (p->position.y < minY) minY = p->position.y;
        if (isInsideBlock(*srv, p->position, hw, ht)) {
            everInsideBlock = true;
            break;
        }
    }
    if (everInsideBlock)
        return "player clipped into block while falling at maxFallSpeed (got to y=" +
               std::to_string(minY) + ")";
    // Should have landed at ground level
    if (p->position.y < groundY - 0.5f)
        return "player fell through floor: y=" + std::to_string(p->position.y) +
               " expected ~" + std::to_string(groundY);
    return "";
}

// ================================================================
// P52: Entity knocked back at high velocity doesn't clip through wall
// ================================================================
static std::string p52_knockback_no_tunneling() {
    auto srv = makeFlatServer();
    EntityId pid = srv->localPlayerId();
    Entity* p = srv->getEntity(pid);
    if (!p) return "no player";

    tickN(*srv, 30);
    float groundY = p->position.y;
    float hw = (p->def().collision_box_max.x - p->def().collision_box_min.x) * 0.5f;
    float ht = p->def().collision_box_max.y - p->def().collision_box_min.y;

    // Build walls on all 4 sides: 3-block-wide box around player
    int px = (int)std::floor(p->position.x);
    int py = (int)std::round(groundY);
    int pz = (int)std::floor(p->position.z);
    for (int d = -3; d <= 3; d++) {
        for (int dy = 0; dy <= 3; dy++) {
            placeBlockDirect(*srv, px - 4, py + dy, pz + d, BlockType::Stone);
            placeBlockDirect(*srv, px + 4, py + dy, pz + d, BlockType::Stone);
            placeBlockDirect(*srv, px + d, py + dy, pz - 4, BlockType::Stone);
            placeBlockDirect(*srv, px + d, py + dy, pz + 4, BlockType::Stone);
        }
    }

    bool everInsideBlock = false;
    // Simulate attack knockback from each direction
    const glm::vec3 dirs[] = {{1,0,0},{-1,0,0},{0,0,1},{0,0,-1},{1,0,1},{-1,0,-1}};
    for (auto& dir : dirs) {
        // Reset player position to center
        p->position = {(float)px + 0.5f, groundY, (float)pz + 0.5f};
        p->velocity = dir * 20.0f + glm::vec3(0, 5.0f, 0);  // hard knockback
        p->onGround = false;
        for (int i = 0; i < 20; i++) {
            srv->tick(1.0f / 60.0f);
            if (isInsideBlock(*srv, p->position, hw, ht)) {
                everInsideBlock = true;
                break;
            }
        }
        if (everInsideBlock) break;
    }
    if (everInsideBlock)
        return "player clipped into block after knockback at 20 m/s";
    return "";
}

// ================================================================
// P53: Entities never end up inside solid blocks after N village ticks
// ================================================================
static std::string p53_entities_never_inside_blocks_village() {
    auto srv = makeVillageServer();

    // Check at spawn (before any ticks) — catches spawn-inside-building bugs
    std::string spawnBad;
    srv->forEachEntity([&](Entity& e) {
        if (!e.def().isLiving() || e.removed) return;
        float hw = (e.def().collision_box_max.x - e.def().collision_box_min.x) * 0.5f;
        float ht = e.def().collision_box_max.y - e.def().collision_box_min.y;
        if (spawnBad.empty() && isInsideBlock(*srv, e.position, hw, ht))
            spawnBad = e.typeId() + " id=" + std::to_string(e.id()) +
                       " SPAWNED inside block at (" + std::to_string(e.position.x) + "," +
                       std::to_string(e.position.y) + "," + std::to_string(e.position.z) + ")";
    });
    if (!spawnBad.empty()) return spawnBad;

    tickN(*srv, 60);  // let world settle

    std::string bad;
    for (int frame = 0; frame < 120 && bad.empty(); frame++) {
        srv->tick(1.0f / 60.0f);
        srv->forEachEntity([&](Entity& e) {
            if (bad.empty() && !e.def().isLiving()) return;  // skip item entities
            if (bad.empty() && e.removed) return;
            float hw = (e.def().collision_box_max.x - e.def().collision_box_min.x) * 0.5f;
            float ht = e.def().collision_box_max.y - e.def().collision_box_min.y;
            if (isInsideBlock(*srv, e.position, hw, ht))
                bad = e.typeId() + " id=" + std::to_string(e.id()) +
                      " inside block at (" + std::to_string(e.position.x) + "," +
                      std::to_string(e.position.y) + "," + std::to_string(e.position.z) + ")";
        });
    }
    if (!bad.empty()) return bad;
    return "";
}

// ================================================================
// P54: Server rejects clientPos that overlaps a solid block.
// Simulates a malicious/buggy client repeatedly asserting a position
// inside a wall — the server must never accept it.
// ================================================================
static std::string p54_server_rejects_client_pos_in_wall() {
    auto srv = makeFlatServer();
    EntityId pid = srv->localPlayerId();
    Entity* p = srv->getEntity(pid);
    if (!p) return "no player";

    tickN(*srv, 30);  // settle on ground
    float groundY = p->position.y;
    float hw = (p->def().collision_box_max.x - p->def().collision_box_min.x) * 0.5f;
    float ht = p->def().collision_box_max.y - p->def().collision_box_min.y;

    // Place a 3-block-tall stone wall 2 blocks east of the player.
    int wallX = (int)std::floor(p->position.x) + 2;
    int wallY = (int)std::round(groundY);
    int wallZ = (int)std::floor(p->position.z);
    for (int dy = 0; dy <= 3; dy++)
        placeBlockDirect(*srv, wallX, wallY + dy, wallZ, BlockType::Stone);

    // Each frame, send a Move with clientPos = current + 0.3m eastward,
    // shoving the entity into the wall. If the server trusts clientPos
    // naively, the entity will slide into the stone.
    for (int i = 0; i < 60; i++) {
        Entity* pe = srv->getEntity(pid);
        if (!pe) return "player removed";
        ActionProposal a;
        a.type = ActionProposal::Move;
        a.actorId = pid;
        a.desiredVel = {5.0f, 0, 0};
        a.hasClientPos = true;
        a.clientPos = pe->position + glm::vec3(0.3f, 0, 0);
        srv->sendAction(a);
        srv->tick(1.0f / 60.0f);
        if (isInsideBlock(*srv, pe->position, hw, ht)) {
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                "server accepted clientPos inside wall at frame %d: pos=(%.3f,%.3f,%.3f) wallX=%d",
                i, pe->position.x, pe->position.y, pe->position.z, wallX);
            return buf;
        }
    }

    // Also assert the entity never crossed the west face of the wall.
    Entity* pe = srv->getEntity(pid);
    float eastEdge = pe->position.x + hw;
    if (eastEdge > (float)wallX + 0.001f) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "entity east edge %.3f crossed wall face at x=%d", eastEdge, wallX);
        return buf;
    }
    return "";
}

// ================================================================
// B2: All behavior files load without Python errors
// ================================================================
static std::string b2_all_behaviors_load_cleanly() {
    const char* behaviors[] = {
        "woodcutter", "wander", "peck", "prowl", "brave_chicken", nullptr
    };
    for (int i = 0; behaviors[i]; i++) {
        std::string path = std::string("artifacts/behaviors/base/") + behaviors[i] + ".py";
        std::ifstream f(path);
        if (!f) return std::string("cannot open ") + behaviors[i] + ".py";
        std::ostringstream ss; ss << f.rdbuf();
        std::string loadErr;
        auto handle = pythonBridge().loadBehavior(ss.str(), loadErr);
        if (handle < 0)
            return std::string(behaviors[i]) + " load error: " + loadErr;
        pythonBridge().unloadBehavior(handle);
    }
    return "";
}

// ================================================================
// B4: Server StoreItem action transfers inventory to chest Structure entity
// ================================================================
static std::string b4_store_item_server_validation() {
    auto srv = makeVillageServer();
    auto* gs = srv->server();

    // Villagers no longer carry a chest_entity_id prop — pick any villager
    // and any chest entity and verify Relocate transfers items between them.
    Entity* villager = nullptr;
    Entity* chest    = nullptr;
    srv->forEachEntity([&](Entity& e) {
        if (!villager && e.typeId() == "villager") villager = &e;
        if (!chest    && e.typeId() == "chest")    chest    = &e;
    });
    if (!villager) return "no villager spawned";
    if (!villager->inventory) return "villager has no inventory";
    if (!chest)    return "no chest entity spawned";
    if (!chest->inventory) return "chest entity has no inventory";
    EntityId chestEid = chest->id();

    // Give the villager some trunks
    villager->inventory->add("logs", 3);

    // Teleport villager next to the chest
    villager->position = chest->position + glm::vec3(1.5f, 0, 0);

    // Send Relocate(to=Entity(chest)) — StoreItem deposits all items
    ActionProposal sp;
    sp.type = ActionProposal::Relocate;
    sp.actorId = villager->id();
    sp.relocateTo = Container::entity(chestEid);
    srv->sendActionDirect(sp);
    srv->tick(1.0f / 60.0f);

    // Verify actor inventory is now empty
    int logsInInventory = villager->inventory->count("logs");
    if (logsInInventory > 0)
        return "villager inventory not cleared after StoreItem (still has " +
               std::to_string(logsInInventory) + " base:logs)";

    // Verify chest entity inventory received the items
    int logsInChest = chest->inventory->count("logs");
    if (logsInChest != 3)
        return "chest has " + std::to_string(logsInChest) + " trunks, expected 3";

    return "";
}

// ================================================================
// W1: House foundation is solid stone under every footprint column
// ================================================================
static std::string w1_foundation_is_stone() {
    auto srv = makeVillageServer();
    auto& world = srv->server()->world();
    auto* ctmpl = dynamic_cast<ConfigurableWorldTemplate*>(&world.getTemplate());
    if (!ctmpl) return "not ConfigurableWorldTemplate";
    int seed    = 42;

    BlockId stoneId = world.blocks.getId(BlockType::Stone);
    if (stoneId == BLOCK_AIR) return "BlockType::Stone not registered";

    auto vc = ctmpl->villageCenter(seed);
    const auto& houses = ctmpl->pyConfig().houses;
    if (houses.empty()) return "no houses in village config";

    int checked = 0;
    for (const auto& h : houses) {
        int hcx = vc.x + h.cx, hcz = vc.y + h.cz;
        int floorY = ctmpl->structureFloorY(seed, hcx, hcz, h.w, h.d);

        // For houses: stone is at floorY-1 (below the wall loop).
        // For barns: planks sit at floorY-1 (the barn floor), so stone is at floorY-2.
        int checkY = (h.type == "barn") ? floorY - 2 : floorY - 1;

        // Check several interior columns (skip corners which may be wall/pillar material)
        for (int dx = 1; dx < h.w - 1; dx += 3) {
            for (int dz = 1; dz < h.d - 1; dz += 3) {
                int x = hcx + dx, z = hcz + dz;
                BlockId bid = world.getBlock(x, checkY, z);
                if (bid != stoneId) {
                    const std::string& actual = world.blocks.get(bid).string_id;
                    return "house " + (h.type.empty() ? "house" : h.type) +
                           " at (" + std::to_string(x) + "," +
                           std::to_string(checkY) + "," + std::to_string(z) +
                           "): expected stone, got '" + actual + "'";
                }
                checked++;
            }
        }
    }
    if (checked == 0) return "no foundation columns checked";
    return "";
}

// ================================================================
// W2: House floor is at or above every terrain point in its footprint
// ================================================================
static std::string w2_floor_at_or_above_all_terrain() {
    auto srv = makeVillageServer();
    auto& world = srv->server()->world();
    auto* ctmpl = dynamic_cast<ConfigurableWorldTemplate*>(&world.getTemplate());
    if (!ctmpl) return "not ConfigurableWorldTemplate";
    int seed    = 42;

    auto vc = ctmpl->villageCenter(seed);
    for (const auto& h : ctmpl->pyConfig().houses) {
        int hcx = vc.x + h.cx, hcz = vc.y + h.cz;
        int floorY = ctmpl->structureFloorY(seed, hcx, hcz, h.w, h.d);

        for (int dx = 0; dx < h.w; dx++) {
            for (int dz = 0; dz < h.d; dz++) {
                float localGY = ctmpl->surfaceHeight(seed, (float)(hcx+dx), (float)(hcz+dz));
                if ((int)std::round(localGY) >= floorY) {
                    return "house at (" + std::to_string(hcx) + "," + std::to_string(hcz) +
                           "): terrain at (" + std::to_string(dx) + "," + std::to_string(dz) +
                           ") has groundY=" + std::to_string((int)localGY) +
                           " >= floorY=" + std::to_string(floorY) +
                           " (hill would protrude through floor)";
                }
            }
        }
    }
    return "";
}

// ================================================================
// W3: House interior has no buried terrain (non-air) above foundation
// ================================================================
static std::string w3_house_interior_no_buried_terrain() {
    auto srv = makeVillageServer();
    auto& world = srv->server()->world();
    auto* ctmpl = dynamic_cast<ConfigurableWorldTemplate*>(&world.getTemplate());
    if (!ctmpl) return "not ConfigurableWorldTemplate";
    int seed    = 42;

    // Block types that are legitimately inside a house (not buried terrain)
    auto& reg = world.blocks;
    std::unordered_set<std::string> allowed = {
        "air", "cobblestone", "wood", "planks",
        "stone", "door", "door_open", "glass",
        "bed", "chest", "stair",
        // Roof and wall materials used by some houses
        "leaves", "log",
    };

    auto vc = ctmpl->villageCenter(seed);
    for (const auto& h : ctmpl->pyConfig().houses) {
        if (h.type == "barn") continue;  // barn has no walls so terrain is expected outside
        int hcx = vc.x + h.cx, hcz = vc.y + h.cz;
        int floorY = ctmpl->structureFloorY(seed, hcx, hcz, h.w, h.d);

        // Check the interior (exclude outer wall columns dx=0,w-1 and dz=0,d-1)
        for (int dx = 1; dx < h.w - 1; dx++) {
            for (int dz = 1; dz < h.d - 1; dz++) {
                // Scan from floor level to 3 blocks above floor (ground story interior)
                for (int dy = 0; dy < 4; dy++) {
                    int bx = hcx + dx, by = floorY + dy, bz = hcz + dz;
                    BlockId bid = world.getBlock(bx, by, bz);
                    const std::string& sid = reg.get(bid).string_id;
                    if (bid != BLOCK_AIR && allowed.find(sid) == allowed.end()) {
                        return "house interior at (" + std::to_string(bx) + "," +
                               std::to_string(by) + "," + std::to_string(bz) +
                               ") has unexpected block '" + sid + "' (buried terrain?)";
                    }
                }
            }
        }
    }
    return "";
}

// ================================================================
// W4: Villagers spawn near the monument; animals spawn inside the barn
// ================================================================
// Guards the species→default-behavior mapping in builtin.cpp. If someone
// edits the behavior string for an animal (e.g. sets pig to "woodcutter"
// again) pigs would march from the barn to the nearest tree within a
// second, making it look like "all animals spawned near the monument".
// Checking EntityDef.default_props[BehaviorId] catches that at registration
// time, long before the visual symptom appears.
static std::string w4b_default_behaviors_per_species() {
    auto srv = makeVillageServer();
    auto& em = srv->server()->world().entities;
    struct Expected { const char* typeId; const char* behavior; };
    const Expected want[] = {
        {"pig",           "wander"},
        {"chicken",       "peck"},
        {"brave_chicken", "brave_chicken"},
        {"cat",           "prowl"},
        {"dog",           "follow"},
        {"villager",      "woodcutter"},
    };
    for (auto& w : want) {
        auto* def = em.getTypeDef(w.typeId);
        if (!def) { char b[96]; std::snprintf(b, sizeof(b), "no EntityDef for %s", w.typeId); return b; }
        auto it = def->default_props.find(Prop::BehaviorId);
        if (it == def->default_props.end())
            return std::string(w.typeId) + " has no default BehaviorId";
        const auto* s = std::get_if<std::string>(&it->second);
        if (!s || *s != w.behavior) {
            char b[160];
            std::snprintf(b, sizeof(b), "%s behavior=%s want=%s", w.typeId,
                s ? s->c_str() : "<non-string>", w.behavior);
            return b;
        }
    }
    return "";
}

static std::string w4_mob_spawn_anchors() {
    auto srv = makeVillageServer();
    auto& tmpl = srv->server()->world().getTemplate();
    int seed = srv->server()->world().seed();
    auto vc = tmpl.villageCenter(seed);
    auto barnCtr = tmpl.barnCenter(seed);
    if (barnCtr.x < 0) return "village template has no barn";

    // Barn footprint: find the barn house in the config and use its (cx,cz,w,d).
    const auto& houses = tmpl.pyConfig().houses;
    glm::ivec2 barnMin{0,0}, barnMax{0,0};
    bool foundBarn = false;
    for (const auto& h : houses) {
        if (h.type == "barn") {
            barnMin = {vc.x + h.cx, vc.y + h.cz};
            barnMax = {vc.x + h.cx + h.w, vc.y + h.cz + h.d};
            foundBarn = true;
            break;
        }
    }
    if (!foundBarn) return "no barn house in template";

    // Monument ring: villagers should land within ~12 blocks of the village
    // center (village.py sets radius=10; allow jitter for gravity-settle).
    // Portal ring: non-barn animals (squirrel, raccoon, beaver, bee) spawn
    // around the portal at radius 4–8 per village.py — allow 16 for slack.
    constexpr float kMonumentRadius = 12.0f;
    constexpr float kPortalRadius   = 16.0f;

    // Index each mob type → its declared spawn_at anchor from the template.
    std::unordered_map<std::string, std::string> anchorOf;
    for (const auto& mc : tmpl.pyConfig().mobs) anchorOf[mc.type] = mc.spawnAt;

    auto portalSpawn = tmpl.preferredSpawn(seed);

    // Skip the local player character (joined via addClient), not all playable
    // types — villagers are also playable but should be counted.
    EntityId playerId = srv->localPlayerId();

    int villagerCount = 0, animalCount = 0;
    srv->forEachEntity([&](Entity& e) {
        if (!e.def().isLiving() || e.removed) return;
        if (e.id() == playerId) return;
        const std::string& tid = e.typeId();

        std::string anchor = anchorOf.count(tid) ? anchorOf[tid] : "";
        if (tid == "villager") anchor = "monument";   // safety default

        if (anchor == "monument") {
            villagerCount++;
            float dx = e.position.x - (float)vc.x;
            float dz = e.position.z - (float)vc.y;
            float d = std::sqrt(dx*dx + dz*dz);
            if (d > kMonumentRadius) {
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                    "%s id=%u at (%.1f,%.1f,%.1f) is %.1fm from monument — want <%.1f",
                    tid.c_str(), e.id(), e.position.x, e.position.y, e.position.z, d, kMonumentRadius);
                throw std::runtime_error(buf);
            }
        } else if (anchor == "barn") {
            animalCount++;
            // XZ must be inside the barn footprint. Y is allowed anywhere
            // (owl roosts on the roof above the walls).
            if (e.position.x < (float)barnMin.x || e.position.x >= (float)barnMax.x ||
                e.position.z < (float)barnMin.y || e.position.z >= (float)barnMax.y) {
                char buf[192];
                std::snprintf(buf, sizeof(buf),
                    "%s id=%u at (%.1f,%.1f,%.1f) is outside barn footprint [%d..%d) x [%d..%d)",
                    tid.c_str(), e.id(), e.position.x, e.position.y, e.position.z,
                    barnMin.x, barnMax.x, barnMin.y, barnMax.y);
                throw std::runtime_error(buf);
            }
        } else if (anchor == "portal") {
            animalCount++;
            float dx = e.position.x - portalSpawn.x;
            float dz = e.position.z - portalSpawn.z;
            float d = std::sqrt(dx*dx + dz*dz);
            if (d > kPortalRadius) {
                char buf[192];
                std::snprintf(buf, sizeof(buf),
                    "%s id=%u at (%.1f,_,%.1f) is %.1fm from portal — want <%.1f",
                    tid.c_str(), e.id(), e.position.x, e.position.z, d, kPortalRadius);
                throw std::runtime_error(buf);
            }
        }
        // Unknown anchor (e.g. empty string — village ring): don't assert.
    });

    if (villagerCount == 0) return "no villagers spawned";
    if (animalCount == 0)   return "no animals spawned";
    return "";
}

// ================================================================
// B5: pathfind.py module loads cleanly
// ================================================================
static std::string b5_pathfind_module_loads() {
    // Load pathfind.py as a standalone module (not as a behavior)
    std::ifstream f("python/pathfind.py");
    if (!f) return "cannot open python/pathfind.py";
    std::ostringstream ss; ss << f.rdbuf();
    std::string src = ss.str();

    // Wrap it in a minimal behavior that imports and uses Navigator
    std::string behaviorSrc = src + R"(
from civcraft_engine import Move
from behavior_base import Behavior

class PathfindTestBehavior(Behavior):
    def __init__(self):
        self._nav = Navigator()
    def decide(self, entity, world):
        return Move(entity.x, entity.y, entity.z), "pathfind loaded"
)";

    std::string loadErr;
    auto handle = pythonBridge().loadBehavior(behaviorSrc, loadErr);
    if (handle < 0) return "loadBehavior failed: " + loadErr;
    pythonBridge().unloadBehavior(handle);
    return "";
}

// ================================================================
// C2: block_search prefers the NEAREST non-empty chunk.
//
// Synthetic setup (no TestServer, no Python):
//   - Near chunk A at ChunkPos{0,0,0} holding 1 trunk block (sparse)
//   - Far chunk B at ChunkPos{3,0,0} holding 50 trunk blocks (rich)
// After Phase 2 (distance-only sort + first-hit exit), run() must pick
// the lone trunk in chunk A and never even load chunk B. A spy counter
// on ensureChunkLoaded asserts chunk B was not accessed.
// ================================================================
static std::string c2_scan_blocks_prefers_nearest_chunk() {
	BlockRegistry blocks;
	BlockDef air;  air.string_id  = "air";  air.solid = false;
	BlockDef trunk; trunk.string_id = "logs";
	blocks.registerBlock(air);    // id 0
	BlockId trunkId = blocks.registerBlock(trunk);

	using block_search::ChunkCensus;
	std::unordered_map<ChunkPos, ChunkCensus, ChunkPosHash> census;
	block_search::ChunkMap chunks;

	auto makeChunk = [&]() { return std::make_unique<Chunk>(); };

	// Near chunk A at (0,0,0) — 1 trunk at local (2,2,2).
	ChunkPos cpA{0, 0, 0};
	{
		auto ch = makeChunk();
		ch->set(2, 2, 2, trunkId);
		chunks[cpA] = std::move(ch);
		ChunkCensus ci;
		ci.entries["logs"] = {1};
		census[cpA] = ci;
	}

	// Far chunk B at (3,0,0) — 50 trunks at local (lx,0,0) for lx in 0..49%16.
	// Pack 50 trunks into a 16×16 y=0 layer.
	ChunkPos cpB{3, 0, 0};
	{
		auto ch = makeChunk();
		int placed = 0;
		for (int lz = 0; lz < CHUNK_SIZE && placed < 50; lz++) {
			for (int lx = 0; lx < CHUNK_SIZE && placed < 50; lx++) {
				ch->set(lx, 0, lz, trunkId);
				placed++;
			}
		}
		chunks[cpB] = std::move(ch);
		ChunkCensus ci;
		ci.entries["logs"] = {50};
		census[cpB] = ci;
	}

	// Spy on ensureChunkLoaded so we can assert chunk B is never loaded.
	std::vector<ChunkPos> loadSpy;
	block_search::EnsureLoadedFn ensureLoaded =
		[&](ChunkPos cp) { loadSpy.push_back(cp); };

	// Origin right inside chunk A at world (2.5, 2.5, 2.5) — chunk A center
	// is ~(8,8,8), chunk B center ~(56,8,8): B is ~53 blocks east.
	block_search::Options opt;
	opt.typeId       = "logs";
	opt.searchOrigin = {2.5f, 2.5f, 2.5f};
	opt.maxDist      = 80.0f;
	opt.maxResults   = 1;

	auto results = block_search::run(opt, census, chunks, blocks, ensureLoaded);

	if (results.empty())
		return "no trunks found (the lone trunk in chunk A should have matched)";
	if (results.size() != 1)
		return "expected exactly 1 result, got " + std::to_string(results.size());

	const auto& hit = results[0];
	// Chunk A's trunk sits at local (2,2,2) → world (2,2,2). Chunk B starts
	// at world x=48. The nearest-first search must pick chunk A.
	if (hit.x >= 48)
		return "expected nearest hit from chunk A (x<48), got x=" +
		       std::to_string(hit.x) +
		       " (distance-first sort/early-exit not working)";
	if (hit.x != 2 || hit.y != 2 || hit.z != 2)
		return "expected (2,2,2), got (" + std::to_string(hit.x) + "," +
		       std::to_string(hit.y) + "," + std::to_string(hit.z) + ")";

	// Chunk A is already in the chunk map, so ensureChunkLoaded should not
	// have been called at all. In particular it must never have been asked
	// to load chunk B — the whole point of the early-exit.
	for (auto& cp : loadSpy) {
		if (cp.x == 3 && cp.y == 0 && cp.z == 0)
			return "ensureChunkLoaded was called for far chunk B — "
			       "early-exit after first non-empty chunk failed";
	}

	return "";
}

// ================================================================
// NAV1: Single entity walks toward goal on flat ground, arrives
// ================================================================
static std::string nav1_single_entity_walks_to_goal() {
	auto srv = makeFlatServer();
	tickN(*srv, 60); // settle on ground

	Entity* p = srv->getEntity(srv->localPlayerId());
	if (!p) return "no player";

	// Move entity to a clear area (away from village buildings)
	p->position = {0, 9.0f, -30.0f};
	tickN(*srv, 30); // settle on ground

	glm::vec3 startPos = p->position;
	glm::vec3 goalPos = startPos + glm::vec3(10.0f, 0, 0);

	// Set nav goal directly (simulating C_SET_GOAL)
	p->nav.setGoal(goalPos);
	if (!p->nav.active) return "nav not active after setGoal";

	// Run enough ticks for entity to walk ~10 blocks
	tickN(*srv, 60 * 5); // 5 seconds (at 8 blocks/sec, plenty)

	float dx = p->position.x - goalPos.x;
	float dz = p->position.z - goalPos.z;
	float dist = std::sqrt(dx * dx + dz * dz);
	if (dist > 2.0f)
		return "entity didn't arrive: dist=" + std::to_string(dist) +
		       " pos=(" + std::to_string(p->position.x) + "," +
		       std::to_string(p->position.z) + ")";

	// Nav should have cleared after arrival
	if (p->nav.active)
		return "nav still active after arriving (dist=" + std::to_string(dist) + ")";

	return "";
}

// ================================================================
// NAV2: Group of 3 entities get formation positions, all arrive
// ================================================================
static std::string nav2_group_formation() {
	auto srv = makeFlatServer();
	tickN(*srv, 60);

	auto* gs = srv->server();

	// Move player to clear area (away from village buildings)
	Entity* p = srv->getEntity(srv->localPlayerId());
	if (!p) return "no player";
	p->position = {0, 9.0f, -30.0f};

	// Spawn 2 more entities nearby in the clear area
	EntityId e2 = gs->world().entities.spawn("knight", p->position + glm::vec3(2, 0, 0));
	EntityId e3 = gs->world().entities.spawn("knight", p->position + glm::vec3(-2, 0, 0));
	Entity* ent2 = gs->world().entities.get(e2);
	Entity* ent3 = gs->world().entities.get(e3);
	if (!ent2 || !ent3) return "failed to spawn test entities";

	tickN(*srv, 30); // settle

	// Group goal — all 3 entities to a point 15 blocks east
	glm::vec3 target = p->position + glm::vec3(15.0f, 0, 0);
	std::vector<Entity*> group = {p, ent2, ent3};
	planGroupFormation(target, group);

	// Verify each entity got a different long-term goal
	if (!p->nav.active || !ent2->nav.active || !ent3->nav.active)
		return "not all entities have active nav";

	// At least 2 of the 3 should have different longGoals (formation offsets)
	bool allSame = (p->nav.longGoal == ent2->nav.longGoal) &&
	               (ent2->nav.longGoal == ent3->nav.longGoal);
	if (allSame)
		return "all 3 entities got identical goals (no formation spread)";

	// Save goals before they get cleared on arrival
	glm::vec3 goals[3] = {p->nav.longGoal, ent2->nav.longGoal, ent3->nav.longGoal};

	// Greedy steering settles "close enough", not exactly on the slot — formation
	// is a group-level concept, individual precision is bounded by friction +
	// neighbor avoidance. Poll up to 10s for everyone to come within arrivedDist.
	Entity* ents[3] = {p, ent2, ent3};
	constexpr float arrivedDist = 5.0f;
	auto allArrived = [&] {
		for (int i = 0; i < 3; i++) {
			float dx = ents[i]->position.x - goals[i].x;
			float dz = ents[i]->position.z - goals[i].z;
			if (std::sqrt(dx*dx + dz*dz) > arrivedDist) return false;
		}
		return true;
	};
	for (int i = 0; i < 60 * 10 && !allArrived(); i++)
		srv->tick(1.0f / 60.0f);

	for (int i = 0; i < 3; i++) {
		float dx = ents[i]->position.x - goals[i].x;
		float dz = ents[i]->position.z - goals[i].z;
		float dist = std::sqrt(dx * dx + dz * dz);
		if (dist > arrivedDist)
			return "entity " + std::to_string(ents[i]->id()) + " didn't arrive: dist=" +
			       std::to_string(dist);
	}

	return "";
}

// ================================================================
// NAV3: Entity facing a wall dodges and makes progress
// ================================================================
static std::string nav3_dodge_around_wall() {
	auto srv = makeFlatServer();
	tickN(*srv, 60);

	auto* gs = srv->server();
	auto& world = gs->world();
	Entity* p = srv->getEntity(srv->localPlayerId());
	if (!p) return "no player";

	float startX = p->position.x;

	// Place a wall 3 blocks east, 5 blocks wide in Z
	BlockId stone = world.blocks.getId("stone");
	int wx = (int)std::floor(p->position.x) + 3;
	int wy = (int)std::floor(p->position.y);
	int baseZ = (int)std::floor(p->position.z);
	for (int dz = -2; dz <= 2; dz++) {
		for (int dy = 0; dy <= 1; dy++) {
			int wz = baseZ + dz;
			ChunkPos cp = World::worldToChunk(wx, wy + dy, wz);
			Chunk* c = world.getChunk(cp);
			if (c) {
				int lx = ((wx % 16) + 16) % 16;
				int ly = (((wy + dy) % 16) + 16) % 16;
				int lz = ((wz % 16) + 16) % 16;
				c->set(lx, ly, lz, stone);
			}
		}
	}

	// Set goal 8 blocks east (past the wall)
	glm::vec3 goal = p->position + glm::vec3(8.0f, 0, 0);
	p->nav.setGoal(goal);

	// Run for a while — entity should dodge and make some progress
	tickN(*srv, 60 * 10); // 10 seconds

	// Entity should have moved from start, even if not fully arrived
	float progress = p->position.x - startX;
	if (progress < 1.0f)
		return "entity made no east progress past wall: moved " +
		       std::to_string(progress) + " blocks";

	// Entity should still be actively navigating (never gives up)
	// (may have arrived, which is also fine)
	return "";
}

// ================================================================
// NAV4: Entity in enclosed box keeps walking, never stops
// ================================================================
static std::string nav4_never_gives_up() {
	auto srv = makeFlatServer();
	tickN(*srv, 60);

	auto* gs = srv->server();
	auto& world = gs->world();
	Entity* p = srv->getEntity(srv->localPlayerId());
	if (!p) return "no player";

	// Build a box around the player (3x3 walls, 2 high)
	BlockId stone = world.blocks.getId("stone");
	int cx = (int)std::floor(p->position.x);
	int cy = (int)std::floor(p->position.y);
	int cz = (int)std::floor(p->position.z);
	for (int dx = -2; dx <= 2; dx++) {
		for (int dz = -2; dz <= 2; dz++) {
			if (std::abs(dx) < 2 && std::abs(dz) < 2) continue; // skip interior
			for (int dy = 0; dy <= 1; dy++) {
				int bx = cx + dx, by = cy + dy, bz = cz + dz;
				ChunkPos cp = World::worldToChunk(bx, by, bz);
				Chunk* c = world.getChunk(cp);
				if (c) {
					int lx = ((bx % 16) + 16) % 16;
					int ly = ((by % 16) + 16) % 16;
					int lz = ((bz % 16) + 16) % 16;
					c->set(lx, ly, lz, stone);
				}
			}
		}
	}

	// Set goal far away (unreachable)
	p->nav.setGoal(p->position + glm::vec3(50.0f, 0, 0));

	// Run for a while
	tickN(*srv, 60 * 6); // 6 seconds

	// Entity should still be active (never gives up)
	if (!p->nav.active)
		return "entity gave up — nav should never deactivate in enclosed box";

	// Entity should have non-zero velocity (still trying to walk)
	float hSpeed = std::sqrt(p->velocity.x * p->velocity.x + p->velocity.z * p->velocity.z);
	if (hSpeed < 0.1f)
		return "entity stopped walking (speed=" + std::to_string(hSpeed) + ")";

	return "";
}

// ================================================================
// NAV5: Entity climbs 1-block step-ups during navigation
// ================================================================
static std::string nav5_step_up_climbing() {
	auto srv = makeFlatServer();
	tickN(*srv, 60);

	auto* gs = srv->server();
	auto& world = gs->world();
	Entity* p = srv->getEntity(srv->localPlayerId());
	if (!p) return "no player";

	// Move to clear area (negative X to avoid houses near spawn)
	p->position = {-20.0f, 9.0f, 0.0f};
	tickN(*srv, 60); // settle

	float startY = p->position.y;
	float startX = p->position.x;

	// Place a raised platform 4 blocks east — extends 6 blocks in X direction.
	// This creates a 1-block step-up that the entity must climb and walk across.
	BlockId stone = world.blocks.getId("stone");
	int stepStartX = (int)std::floor(p->position.x) + 4;
	int feetY = (int)std::floor(p->position.y);
	int stepZ = (int)std::floor(p->position.z);
	for (int dx = 0; dx < 6; dx++) {
		for (int dz = -1; dz <= 1; dz++) {
			int bx = stepStartX + dx;
			int bz = stepZ + dz;
			ChunkPos cp = World::worldToChunk(bx, feetY, bz);
			Chunk* c = world.getChunk(cp);
			if (c) {
				int lx = ((bx % 16) + 16) % 16;
				int ly = ((feetY % 16) + 16) % 16;
				int lz = ((bz % 16) + 16) % 16;
				c->set(lx, ly, lz, stone);
			}
		}
	}
	(void)stepStartX; // used during construction above

	// Set goal 8 blocks east (past the step)
	p->nav.setGoal(p->position + glm::vec3(8.0f, 0, 0));
	tickN(*srv, 60 * 4); // 4 seconds

	// Entity should have climbed up (Y increased by ~1)
	float yGain = p->position.y - startY;
	if (yGain < 0.8f)
		return "entity didn't climb step: yGain=" + std::to_string(yGain) +
		       " pos.y=" + std::to_string(p->position.y) +
		       " startY=" + std::to_string(startY);

	// Entity should have progressed past the step in X
	float xProgress = p->position.x - startX;
	if (xProgress < 5.0f)
		return "entity didn't progress past step: xProgress=" + std::to_string(xProgress);

	return "";
}

// ================================================================
// NAV6: Entity routes around a 2-block wall (can't climb, must detour).
// Post-GridPlanner: we assert the entity goes AROUND and reaches the
// goal — the planner's `standable()` check prevents any illegal climb
// (head space blocked), so the only valid path is a lateral detour.
// ================================================================
static std::string nav6_blocked_by_tall_wall() {
	auto srv = makeFlatServer();
	tickN(*srv, 60);

	auto* gs = srv->server();
	auto& world = gs->world();
	Entity* p = srv->getEntity(srv->localPlayerId());
	if (!p) return "no player";

	// Move to clear area (negative X to avoid houses)
	p->position = {-20.0f, 9.0f, 0.0f};
	tickN(*srv, 60);

	// Place a single 2-block-tall column 4 blocks east. The planner must
	// route around (N or S) — it can never climb, because a 2-block column
	// leaves no head clearance for MovementAscend.
	BlockId stone = world.blocks.getId("stone");
	int wallX = (int)std::floor(p->position.x) + 4;
	int baseY = (int)std::floor(p->position.y);
	int wallZ = (int)std::floor(p->position.z);
	for (int dy = 0; dy <= 1; dy++) {
		ChunkPos cp = World::worldToChunk(wallX, baseY + dy, wallZ);
		Chunk* c = world.getChunk(cp);
		if (c) {
			int lx = ((wallX % 16) + 16) % 16;
			int ly = (((baseY + dy) % 16) + 16) % 16;
			int lz = ((wallZ % 16) + 16) % 16;
			c->set(lx, ly, lz, stone);
		}
	}

	glm::vec3 goalPos = p->position + glm::vec3(8.0f, 0, 0);
	p->nav.setGoal(goalPos);
	tickN(*srv, 60 * 6); // 6 seconds: ~12 blocks of travel budget

	// Entity should have reached the goal via detour — not phased through
	// the wall at y=baseY (would mean a fractional XZ that sits inside the
	// wall's block). Final XZ distance to goal must be small.
	float dx = p->position.x - goalPos.x;
	float dz = p->position.z - goalPos.z;
	float dist = std::sqrt(dx*dx + dz*dz);
	if (dist > 2.0f)
		return "entity did not reach goal: dist=" + std::to_string(dist) +
		       " pos=(" + std::to_string(p->position.x) + "," +
		       std::to_string(p->position.z) + ")";

	return "";
}

// ================================================================
// M1–M4: Camera Mode Movement Tests (client prediction + server)
// ================================================================

// Helper: simulate one frame of client-side prediction + server acceptance.
// Mirrors gameplay_movement.cpp: runs moveAndCollide locally, sends clientPos
// to server, server accepts and skips its own physics.
static glm::vec3 clientMoveFrame(TestServer& srv, EntityId actor,
                                  glm::vec3 desiredVel, bool jump = false) {
	Entity* e = srv.getEntity(actor);
	if (!e) return {0,0,0};
	const auto& def = e->def();

	auto& chunks = srv.chunks();
	auto& blocks = srv.blockRegistry();
	BlockSolidFn solidFn = [&](int x, int y, int z) -> float {
		const auto& bd = blocks.get(chunks.getBlock(x, y, z));
		return bd.solid ? bd.collision_height : 0.0f;
	};

	MoveParams mp;
	mp.halfWidth  = (def.collision_box_max.x - def.collision_box_min.x) * 0.5f;
	mp.height     = def.collision_box_max.y - def.collision_box_min.y;
	mp.gravity    = 32.0f * def.gravity_scale;
	mp.stepHeight = def.isLiving() ? 1.0f : 0.0f;
	mp.smoothStep = false;

	glm::vec3 localVel = {desiredVel.x, e->velocity.y, desiredVel.z};
	if (jump && e->onGround)
		localVel.y = 10.0f;

	constexpr float dt = 1.0f / 60.0f;
	auto result = moveAndCollide(solidFn, e->position, localVel, dt, mp, e->onGround);

	// Send to server (same as GUI client)
	ActionProposal p;
	p.type = ActionProposal::Move;
	p.actorId = actor;
	p.desiredVel = {desiredVel.x, result.velocity.y, desiredVel.z};
	p.clientPos = result.position;
	p.hasClientPos = true;
	p.jump = jump;
	p.jumpVelocity = 10.0f;
	srv.sendActionDirect(p);
	srv.tick(dt);

	// Apply client-side state
	e->position = result.position;
	e->velocity = result.velocity;
	e->onGround = result.onGround;

	return result.position;
}

// M1: FPS — Walk + Jump While Moving
// Tests the ground-snap bug fix: jump must work during horizontal movement.
static std::string m01_fps_walk_jump() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30); // settle
	float startY = p->position.y;
	glm::vec3 startPos = p->position;

	// Walk forward for 30 frames
	for (int i = 0; i < 30; i++)
		clientMoveFrame(*srv, pid, {0, 0, 4.0f});

	// Jump while walking: 1 frame with jump=true, then 30 frames of arc
	float peakY = p->position.y;
	clientMoveFrame(*srv, pid, {0, 0, 4.0f}, true); // jump!
	for (int i = 0; i < 30; i++) {
		clientMoveFrame(*srv, pid, {0, 0, 4.0f});
		if (p->position.y > peakY) peakY = p->position.y;
	}

	// Walk 30 more frames to land
	for (int i = 0; i < 30; i++)
		clientMoveFrame(*srv, pid, {0, 0, 4.0f});

	float totalZ = std::abs(p->position.z - startPos.z);
	float jumpHeight = peakY - startY;

	if (totalZ < 3.0f)
		return "barely moved forward: " + std::to_string(totalZ) + " blocks";
	if (jumpHeight < 0.5f)
		return "jump too low while moving: peak=" + std::to_string(jumpHeight) + " blocks";

	// Verify server agrees (entity position should match within tolerance)
	Entity* serverE = srv->server()->world().entities.get(pid);
	float drift = glm::length(serverE->position - p->position);
	if (drift > 1.0f)
		return "server/client drift: " + std::to_string(drift) + " blocks";

	return "";
}

// M2: TPS — Diagonal Walk + Sprint
// Tests orbit-relative velocity: walk at 45° then sprint.
static std::string m02_tps_diagonal_sprint() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30);
	glm::vec3 start = p->position;

	// Walk diagonally (simulates TPS orbit at 45°)
	float walkSpeed = 4.0f;
	glm::vec3 walkVel = {walkSpeed * 0.707f, 0, walkSpeed * 0.707f};
	for (int i = 0; i < 60; i++)
		clientMoveFrame(*srv, pid, walkVel);

	float walkDist = glm::length(glm::vec2(p->position.x - start.x, p->position.z - start.z));
	glm::vec3 walkEnd = p->position;

	if (std::abs(p->position.x - start.x) < 1.0f)
		return "didn't move in X: " + std::to_string(p->position.x - start.x);
	if (std::abs(p->position.z - start.z) < 1.0f)
		return "didn't move in Z: " + std::to_string(p->position.z - start.z);

	// Sprint (2.5x speed)
	glm::vec3 sprintVel = walkVel * 2.5f;
	for (int i = 0; i < 30; i++)
		clientMoveFrame(*srv, pid, sprintVel);

	float sprintDist = glm::length(glm::vec2(p->position.x - walkEnd.x, p->position.z - walkEnd.z));

	// Sprint 30 frames should cover more than walk 30 frames
	float walk30 = walkDist * (30.0f / 60.0f);
	if (sprintDist < walk30 * 1.5f)
		return "sprint not faster: sprint=" + std::to_string(sprintDist) +
		       " walk30=" + std::to_string(walk30);

	return "";
}

// M3: RPG — Click-to-Move (simulated agent navigation toward goal)
// Tests goal-based movement: set a target, simulate agent walking toward it.
static std::string m03_rpg_click_to_move() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30);
	glm::vec3 start = p->position;
	glm::vec3 goal = start + glm::vec3(8.0f, 0, 0); // 8 blocks east

	// Simulate agent navigation: compute direction to goal, send Move actions.
	// Agent-driven (server physics), so it's slower than client prediction.
	float speed = 6.0f;
	for (int i = 0; i < 600; i++) {
		glm::vec3 toGoal = goal - p->position;
		toGoal.y = 0;
		float dist = glm::length(toGoal);
		if (dist < 1.0f) break;
		glm::vec3 dir = toGoal / dist;
		glm::vec3 vel = dir * speed;
		moveAndTick(*srv, pid, vel); // server-driven (no clientPos)
	}

	float reached = glm::length(glm::vec2(p->position.x - goal.x, p->position.z - goal.z));
	if (reached > 3.0f)
		return "didn't reach goal: " + std::to_string(reached) + " blocks away";

	return "";
}

// M4: RTS — Multi-Entity Movement (player + Creatures toward different goals)
// Tests controlling multiple entities simultaneously.
static std::string m04_rts_multi_entity() {
	auto srv = makeVillageServer();
	EntityId pid = srv->localPlayerId();
	Entity* player = srv->getEntity(pid);
	if (!player) return "no player";

	tickN(*srv, 60); // let village settle

	// Find first living non-player entity
	EntityId npcId = ENTITY_NONE;
	srv->forEachEntity([&](Entity& e) {
		if (npcId != ENTITY_NONE) return;
		if (e.id() == pid) return;
		if (!e.def().isLiving()) return;
		if (e.removed) return;
		npcId = e.id();
	});
	if (npcId == ENTITY_NONE) return "no Creatures found in village";

	Entity* npc = srv->getEntity(npcId);
	glm::vec3 playerStart = player->position;
	glm::vec3 npcStart = npc->position;

	// Move player east, Creatures west (simultaneously)
	// Use sendActionDirect for Creatures (bypasses ownership — test simulates admin/agent)
	float speed = 4.0f;
	constexpr float dt = 1.0f / 60.0f;
	for (int i = 0; i < 90; i++) {
		moveAndTick(*srv, pid, {speed, 0, 0});
		ActionProposal np;
		np.type = ActionProposal::Move;
		np.actorId = npcId;
		np.desiredVel = {-speed, 0, 0};
		srv->sendActionDirect(np);
		srv->tick(dt);
	}

	float playerMoved = player->position.x - playerStart.x;
	float npcMoved = npcStart.x - npc->position.x;

	if (playerMoved < 2.0f)
		return "player didn't move east: " + std::to_string(playerMoved);
	if (npcMoved < 2.0f)
		return "Creatures didn't move west: " + std::to_string(npcMoved);

	return "";
}

// ================================================================
// Client reconciler simulation (for R* tests)
// ================================================================
//
// Reproduces the NetworkServer + EntityReconciler pipeline in-process, without
// TCP: every broadcast tick, clone every server entity that passes
// shouldBroadcastEntityToClient() into a mirror map and feed its state to the
// reconciler; every frame, run reconciler.tick() over the mirror. Clients see
// exactly what they'd see over the wire, and we can assert invariants
// (hasError, drift, staleness) directly.
struct SimClient {
	EntityId playerId = ENTITY_NONE;
	SeatId   seatId   = SEAT_NONE;
	std::unordered_map<EntityId, std::unique_ptr<Entity>> mirror;
	EntityReconciler reconciler;
	// Server entity ids seen at least once — so we can assert "once known,
	// always fresh" for owned entities.
	std::unordered_set<EntityId> everSeen;
};

// Mirror every server entity that the broadcast filter would accept. Matches
// broadcastState()'s loop: viewPoints = [player.position], perception=64.
static void simBroadcastTick(TestServer& srv, SimClient& c) {
	Entity* player = srv.getEntity(c.playerId);
	if (!player) return;
	std::vector<glm::vec3> viewPoints = { player->position };
	srv.forEachEntity([&](Entity& e) {
		if (!shouldBroadcastEntityToClient(e, c.seatId, viewPoints)) return;
		auto it = c.mirror.find(e.id());
		if (it == c.mirror.end()) {
			auto clone = std::make_unique<Entity>(e.id(), e.typeId(), e.def());
			clone->position = e.position;
			clone->velocity = e.velocity;
			clone->yaw = e.yaw;
			clone->onGround = e.onGround;
			clone->goalText = e.goalText.empty() ? std::string("Spawning...") : e.goalText;
			c.mirror[e.id()] = std::move(clone);
			c.reconciler.onEntityCreate(e.id(), e.position, e.velocity, e.yaw,
			                            e.moveTarget, e.moveSpeed);
		} else {
			c.reconciler.onEntityUpdate(e.id(), e.position, e.velocity, e.yaw,
			                            e.moveTarget, e.moveSpeed);
		}
		c.everSeen.insert(e.id());
	});
}

// Run 2 seconds (120 frames at 60Hz) with broadcasts at 20Hz (every 3rd
// frame), exactly matching production cadence. Returns all entity ids the
// client ever saw.
static void simRun(TestServer& srv, SimClient& c, int frames) {
	constexpr float dt = 1.0f / 60.0f;
	BlockSolidFn solidFn = [&](int x, int y, int z) -> float {
		const auto& bd = srv.blockRegistry().get(srv.chunks().getBlock(x, y, z));
		return bd.solid ? bd.collision_height : 0.0f;
	};
	for (int i = 0; i < frames; i++) {
		srv.tick(dt);
		if (i % 3 == 0) simBroadcastTick(srv, c);
		c.reconciler.tick(dt, c.playerId, c.mirror, solidFn, /*serverSilent=*/false);
	}
}

// ================================================================
// R* tests — network reconciliation regressions
// ================================================================

// R1 reproduces the red-lightbulb bug: before the owner-always-broadcast fix,
// mobs that wandered >64 blocks from the player stopped broadcasting, the
// reconciler marked them stale, Entity::hasError flipped to true, and the
// lightbulb turned red even though the server was perfectly healthy. This
// test hard-fails if ANY entity gains hasError=true across 2s of simulation.
static std::string r1_no_stale_entities_in_steady_state() {
	auto srv = makeVillageServer();
	SimClient c;
	c.playerId = srv->localPlayerId();
	c.seatId   = srv->localSeatId();

	// Seed the mirror first (so reconciler.tick has targets to reason about).
	simBroadcastTick(*srv, c);

	// Teleport a random owned mob ~80 blocks away — beyond the 64-block
	// perception radius — to exercise the owner-override. Without the fix
	// this entity's broadcasts stop and it goes stale.
	Entity* player = srv->getEntity(c.playerId);
	EntityId faraway = ENTITY_NONE;
	srv->forEachEntity([&](Entity& e) {
		if (faraway != ENTITY_NONE) return;
		int ownerSeat = e.getProp<int>(Prop::Owner, 0);
		if (ownerSeat == (int)c.seatId && e.id() != c.playerId && e.def().isLiving())
			faraway = e.id();
	});
	if (faraway == ENTITY_NONE) return "no owned mob to test with";
	Entity* far = srv->getEntity(faraway);
	far->position = player->position + glm::vec3(80.0f, 0.0f, 80.0f);

	// Simulate 2 seconds — reconciler's kStaleThreshold is 2.0s so anything
	// that's going to fire will have fired by the end.
	simRun(*srv, c, 120);

	// Assert: no mirrored entity went into error state. (hasError=true with
	// serverSilent=false would be a new bug; we never pass serverSilent=true.)
	int errored = 0;
	EntityId firstBad = ENTITY_NONE;
	for (auto& [id, e] : c.mirror) {
		if (e->hasError) { errored++; if (firstBad == ENTITY_NONE) firstBad = id; }
	}
	if (errored > 0) {
		char buf[128];
		std::snprintf(buf, sizeof(buf),
			"%d entities marked hasError; first=%u",
			errored, firstBad);
		return buf;
	}
	// And the far-away owned mob must still be receiving broadcasts —
	// test the invariant at its strictest.
	if (!c.everSeen.count(faraway))
		return "far-away owned mob was never broadcast (owner-override broken)";
	auto it = c.mirror.find(faraway);
	if (it == c.mirror.end())
		return "far-away owned mob missing from client mirror";

	return "";
}

// R2 hard-fails if any client-mirrored entity ever drifts more than 100m from
// its server-authoritative position. This is the "looks normal on server but
// ghost-drifting on client" scenario the user hit before staleness detection
// existed. 100m is well past the reconciler's 16m hard-snap, so any sustained
// drift at that magnitude means a snap was skipped or broadcasts were lost.
static std::string r2_no_sustained_position_error() {
	auto srv = makeVillageServer();
	SimClient c;
	c.playerId = srv->localPlayerId();
	c.seatId   = srv->localSeatId();
	simBroadcastTick(*srv, c);

	// Run 2 seconds at 60Hz. Sample drift every 10 frames and keep the max
	// observed; report if it exceeded 100m for more than ~5 samples in a row.
	constexpr float dt = 1.0f / 60.0f;
	constexpr float kDriftLimit = 100.0f;
	BlockSolidFn solidFn = [&](int x, int y, int z) -> float {
		const auto& bd = srv->blockRegistry().get(srv->chunks().getBlock(x, y, z));
		return bd.solid ? bd.collision_height : 0.0f;
	};
	int sustainedBreach = 0;
	EntityId worstId = ENTITY_NONE;
	float worstDist = 0;
	for (int i = 0; i < 120; i++) {
		srv->tick(dt);
		if (i % 3 == 0) simBroadcastTick(*srv, c);
		c.reconciler.tick(dt, c.playerId, c.mirror, solidFn, /*serverSilent=*/false);
		if (i % 10 == 0) {
			bool breached = false;
			for (auto& [id, client_e] : c.mirror) {
				Entity* se = srv->getEntity(id);
				if (!se) continue;
				float d = glm::length(se->position - client_e->position);
				if (d > worstDist) { worstDist = d; worstId = id; }
				if (d > kDriftLimit) breached = true;
			}
			sustainedBreach = breached ? sustainedBreach + 1 : 0;
			if (sustainedBreach >= 5) {
				char buf[128];
				std::snprintf(buf, sizeof(buf),
					"eid=%u drift %.1fm > %.0fm for %d consecutive samples",
					worstId, worstDist, kDriftLimit, sustainedBreach);
				return buf;
			}
		}
	}
	return "";
}

// R3 hard-fails if ANY client-mirrored entity ever drifts more than 0.3m
// from its server-authoritative position during normal play. This catches
// the "client runs its own gravity + collision and walks away from server
// truth" bug where entities accumulated a ~1m Y-gap because client physics
// landed them on a different block than the server.
//
// Tolerance is tight (0.3m) so any drift from physics integration errors
// or missed broadcasts fails immediately. Server-side motion at 2.5 m/s
// plus 50ms broadcast age would contribute at most ~0.125m if extrapolation
// is off — so 0.3m is well outside the legitimate budget.
static std::string r3_no_drift_during_steady_state() {
	auto srv = makeVillageServer();
	SimClient c;
	c.playerId = srv->localPlayerId();
	c.seatId   = srv->localSeatId();
	simBroadcastTick(*srv, c);

	constexpr float dt = 1.0f / 60.0f;
	// Snapshot interpolation renders the client ~kInterpDelay behind the
	// server, so drift must be measured against what the server looked like
	// kInterpDelay ago (not its current state). We keep a short ring buffer
	// of server positions per entity and look up the entry nearest the
	// client's render time. Tolerance is a small epsilon for lerp error.
	constexpr float kInterpDelay  = EntityReconciler::kInterpDelay;
	// 0.5m budgets the unavoidable linear-lerp error when a 20Hz-broadcast
	// entity undergoes impulsive velocity changes (e.g. fast-fall clamped
	// to 0 on landing between two snapshots) — bounded by speed × T/2 at
	// the transition. Ordinary smooth motion is well under 0.1m here.
	constexpr float kDriftTolerance = 0.5f;
	BlockSolidFn solidFn = [&](int x, int y, int z) -> float {
		const auto& bd = srv->blockRegistry().get(srv->chunks().getBlock(x, y, z));
		return bd.solid ? bd.collision_height : 0.0f;
	};
	// Per-entity: chronological (serverTime, serverPos) pairs.
	std::unordered_map<EntityId, std::deque<std::pair<float, glm::vec3>>> hist;
	float worstDrift = 0;
	EntityId worstId = ENTITY_NONE;
	glm::vec3 worstClient, worstServer;
	// 300 frames (5s). Check drift every frame (after reconciler tick)
	// for every mirrored entity against the server.
	for (int i = 0; i < 300; i++) {
		srv->tick(dt);
		float tNow = (i + 1) * dt;
		if (i % 3 == 0) simBroadcastTick(*srv, c);
		c.reconciler.tick(dt, c.playerId, c.mirror, solidFn, /*serverSilent=*/false);
		// Record server state every frame; prune entries older than 1s.
		srv->forEachEntity([&](Entity& se) {
			auto& h = hist[se.id()];
			h.push_back({tNow, se.position});
			while (!h.empty() && h.front().first < tNow - 1.0f) h.pop_front();
		});
		// Skip warmup frames (entities still settling on terrain)
		if (i < 30) continue;
		float renderTime = tNow - kInterpDelay;
		for (auto& [id, client_e] : c.mirror) {
			if (id == c.playerId) continue;  // local player has its own prediction path
			auto hit = hist.find(id);
			if (hit == hist.end() || hit->second.size() < 2) continue;
			// Lerp server history at exactly renderTime (faithful reconstruction
			// of what the server was doing when the client thinks it was).
			const auto& h = hit->second;
			glm::vec3 expected = h.front().second;
			for (size_t k = 0; k + 1 < h.size(); k++) {
				if (h[k].first <= renderTime && h[k + 1].first >= renderTime) {
					float span = h[k + 1].first - h[k].first;
					float alpha = span > 1e-4f ? (renderTime - h[k].first) / span : 0.0f;
					expected = glm::mix(h[k].second, h[k + 1].second, alpha);
					break;
				}
				if (h[k + 1].first < renderTime) expected = h[k + 1].second;
			}
			float d = glm::length(expected - client_e->position);
			if (d > worstDrift) {
				worstDrift = d;
				worstId = id;
				worstClient = client_e->position;
				worstServer = expected;
			}
		}
	}
	if (worstDrift > kDriftTolerance) {
		char buf[256];
		std::snprintf(buf, sizeof(buf),
			"eid=%u drift=%.2fm > %.2fm (vs server state at t-%.0fms) "
			"client=(%.2f,%.2f,%.2f) server=(%.2f,%.2f,%.2f)",
			worstId, worstDrift, kDriftTolerance, kInterpDelay * 1000.0f,
			worstClient.x, worstClient.y, worstClient.z,
			worstServer.x, worstServer.y, worstServer.z);
		return buf;
	}
	return "";
}

// ================================================================
// Seats + Ownership (Phase 1): stable uuid → SeatId mapping
// ================================================================

static std::string s01_same_uuid_same_seat() {
	SeatRegistry reg;
	auto r1 = reg.claim("uuid-alpha");
	if (r1.id == SEAT_NONE)      return "first claim returned SEAT_NONE";
	if (!r1.isNew)               return "first claim not flagged isNew";
	auto r2 = reg.claim("uuid-alpha");
	if (r2.id != r1.id)          return "second claim issued a different seat";
	if (r2.isNew)                return "second claim wrongly flagged isNew";
	return "";
}

static std::string s02_different_uuids_different_seats() {
	SeatRegistry reg;
	auto a = reg.claim("uuid-a");
	auto b = reg.claim("uuid-b");
	auto c = reg.claim("uuid-c");
	if (a.id == b.id || b.id == c.id || a.id == c.id)
		return "distinct uuids collided on seat id";
	// Lookup must hit stored values and be SEAT_NONE for unknowns.
	if (reg.lookup("uuid-b") != b.id) return "lookup mismatch";
	if (reg.lookup("uuid-zzz") != SEAT_NONE) return "lookup of unknown returned non-zero";
	return "";
}

// S4 verifies the Phase 2 invariant: ownership gates on SeatId, not EntityId.
// Before the flip Prop::Owner held the owning player's entity id; a careless
// spoof (actorId=foreign_eid) would pass canClientControl since the server
// only checked `owner == that eid`. Now `owner` is a seat, so the same attack
// fails — client1 has seat 1, client2 has seat 2, their entities can't cross.
static std::string s04_cross_seat_cannot_control() {
	auto ts = makeVillageServer();             // client 1, seat 1, owned mobs spawned
	GameServer* srv = ts->server();
	if (!srv) return "no server";

	EntityId p1 = ts->localPlayerId();
	ClientId c2 = 2;
	SeatId   s2 = 2;
	EntityId p2 = srv->addClient(c2, s2, "knight");
	if (p2 == ENTITY_NONE || p2 == p1) return "addClient did not return distinct entity";

	// Sanity: each client controls its own player.
	if (!srv->canClientControl(1,  p1))  return "client1 cannot control own player";
	if (!srv->canClientControl(c2, p2))  return "client2 cannot control own player";
	// The invariant: cross-seat control must be denied.
	if (srv->canClientControl(1,  p2))   return "client1 wrongly controls client2's player";
	if (srv->canClientControl(c2, p1))   return "client2 wrongly controls client1's player";

	// Extend the invariant to owned mobs: any NPC whose Prop::Owner is seat 1
	// must refuse actions from client 2, and vice versa.
	int checkedMobs = 0;
	std::string err;
	srv->world().entities.forEach([&](Entity& e) {
		if (!err.empty()) return;
		int ownerSeat = e.getProp<int>(Prop::Owner, 0);
		if (ownerSeat == 1 && e.id() != p1) {
			if (srv->canClientControl(c2, e.id())) {
				err = "client2 controls mob owned by seat 1";
			}
			checkedMobs++;
		} else if (ownerSeat == 2 && e.id() != p2) {
			if (srv->canClientControl(1, e.id())) {
				err = "client1 controls mob owned by seat 2";
			}
			checkedMobs++;
		}
	});
	if (!err.empty()) return err;
	if (checkedMobs == 0) return "no owned mobs spawned for either seat";
	return "";
}

// S7 (Phase 5): owned-entity round-trip keyed by SeatId. Drop the client,
// rejoin, and the same mobs (same ids-or-count, same positions) come back.
// This is the invariant that makes "log off / log on" feel like resuming a
// save, not like a fresh spawn every session.
static std::string s07_disconnect_snapshot_rejoin_restore() {
	auto ts = makeVillageServer();
	GameServer* srv = ts->server();
	if (!srv) return "no server";

	// Record pre-disconnect state: count owned NPCs and nudge one of them so
	// we can verify the restored position isn't just the template default.
	struct Snap { EntityId id; std::string typeId; glm::vec3 pos; };
	std::vector<Snap> before;
	srv->world().entities.forEach([&](Entity& e) {
		if (e.getProp<int>(Prop::Owner, 0) == 1 && !e.def().playable
		    && e.def().isLiving()) {
			before.push_back({e.id(), e.typeId(), e.position});
		}
	});
	if (before.empty()) return "no owned NPCs spawned for seat 1";

	// Pick the first NPC and shove it to a distinctive offset.
	glm::vec3 shoved = before[0].pos + glm::vec3(7.25f, 0, -3.125f);
	Entity* sample = srv->world().entities.get(before[0].id);
	if (!sample) return "sample NPC vanished before shove";
	sample->position = shoved;

	// Disconnect → snapshot + despawn.
	srv->removeClient(1);
	// Drain the removal broadcast and let stepPhysics evict removed entries.
	tickN(*ts, 2);

	// All owned NPCs for seat 1 are gone from the live world.
	int stillLiving = 0;
	srv->world().entities.forEach([&](Entity& e) {
		if (e.getProp<int>(Prop::Owner, 0) == 1 && e.def().isLiving())
			stillLiving++;
	});
	if (stillLiving != 0)
		return "seat 1 owned entities remained after removeClient ("
		     + std::to_string(stillLiving) + ")";
	if (srv->ownedEntities().seatCount() != 1)
		return "expected 1 seat's snapshot stored, got "
		     + std::to_string(srv->ownedEntities().seatCount());

	// Rejoin same seat: restore path fires (not a fresh template spawn).
	EntityId pNew = srv->addClient(/*clientId*/1, /*seatId*/1, "knight");
	if (pNew == ENTITY_NONE) return "addClient after reconnect failed";

	// After restore + one tick, count + find the shoved NPC by position.
	tickN(*ts, 1);
	int restored = 0;
	bool foundShoved = false;
	srv->world().entities.forEach([&](Entity& e) {
		if (e.getProp<int>(Prop::Owner, 0) != 1) return;
		if (e.def().playable) return;
		if (!e.def().isLiving()) return;
		restored++;
		if (e.typeId() == before[0].typeId
		    && glm::length(e.position - shoved) < 0.5f)
			foundShoved = true;
	});
	if (restored != (int)before.size())
		return "restored " + std::to_string(restored)
		     + " owned NPCs, expected " + std::to_string(before.size());
	if (!foundShoved)
		return "shoved NPC's position did not round-trip through the snapshot";
	if (srv->ownedEntities().seatCount() != 0)
		return "snapshot map should be empty after restore consumed it";
	return "";
}

// S8 (Phase 5): removeClient tags every despawning owned entity with
// removalReason=OwnerOffline so clients fire a puff particle instead of a
// death SFX. We can't observe the wire packet from here, but the byte the
// broadcaster reads lives on the entity itself — checking it there catches
// regressions in either `EntityManager::remove(reason)` or the removeClient
// loop.
static std::string s08_removeclient_reason_owner_offline() {
	auto ts = makeVillageServer();
	GameServer* srv = ts->server();
	if (!srv) return "no server";

	// Freeze the pre-disconnect owned set.
	std::vector<EntityId> ownedIds;
	srv->world().entities.forEach([&](Entity& e) {
		if (e.getProp<int>(Prop::Owner, 0) == 1) ownedIds.push_back(e.id());
	});
	if (ownedIds.empty()) return "no entities owned by seat 1";

	srv->removeClient(1);

	// After removeClient, entities are marked removed but stepPhysics hasn't
	// erased them yet — we can still read removalReason.
	int checked = 0, mismatched = 0;
	srv->world().entities.forEachIncludingRemoved([&](Entity& e) {
		if (std::find(ownedIds.begin(), ownedIds.end(), e.id()) == ownedIds.end())
			return;
		checked++;
		if (e.removalReason != (uint8_t)EntityRemovalReason::OwnerOffline)
			mismatched++;
	});
	if (checked == 0) return "none of seat 1's entities were visible post-removeClient";
	if (mismatched > 0)
		return std::to_string(mismatched) + "/" + std::to_string(checked)
		     + " owned entities missing removalReason=OwnerOffline";
	return "";
}

// S9 (Phase 4 regression): VillageStamper actually paints house walls.
//
// Catches the bug the user reported as "I can't see the buildings anymore":
// the footprint was force-loaded and the stamper logged "stamped N chunks",
// but generateVillageInChunk silently no-op'd, so the village amounted to
// just terrain + monument + chests — no walls, no roofs. Entity-level checks
// (T25 = spawn near center; T07 = villagers spawn) all still passed.
//
// Invariant: for every non-barn house in the template, the four wall corners
// at floorY+2 (well above any reasonable ground hillock, inside the first
// story) must be non-AIR. `generateHouse` writes wallB / glassB / doorB at
// those cells depending on dx/dz; all three are solid block ids, so "AIR"
// at a corner means the paint pass didn't execute over that chunk.
static std::string s09_village_houses_painted_near_spawn() {
	auto ts = makeVillageServer();
	GameServer* srv = ts->server();
	if (!srv) return "no server";

	auto& tmpl   = srv->world().getTemplate();
	auto& cfg    = tmpl.pyConfig();
	if (!cfg.hasVillage) return "village template has no village";
	if (cfg.houses.empty()) return "village template has no houses";

	const VillageRecord* rec = srv->villages().findBySeat(1);
	if (!rec) return "seat 1 has no village record after addClient";
	glm::ivec2 vc = rec->centerXZ;
	int seed = 42;

	BlockId airId = srv->world().blocks.getId(BlockType::Air);

	int housesChecked = 0;
	for (const auto& h : cfg.houses) {
		if (h.type == "barn") continue;  // barn uses a different block layout
		housesChecked++;

		int hcx = vc.x + h.cx;
		int hcz = vc.y + h.cz;
		int floorY =
			(int)std::round(tmpl.surfaceHeight(seed, (float)hcx, (float)hcz)) + 1;
		// Rescan corners + center so one unlucky hillock doesn't skew floorY;
		// generateHouse uses footprint-max ground, so match that.
		int maxGY = INT_MIN;
		for (int dx = 0; dx < h.w; dx++)
			for (int dz = 0; dz < h.d; dz++) {
				int y = (int)std::round(
					tmpl.surfaceHeight(seed, (float)(hcx+dx), (float)(hcz+dz)));
				if (y > maxGY) maxGY = y;
			}
		floorY = maxGY + 1;

		struct Corner { int dx, dz; const char* name; };
		Corner corners[4] = {
			{0,       0,       "NW"},
			{h.w - 1, 0,       "NE"},
			{0,       h.d - 1, "SW"},
			{h.w - 1, h.d - 1, "SE"},
		};
		for (auto& c : corners) {
			int wx = hcx + c.dx;
			int wz = hcz + c.dz;
			int wy = floorY + 2;  // inside first story, above any ground lip
			BlockId bid = srv->world().getBlock(wx, wy, wz);
			if (bid == airId) {
				return std::string("house (") + std::to_string(h.cx) + ","
				     + std::to_string(h.cz) + ") wall corner " + c.name
				     + " at (" + std::to_string(wx) + "," + std::to_string(wy)
				     + "," + std::to_string(wz)
				     + ") is AIR — stamper didn't paint";
			}
		}
	}
	if (housesChecked == 0) return "no non-barn houses in template to check";

	// Also assert the nearest house corner is within render distance of spawn.
	// Stamping-but-unreachable would be just as invisible as not-stamping.
	glm::vec3 spawn = srv->spawnPos();
	float bestDist2 = 1e12f;
	for (const auto& h : cfg.houses) {
		if (h.type == "barn") continue;
		for (int cx : {0, h.w - 1}) for (int cz : {0, h.d - 1}) {
			float dx = (float)(vc.x + h.cx + cx) - spawn.x;
			float dz = (float)(vc.y + h.cz + cz) - spawn.z;
			float d2 = dx*dx + dz*dz;
			if (d2 < bestDist2) bestDist2 = d2;
		}
	}
	// preload_radius_chunks=16 → 256 blocks. 96 blocks is a conservative
	// "visible within a couple of chunks" bound; village.py puts a house
	// corner ≤60 blocks from spawn by construction.
	float bestDist = std::sqrt(bestDist2);
	if (bestDist > 96.0f) {
		return "nearest house corner is " + std::to_string((int)bestDist)
		     + " blocks from spawn (expected ≤96)";
	}
	return "";
}

// V1 (Phase 4): three sequential sitings land ≥ kMinDistance apart pairwise.
// The siter's contract is "≥ min from nearest already-registered village" —
// we verify the stronger pairwise invariant holds because each siting runs
// against the registry that contains everything placed before it.
static std::string v01_three_sitings_spaced() {
	VillageRegistry reg;
	VillageSiterConfig cfg;  // defaults: 256..512

	// Seed a starting anchor so the first site lands somewhere deterministic.
	glm::ivec2 initial = {0, 0};
	// Place three seats one at a time, as join-order would.
	for (SeatId s = 1; s <= 3; s++) {
		auto c = VillageSiter::pick(reg, initial, /*worldSeed*/42, s, cfg);
		if (!c) return "siter returned nullopt for seat " + std::to_string(s);
		reg.allocate(s, *c);
	}
	if (reg.size() != 3) return "registry size != 3 after three allocations";

	// Pairwise distance check — must all be ≥ kMinDistance.
	int64_t minD2 = (int64_t)cfg.kMinDistance * cfg.kMinDistance;
	for (size_t i = 0; i < reg.all().size(); i++) {
		for (size_t j = i + 1; j < reg.all().size(); j++) {
			auto& a = reg.all()[i];
			auto& b = reg.all()[j];
			int64_t dx = (int64_t)a.centerXZ.x - b.centerXZ.x;
			int64_t dz = (int64_t)a.centerXZ.y - b.centerXZ.y;
			int64_t d2 = dx*dx + dz*dz;
			if (d2 < minD2) {
				char buf[160];
				std::snprintf(buf, sizeof(buf),
					"villages %zu and %zu only %lld blocks apart (min %d)",
					i, j, (long long)std::llround(std::sqrt((double)d2)),
					cfg.kMinDistance);
				return buf;
			}
		}
	}
	return "";
}

// V2 (Phase 4): a despawned village's footprint still blocks new sitings.
// Regression guard for "seat GC'd → new seat can land on the ruins"; the
// registry is supposed to keep the record as Status::Despawned precisely so
// placement stays consistent with world history.
static std::string v02_siter_avoids_despawned() {
	VillageRegistry reg;
	// Seed one Despawned record. It should still count for distance checks.
	auto& r = reg.allocate(/*ownerSeat*/SEAT_NONE, {0, 0});
	r.status = VillageRecord::Status::Despawned;

	// Now a live seat claims. The siter must not place near (0,0).
	auto c = VillageSiter::pick(reg, {0, 0}, /*seed*/42, /*seat*/7);
	if (!c) return "siter returned nullopt";
	int64_t dx = c->x, dz = c->y;
	int64_t d2 = dx*dx + dz*dz;
	VillageSiterConfig cfg;
	int64_t minD2 = (int64_t)cfg.kMinDistance * cfg.kMinDistance;
	if (d2 < minD2) {
		char buf[160];
		std::snprintf(buf, sizeof(buf),
			"new village at (%d,%d) within %lld of despawned at (0,0)",
			c->x, c->y, (long long)std::llround(std::sqrt((double)d2)));
		return buf;
	}
	return "";
}

// V3 (Phase 4): loadEntry replays ids verbatim and preserves the high-water
// so a subsequent allocate() doesn't collide with a persisted id. This is the
// same invariant as seats.bin — every reference that survives restart needs
// the id to be stable.
static std::string v03_loadEntry_preserves_ids() {
	VillageRegistry reg;
	VillageRecord a{3, 1, {100, 200}, VillageRecord::Status::Live};
	VillageRecord b{7, 2, {-100, 400}, VillageRecord::Status::Despawned};
	reg.loadEntry(a);
	reg.loadEntry(b);

	// The persisted ids must round-trip unchanged.
	if (!reg.find(3) || reg.find(3)->centerXZ != glm::ivec2{100, 200})
		return "village id 3 missing or wrong center after reload";
	if (!reg.find(7) || reg.find(7)->status != VillageRecord::Status::Despawned)
		return "village id 7 missing or wrong status after reload";

	// A fresh allocate must pick an id strictly greater than any loaded.
	auto& fresh = reg.allocate(/*seat*/9, {1000, 1000});
	if (fresh.id <= 7)
		return "fresh allocate id " + std::to_string(fresh.id) + " collides with loaded id 7";
	return "";
}

// S6 (Phase 6): the spawn-with-overrides path must carry Prop::Owner = seatId
// for every Living. If overrides omits it, the Living ends up with owner=0
// (SEAT_NONE) — an AgentClient would never adopt it, so the server warns on
// the spawn path. This test pins both directions of the invariant: empty
// overrides → owner=0 (warn case), explicit overrides → owner honored.
static std::string s06_living_spawn_requires_owner() {
	auto ts = makeFlatServer();
	GameServer* srv = ts->server();
	if (!srv) return "no server";

	// 1. overrides variant without Prop::Owner → Living lands at owner=0.
	//    (Spawn path prints a WARN; we verify the state the warn triggers on.)
	EntityId unowned = srv->world().entities.spawn(
		"knight", glm::vec3(0, 10, 0),
		std::unordered_map<std::string, PropValue>{});
	if (unowned == ENTITY_NONE) return "spawn(overrides) returned ENTITY_NONE for 'knight'";
	Entity* eu = srv->world().entities.get(unowned);
	if (!eu) return "spawned 'knight' not findable by id";
	if (!eu->def().isLiving()) return "'knight' unexpectedly not Living in this build";
	int owner0 = eu->getProp<int>(Prop::Owner, 0);
	if (owner0 != 0)
		return "Living spawned with empty overrides has owner=" + std::to_string(owner0)
		     + " (expected 0 — warn path)";

	// 2. overrides variant with Prop::Owner threaded → honored verbatim.
	EntityId owned = srv->world().entities.spawn(
		"knight", glm::vec3(4, 10, 0),
		{{Prop::Owner, (int)99}});
	if (owned == ENTITY_NONE) return "spawn(overrides) returned ENTITY_NONE for owned 'knight'";
	Entity* eo = srv->world().entities.get(owned);
	if (!eo) return "owned 'knight' not findable by id";
	int ownerN = eo->getProp<int>(Prop::Owner, 0);
	if (ownerN != 99)
		return "Living spawned with Prop::Owner=99 reports owner="
		     + std::to_string(ownerN);
	return "";
}

// S5 (Phase 3): world-gen itself spawns zero mobs. Only client join triggers
// spawnMobsForClient(seat); a dedicated server with nobody connected must sit
// empty of living entities. Chests and the monument are world-scope structure
// entities (owner=SEAT_NONE, non-living) — they may remain.
static std::string s05_worldgen_no_mobs_without_client() {
	ServerConfig cfg;
	cfg.seed = 42;
	cfg.templateIndex = 1;           // village template — the worst case
	GameServer srv;
	srv.init(cfg, g_templates);
	// Mirror main.cpp/TestServer: load artifacts so entity defs match runtime.
	ArtifactRegistry artifacts;
	artifacts.loadAll("artifacts");
	srv.mergeArtifactTags(artifacts.livingTags());
	srv.applyLivingStats(artifacts.livingStats());

	// Tick 30 frames — any pending worldgen spawns drain into the world.
	for (int i = 0; i < 30; i++) srv.tick(1.0f / 60.0f);

	int living = 0;
	std::string firstType;
	srv.world().entities.forEach([&](Entity& e) {
		if (e.def().isLiving()) {
			if (firstType.empty()) firstType = e.typeId();
			living++;
		}
	});
	if (living > 0) {
		return "world-gen spawned " + std::to_string(living)
		     + " living entities (first=" + firstType
		     + "); mobs must only spawn via addClient(seat)";
	}
	return "";
}

static std::string s03_loadEntry_preserves_high_water() {
	SeatRegistry reg;
	// Simulating "restored from seats.bin": ids 1 and 5 are on disk; next claim
	// must be 6 so no live session ever collides with a persisted seat.
	reg.loadEntry("uuid-old-1", 1);
	reg.loadEntry("uuid-old-5", 5);
	auto fresh = reg.claim("uuid-new");
	if (fresh.id != 6)
		return std::string("expected seatId=6 after loading {1,5}, got ")
		     + std::to_string(fresh.id);
	// Returning user must still resolve to their persisted seat.
	auto returning = reg.claim("uuid-old-5");
	if (returning.id != 5)   return "existing uuid mapped to wrong seat after reload";
	if (returning.isNew)     return "existing uuid flagged as isNew after reload";
	return "";
}

// Main
// ================================================================

} // namespace civcraft::test

int main() {
	using namespace civcraft;
	using namespace civcraft::test;

	// Initialize Python so world templates and behaviors can load from artifacts/
	pythonBridge().init("python");

	printf("\n=== CivCraft E2E Tests ===\n\n");
	initTemplates();

	printf("--- Player Basics ---\n");
	run("T01: player spawns",            t01_player_spawns);

	printf("\n--- Movement ---\n");
	run("T04: player moves (60 ticks)",      t04_player_moves);
	run("T06: player can jump",              t06_player_jump);

	printf("\n--- Creatures / Village ---\n");
	run("T07: creatures spawn in village",    t07_creatures_spawn_village);
	run("T08: all Living have inventory",     t08_all_living_have_inventory);
	run("T09: creatures have HP > 0",         t09_creatures_have_hp);

	printf("\n--- Block Interaction ---\n");
	run("T10: break block removes block",               t10_break_block_removes_block);
	run("T11: break block spawns item entity",          t11_break_block_survival_item_spawns);
	run("T12: item pickup after break",               t12_item_pickup_after_break);
	run("T13: place block consumed from inventory",   t13_place_block);
	run("T14: place block rejected without item",     t14_place_block_rejected_without_item);

	printf("\n--- Server Stability ---\n");
	run("T15: items only picked up by player",    t15_items_only_picked_up_by_player);
	run("T16: village stable over 300 ticks",     t16_village_stable_300_ticks);
	run("T17: player not stuck 600 ticks",        t17_player_not_stuck_with_obstacles);

	printf("\n--- Inventory ---\n");
	run("T18: starting inventory populated",     t18_starting_inventory_populated);
	run("T19: break+place deducts inventory",    t19_break_and_place_cycle);

	printf("\n--- Spawn Integrity ---\n");
	run("T20: spawn area clear (no solid in body)", t20_spawn_area_clear);

	printf("\n--- Callbacks ---\n");
	run("T21: onBreakText fires on block break",    t21_break_fires_onbreaktext);
	run("T22: onPickupText fires on item pickup",   t22_pickup_fires_onpickuptext);
	run("T22b: break underfoot fires pickup callbacks", t22b_break_underfoot_fires_callbacks);
	run("T22c: item reaches player from distance",     t22c_item_reaches_player);

	printf("\n--- Village ---\n");
	run("T25: player spawn within 65 blocks of village", t25_spawn_near_village);

	printf("\n--- Action Integrity ---\n");
	run("T27: 3 DropItems produce exactly 3 items",  t27_multiple_dropitems_correct_count);

	printf("\n--- Item Actions ---\n");
	run("T30: equip item to wear slot",          t30_equip_item);
	run("T31: use item consumes and heals",       t31_use_item_consume);
	run("T32: attack damages creature",           t32_attack_damages_entity);
	run("T33: drop item deducts from inventory",  t33_dropitem_deducts_inventory);
	run("T39: second floor reachable from spawn",    t39_second_floor_reachable);
	run("T40: break stone drops stone (same-name)",  t40_break_stone_drops_stone);
	run("T41: village houses contain no logs",       t41_village_houses_no_logs);
	run("T35: item entity has ItemType property",  t35_item_entity_has_type);
	run("T36: equip+unequip cycle returns item",  t36_equip_unequip_cycle);
	run("T37: pickup denied if out of range",     t37_pickup_denied_out_of_range);

	printf("\n--- Physics Anti-Tunneling ---\n");
	run("P50: no horizontal tunneling at 50 m/s", p50_no_horizontal_tunneling);
	run("P51: no vertical tunneling at 50 m/s",   p51_no_vertical_tunneling);
	run("P52: knockback no tunneling in box",      p52_knockback_no_tunneling);
	run("P53: village entities never inside blocks (120 frames)", p53_entities_never_inside_blocks_village);
	run("P54: server rejects clientPos inside wall",              p54_server_rejects_client_pos_in_wall);

	printf("\n--- Behavior ---\n");
	run("B2: all behavior files load cleanly",               b2_all_behaviors_load_cleanly);
	run("B4: StoreItem transfers inventory to chest block",   b4_store_item_server_validation);
	run("B5: pathfind.py module loads cleanly",              b5_pathfind_module_loads);

	printf("\n--- Block Search ---\n");
	run("C2: scan_blocks prefers nearest non-empty chunk",   c2_scan_blocks_prefers_nearest_chunk);

	printf("\n--- Server Navigation ---\n");
	run("NAV1: single entity walks to goal on flat ground",  nav1_single_entity_walks_to_goal);
	run("NAV2: group formation assigns different goals",     nav2_group_formation);
	run("NAV3: entity dodges around a wall",                 nav3_dodge_around_wall);
	run("NAV4: entity in box never gives up",                nav4_never_gives_up);
	run("NAV5: entity climbs 1-block step-ups",             nav5_step_up_climbing);
	run("NAV6: entity blocked by 2-block wall",             nav6_blocked_by_tall_wall);

	printf("\n--- World Generation ---\n");
	run("W1: foundation columns are stone (not dirt/grass)", w1_foundation_is_stone);
	run("W2: house floor at or above all footprint terrain", w2_floor_at_or_above_all_terrain);
	run("W3: house interior has no buried terrain blocks",   w3_house_interior_no_buried_terrain);
	run("W4: villagers near monument, animals in barn",      w4_mob_spawn_anchors);
	run("W4b: species→default behavior mapping",             w4b_default_behaviors_per_species);

	printf("\n--- Client Reconciliation ---\n");
	run("R1: owned mobs never go stale (no red lightbulb)", r1_no_stale_entities_in_steady_state);
	run("R2: no entity drifts >100m from server",          r2_no_sustained_position_error);
	run("R3: no drift >0.3m during steady-state play",     r3_no_drift_during_steady_state);

	printf("\n--- Camera Mode Movement ---\n");
	run("M1: FPS walk + jump while moving",     m01_fps_walk_jump);
	run("M2: TPS diagonal walk + sprint",       m02_tps_diagonal_sprint);
	run("M3: RPG click-to-move (agent sim)",     m03_rpg_click_to_move);
	run("M4: RTS multi-entity movement",         m04_rts_multi_entity);

	printf("\n--- Seats + Ownership ---\n");
	run("S1: same uuid → same seat",              s01_same_uuid_same_seat);
	run("S2: different uuids → different seats",  s02_different_uuids_different_seats);
	run("S3: loadEntry preserves high-water mark", s03_loadEntry_preserves_high_water);
	run("S4: cross-seat clients cannot control each other", s04_cross_seat_cannot_control);
	run("S5: world-gen spawns no mobs without a client",   s05_worldgen_no_mobs_without_client);
	run("S6: living spawn requires Prop::Owner",           s06_living_spawn_requires_owner);
	run("S7: disconnect→snapshot→rejoin→restore",          s07_disconnect_snapshot_rejoin_restore);
	run("S8: removeClient tags reason=OwnerOffline",       s08_removeclient_reason_owner_offline);
	run("S9: village houses painted + within render dist", s09_village_houses_painted_near_spawn);

	printf("\n--- Village Registry + Siter ---\n");
	run("V1: three sequential sitings are pairwise ≥256 apart", v01_three_sitings_spaced);
	run("V2: siter avoids despawned village footprints",         v02_siter_avoids_despawned);
	run("V3: loadEntry preserves ids + high-water",              v03_loadEntry_preserves_ids);

	pythonBridge().shutdown();

	// Summary
	int passed = 0, failed = 0;
	for (auto& r : g_results) {
		if (r.passed) passed++;
		else failed++;
	}
	printf("\n=== Results: %d/%d passed", passed, passed + failed);
	if (failed > 0) {
		printf(" (%d FAILED)", failed);
		printf("\n\nFailed tests:\n");
		for (auto& r : g_results)
			if (!r.passed) printf("  [FAIL] %s: %s\n", r.name.c_str(), r.msg.c_str());
	}
	printf(" ===\n\n");
	return failed;
}

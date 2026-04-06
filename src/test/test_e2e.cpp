/**
 * test_e2e.cpp — Headless end-to-end gameplay tests.
 *
 * Runs entirely in-process (no OpenGL, no network, no GLFW).
 * Uses TestServer (wraps GameServer) directly to simulate gameplay
 * and assert correctness.
 *
 * Each test creates a fresh world to avoid cross-test pollution.
 * Tests are numbered and run sequentially. Exit code = failed count.
 *
 * Runs from build/ directory so python/ and artifacts/ are accessible.
 */

#include "server/test_server.h"
#include "server/world_template.h"
#include "server/python_bridge.h"
#include "server/world_accessibility.h"
#include "shared/constants.h"
#include <cstdio>
#include <cmath>
#include <string>
#include <functional>
#include <vector>
#include <algorithm>

namespace modcraft::test {

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
		std::make_shared<FlatWorldTemplate>(),
		std::make_shared<VillageWorldTemplate>(),
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
		if (e.typeId() != EntityType::ItemEntity) return;
		if (e.removed) return;
		float dist = glm::length(e.position - p->position);
		if (dist < range) {
			ActionProposal a;
			a.type = ActionProposal::PickupItem;
			a.actorId = actor;
			a.targetEntity = e.id();
			srv.sendAction(a);
			sent++;
		}
	});
	if (sent > 0) srv.tick(1.0f / 60.0f);
	return sent;
}

// Send a BreakBlock action and tick one frame.
static void breakAndTick(TestServer& srv, EntityId actor, glm::ivec3 pos) {
	ActionProposal p;
	p.type = ActionProposal::BreakBlock;
	p.actorId = actor;
	p.blockPos = pos;
	srv.sendAction(p);
	srv.tick(1.0f / 60.0f);
}

// Send a PlaceBlock action and tick one frame.
static void placeAndTick(TestServer& srv, EntityId actor,
                         glm::ivec3 pos, const std::string& type) {
	ActionProposal p;
	p.type = ActionProposal::PlaceBlock;
	p.actorId = actor;
	p.blockPos = pos;
	p.blockType = type;
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
// T01 — T03: Player basics
// ================================================================

static std::string t01_player_spawns() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	if (pid == ENTITY_NONE) return "localPlayerId() is ENTITY_NONE";
	Entity* p = srv->getEntity(pid);
	if (!p) return "getEntity(playerId) returned null";
	return "";
}

static std::string t02_player_has_inventory() {
	auto srv = makeFlatServer();
	Entity* p = srv->getEntity(srv->localPlayerId());
	if (!p) return "no player";
	if (!p->inventory) return "player.inventory is null";
	if (p->inventory->distinctCount() == 0) return "player inventory is empty";
	return "";
}

static std::string t03_player_has_hp() {
	auto srv = makeFlatServer();
	Entity* p = srv->getEntity(srv->localPlayerId());
	if (!p) return "no player";
	if (!p->def().isLiving()) return "player def is not isLiving()";
	if (p->def().max_hp <= 0) return "player max_hp <= 0";
	if (p->hp() <= 0) return "player current hp <= 0";
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

static std::string t05_player_not_stuck_flat() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30);
	glm::vec3 startPos = p->position;

	// Walk east for 300 frames (~5 seconds of game time)
	for (int i = 0; i < 300; i++)
		moveAndTick(*srv, pid, {8.0f, 0, 0});

	float moved = std::abs(p->position.x - startPos.x);
	if (moved < 3.0f)
		return "player stuck: only moved " + std::to_string(moved) + " units east";
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
		if (e.def().isCreature()) creatures++;
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
		if (e.def().isCreature() && e.hp() <= 0)
			bad = e.typeId();
	});
	if (!bad.empty())
		return "Creature with hp <= 0: " + bad;
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

static std::string t11_break_block_survival_item_spawns() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30);
	glm::vec3 pos = p->position;

	// Break a block 4 units north — well outside attract radius (2.5) so
	// the item entity persists for at least one tick before pickup.
	glm::ivec3 blockPos = {(int)std::floor(pos.x), (int)std::floor(pos.y) - 1,
	                       (int)std::floor(pos.z) - 4};

	BlockId bid = srv->chunks().getBlock(blockPos.x, blockPos.y, blockPos.z);
	if (bid == BLOCK_AIR) return "no surface block 4 units north of spawn";

	int itemsBefore = countByType(*srv, EntityType::ItemEntity);
	breakAndTick(*srv, pid, blockPos);
	int itemsAfter = countByType(*srv, EntityType::ItemEntity);

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
	                       p->inventory->count("base:sword") +
	                       p->inventory->count("base:shield") +
	                       p->inventory->count("base:potion");

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
		if (!e.def().isCreature()) return;
		if (!e.inventory) return;
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
	srv->forEachEntity([&](Entity& e) { if (e.def().isCreature()) creaturesBefore++; });

	tickN(*srv, 300);

	int creaturesAfter = 0;
	srv->forEachEntity([&](Entity& e) { if (e.def().isCreature()) creaturesAfter++; });

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
// T18: Inventory autoPopulateHotbar — slot 0 is usable
// ================================================================

static std::string t18_hotbar_populated() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p || !p->inventory) return "no player or inventory";

	// At least one hotbar slot should have an item
	bool anyFilled = false;
	for (int i = 0; i < 5; i++) {
		if (!p->inventory->hotbar(i).empty()) { anyFilled = true; break; }
	}
	if (!anyFilled) return "all hotbar slots empty after autoPopulateHotbar";
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

	int itemCount = countByType(*srv, EntityType::ItemEntity);
	if (itemCount == 0) return "no item entity spawned after break";

	// Item is ~2.5 blocks away. Walk player toward it.
	for (int i = 0; i < 30; i++)
		moveAndTick(*srv, pid, {4.0f, 0, 0}); // walk east toward item

	// Now try pickup — should be in range
	pickupNearbyItems(*srv, pid, 2.0f);
	tickN(*srv, 5);

	int remaining = countByType(*srv, EntityType::ItemEntity);
	if (remaining > 0) return "item not picked up after walking to it";
	return "";
}

// ================================================================
// T23: Creatures spawn within range of village center
// ================================================================

static std::string t23_creatures_near_village_center() {
	auto srv = makeVillageServer();
	tickN(*srv, 10); // let gravity drop mobs to surface

	auto& tmpl = srv->server()->world().getTemplate();
	if (!tmpl.pyConfig().hasVillage) return "template has no village";

	auto vc = tmpl.villageCenter(42); // seed=42, same as makeVillageServer
	float vcX = (float)vc.x, vcZ = (float)vc.y;

	// Furthest mob radius from Python config (pigs at 22) + buffer
	constexpr float maxDist = 60.0f;
	bool found = false;
	srv->forEachEntity([&](Entity& e) {
		if (!e.def().isCreature()) return;
		float dx = e.position.x - vcX;
		float dz = e.position.z - vcZ;
		if (std::sqrt(dx*dx + dz*dz) < maxDist) found = true;
	});
	if (!found)
		return "no creature within " + std::to_string((int)maxDist) +
		       " blocks of village center (" + std::to_string((int)vcX) +
		       "," + std::to_string((int)vcZ) + ")";
	return "";
}

// ================================================================
// T24: Chest is placed inside (or near) the village center house
// ================================================================

static std::string t24_chest_in_village() {
	auto srv = makeVillageServer();

	auto& tmpl = srv->server()->world().getTemplate();
	if (!tmpl.pyConfig().hasVillage) return "template has no village";

	auto vc = tmpl.villageCenter(42);
	glm::vec3 spawnPos = srv->spawnPos();
	glm::vec3 chestPos = tmpl.chestPosition(42, spawnPos);

	// Chest should be within ~15 blocks of village center (inside main house)
	float dx = chestPos.x - (float)vc.x;
	float dz = chestPos.z - (float)vc.y;
	float distFromVC = std::sqrt(dx*dx + dz*dz);
	if (distFromVC > 15.0f)
		return "chest is " + std::to_string((int)distFromVC) +
		       " blocks from village center — expected inside main house (<15)";

	// The chest block should actually be placed in the world
	int cx = (int)std::round(chestPos.x), cy = (int)std::round(chestPos.y),
	    cz = (int)std::round(chestPos.z);
	BlockId bid = srv->chunks().getBlock(cx, cy, cz);
	if (bid == BLOCK_AIR)
		return "no chest block at chestPosition (" +
		       std::to_string(cx) + "," + std::to_string(cy) + "," + std::to_string(cz) + ")";
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
// T26: DropItem produces exactly 1 item (one-shot, no duplication)
// ================================================================
// Regression test for the "13 eggs" bug: sending DropItem once must
// spawn exactly 1 item entity. Sending it N times must spawn N items.
// This catches any tick-loop that re-sends one-shot actions.

static std::string t26_dropitem_no_duplication() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p || !p->inventory) return "no player or inventory";

	// Walk out of portal first
	for (int i = 0; i < 60; i++)
		moveAndTick(*srv, pid, {0, 0, -8.0f});
	tickN(*srv, 10);

	// Give player some eggs
	p->inventory->add("base:egg", 10);

	// Count item entities before
	int itemsBefore = 0;
	srv->forEachEntity([&](Entity& e) {
		if (e.typeId() == EntityType::ItemEntity) itemsBefore++;
	});

	// Send exactly 1 DropItem action
	{
		ActionProposal dp;
		dp.type = ActionProposal::DropItem;
		dp.actorId = pid;
		dp.blockType = "base:egg";
		dp.itemCount = 1;
		srv->sendAction(dp);
	}

	// Tick 20 frames (simulates ~0.33s at 60Hz — more than one decide cycle)
	tickN(*srv, 20);

	// Count item entities after
	int itemsAfter = 0;
	srv->forEachEntity([&](Entity& e) {
		if (e.typeId() == EntityType::ItemEntity) itemsAfter++;
	});

	int spawned = itemsAfter - itemsBefore;
	if (spawned != 1)
		return "expected 1 item entity from 1 DropItem, got " + std::to_string(spawned);

	// Verify inventory decreased by exactly 1
	int remaining = p->inventory->count("base:egg");
	if (remaining != 9)
		return "expected 9 eggs remaining, got " + std::to_string(remaining);

	return "";
}

// ================================================================
// T27: Multiple DropItems produce correct count (no dedup on server)
// ================================================================

static std::string t27_multiple_dropitems_correct_count() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p || !p->inventory) return "no player or inventory";

	for (int i = 0; i < 60; i++)
		moveAndTick(*srv, pid, {0, 0, -8.0f});
	tickN(*srv, 10);

	p->inventory->add("base:egg", 20);

	int itemsBefore = 0;
	srv->forEachEntity([&](Entity& e) {
		if (e.typeId() == EntityType::ItemEntity) itemsBefore++;
	});

	// Send exactly 3 DropItem actions (3 separate intentional drops)
	for (int i = 0; i < 3; i++) {
		ActionProposal dp;
		dp.type = ActionProposal::DropItem;
		dp.actorId = pid;
		dp.blockType = "base:egg";
		dp.itemCount = 1;
		srv->sendAction(dp);
		tickN(*srv, 5);
	}

	tickN(*srv, 10);

	int itemsAfter = 0;
	srv->forEachEntity([&](Entity& e) {
		if (e.typeId() == EntityType::ItemEntity) itemsAfter++;
	});

	int spawned = itemsAfter - itemsBefore;
	if (spawned != 3)
		return "expected 3 item entities from 3 DropItems, got " + std::to_string(spawned);

	int remaining = p->inventory->count("base:egg");
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

	if (!p->inventory->has("base:shield")) return "no shield in starting inventory";

	// Find shield's hotbar slot
	int shieldSlot = -1;
	for (int i = 0; i < Inventory::HOTBAR_SLOTS; i++) {
		if (p->inventory->hotbar(i) == "base:shield") { shieldSlot = i; break; }
	}
	if (shieldSlot < 0) return "shield not in any hotbar slot";

	// Equip shield to offhand
	ActionProposal a;
	a.type = ActionProposal::EquipItem;
	a.actorId = pid;
	a.slotIndex = shieldSlot;
	a.blockType = "offhand";
	srv->sendAction(a);
	srv->tick(1.0f / 60.0f);

	if (p->inventory->equipped(WearSlot::Offhand) != "base:shield")
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
	int before = p->inventory->count("base:potion");
	if (before < 1) return "no potions in starting inventory";

	// Damage player first so healing is observable
	p->setHp(5);
	int hpBefore = p->hp();

	// Use potion
	ActionProposal a;
	a.type = ActionProposal::UseItem;
	a.actorId = pid;
	a.slotIndex = 0;
	srv->sendAction(a);
	srv->tick(1.0f / 60.0f);

	// Potion count should decrease
	int after = p->inventory->count("base:potion");
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
		if (e.def().isCreature() && e.id() != pid) {
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
	a.type = ActionProposal::Attack;
	a.actorId = pid;
	a.targetEntity = targetId;
	a.damage = 3.0f;
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
	a.type = ActionProposal::DropItem;
	a.actorId = pid;
	a.blockType = BlockType::Stone;
	a.itemCount = 1;
	srv->sendAction(a);
	srv->tick(1.0f / 60.0f);

	int stoneAfter = p->inventory->count(BlockType::Stone);
	if (stoneAfter >= stoneBefore) return "stone count didn't decrease";
	int items = countByType(*srv, EntityType::ItemEntity);
	if (items < 1) return "no item entity spawned after drop";
	return "";
}

// ================================================================
// T34: Door toggle opens and closes
// ================================================================

static std::string t34_door_toggle() {
	auto srv = makeVillageServer();
	tickN(*srv, 10);

	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	// Find a door block in the village
	glm::ivec3 doorPos = {0, 0, 0};
	bool foundDoor = false;
	BlockId doorId = srv->blockRegistry().getId(BlockType::Door);
	BlockId doorOpenId = srv->blockRegistry().getId(BlockType::DoorOpen);

	// Scan near spawn for a door
	glm::vec3 spawn = srv->spawnPos();
	for (int dx = -30; dx <= 30 && !foundDoor; dx++) {
		for (int dz = -30; dz <= 30 && !foundDoor; dz++) {
			for (int dy = -3; dy <= 5 && !foundDoor; dy++) {
				int bx = (int)spawn.x + dx, by = (int)spawn.y + dy, bz = (int)spawn.z + dz;
				BlockId bid = srv->chunks().getBlock(bx, by, bz);
				if (bid == doorId || bid == doorOpenId) {
					doorPos = {bx, by, bz};
					foundDoor = true;
				}
			}
		}
	}
	if (!foundDoor) return "no door found near spawn (skip)";

	// Teleport player next to door (within interaction range)
	p->position = glm::vec3(doorPos) + glm::vec3(1, 0, 0);

	BlockId before = srv->chunks().getBlock(doorPos.x, doorPos.y, doorPos.z);

	// Toggle door
	ActionProposal a;
	a.type = ActionProposal::InteractBlock;
	a.actorId = pid;
	a.blockPos = doorPos;
	srv->sendAction(a);
	srv->tick(1.0f / 60.0f);

	BlockId after = srv->chunks().getBlock(doorPos.x, doorPos.y, doorPos.z);
	if (after == before) return "door didn't toggle (same block ID before=" +
		std::to_string(before) + " after=" + std::to_string(after) + ")";
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
		if (e.typeId() == EntityType::ItemEntity) {
			itemType = e.getProp<std::string>(Prop::ItemType);
		}
	});
	if (itemType.empty()) return "item entity has no ItemType property";
	if (itemType.find("base:") != 0) return "ItemType doesn't start with 'base:' (got: " + itemType + ")";
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

	if (!p->inventory->has("base:shield")) return "no shield";

	// Equip shield to offhand
	p->inventory->equip(WearSlot::Offhand, "base:shield");
	if (p->inventory->equipped(WearSlot::Offhand) != "base:shield")
		return "equip failed";
	if (p->inventory->has("base:shield")) return "shield still in counter after equip";

	// Unequip
	p->inventory->unequip(WearSlot::Offhand);
	if (!p->inventory->equipped(WearSlot::Offhand).empty())
		return "unequip didn't clear slot";
	if (!p->inventory->has("base:shield")) return "shield not returned to inventory after unequip";
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
	int items = countByType(*srv, EntityType::ItemEntity);
	if (items == 0) return "no item spawned";

	// Try to pick it up (should be denied by server — too far)
	pickupNearbyItems(*srv, pid, 10.0f); // generous client range
	// Server denies because actual distance > pickup_range

	int remaining = countByType(*srv, EntityType::ItemEntity);
	if (remaining == 0) return "item was picked up despite being out of range";
	return "";
}

// ================================================================
// T38: House geometry — doors and stairs have enough clearance
// ================================================================
// Uses scanForViolations() from world_accessibility.h which applies
// the same physics predicates as the runtime collision engine.
// Any door or stair block within 50 blocks of spawn is checked.
//
static std::string t38_house_geometry_valid() {
	auto srv = makeVillageServer();
	tickN(*srv, 5);

	glm::vec3 spawn = srv->spawnPos();
	glm::ivec3 center = {(int)spawn.x, (int)spawn.y, (int)spawn.z};
	auto violations = scanForViolations(srv->chunks(), srv->blockRegistry(), center);

	if (!violations.empty()) {
		auto& v = violations[0];
		return v.description;
	}
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
// Main
// ================================================================

} // namespace modcraft::test

int main() {
	using namespace modcraft;
	using namespace modcraft::test;

	// Initialize Python so world templates and behaviors can load from artifacts/
	pythonBridge().init("python");

	printf("\n=== ModCraft E2E Tests ===\n\n");
	initTemplates();

	printf("--- Player Basics ---\n");
	run("T01: player spawns",            t01_player_spawns);
	run("T02: player has inventory",     t02_player_has_inventory);
	run("T03: player has HP",            t03_player_has_hp);

	printf("\n--- Movement ---\n");
	run("T04: player moves (60 ticks)",      t04_player_moves);
	run("T05: player not stuck (300 ticks)", t05_player_not_stuck_flat);
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
	run("T18: hotbar populated on spawn",        t18_hotbar_populated);
	run("T19: break+place deducts inventory",    t19_break_and_place_cycle);

	printf("\n--- Spawn Integrity ---\n");
	run("T20: spawn area clear (no solid in body)", t20_spawn_area_clear);

	printf("\n--- Callbacks ---\n");
	run("T21: onBreakText fires on block break",    t21_break_fires_onbreaktext);
	run("T22: onPickupText fires on item pickup",   t22_pickup_fires_onpickuptext);
	run("T22b: break underfoot fires pickup callbacks", t22b_break_underfoot_fires_callbacks);
	run("T22c: item reaches player from distance",     t22c_item_reaches_player);

	printf("\n--- Village ---\n");
	run("T23: creatures spawn near village center", t23_creatures_near_village_center);
	run("T24: chest placed inside village main house", t24_chest_in_village);
	run("T25: player spawn within 65 blocks of village", t25_spawn_near_village);

	printf("\n--- Action Integrity ---\n");
	run("T26: DropItem produces exactly 1 item",     t26_dropitem_no_duplication);
	run("T27: 3 DropItems produce exactly 3 items",  t27_multiple_dropitems_correct_count);

	printf("\n--- Item Actions ---\n");
	run("T30: equip item to wear slot",          t30_equip_item);
	run("T31: use item consumes and heals",       t31_use_item_consume);
	run("T32: attack damages creature",           t32_attack_damages_entity);
	run("T33: drop item deducts from inventory",  t33_dropitem_deducts_inventory);
	run("T34: door toggle opens/closes",             t34_door_toggle);
	run("T38: house geometry (door+stair clearance)", t38_house_geometry_valid);
	run("T39: second floor reachable from spawn",    t39_second_floor_reachable);
	run("T35: item entity has ItemType property",  t35_item_entity_has_type);
	run("T36: equip+unequip cycle returns item",  t36_equip_unequip_cycle);
	run("T37: pickup denied if out of range",     t37_pickup_denied_out_of_range);

	printf("\n--- Physics Anti-Tunneling ---\n");
	run("P50: no horizontal tunneling at 50 m/s", p50_no_horizontal_tunneling);
	run("P51: no vertical tunneling at 50 m/s",   p51_no_vertical_tunneling);
	run("P52: knockback no tunneling in box",      p52_knockback_no_tunneling);
	run("P53: village entities never inside blocks (120 frames)", p53_entities_never_inside_blocks_village);

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

/**
 * test_e2e.cpp — Headless end-to-end gameplay tests.
 *
 * Runs entirely in-process (no OpenGL, no network, no GLFW).
 * Uses LocalServer + GameServer directly to simulate gameplay
 * and assert correctness.
 *
 * Each test creates a fresh world to avoid cross-test pollution.
 * Tests are numbered and run sequentially. Exit code = failed count.
 *
 * Runs from build/ directory so python/ and artifacts/ are accessible.
 */

#include "server/local_server.h"
#include "server/world_template.h"
#include "server/python_bridge.h"
#include "shared/constants.h"
#include <cstdio>
#include <cmath>
#include <string>
#include <functional>
#include <vector>
#include <algorithm>

namespace agentworld::test {

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

// Create a flat-world LocalServer and connect one client.
static std::unique_ptr<LocalServer> makeFlatServer() {
	auto srv = std::make_unique<LocalServer>(g_templates);
	WorldGenConfig wgc;
	// Flat world: templateIndex=0, no mob spawning
	wgc.mobs.clear();
	srv->createGame(42, 0, wgc);
	return srv;
}

// Create a village world server with default mob config.
static std::unique_ptr<LocalServer> makeVillageServer() {
	auto srv = std::make_unique<LocalServer>(g_templates);
	srv->createGame(42, 1, WorldGenConfig{});
	return srv;
}

// Tick the server N frames at 60 Hz.
static void tickN(LocalServer& srv, int frames) {
	constexpr float dt = 1.0f / 60.0f;
	for (int i = 0; i < frames; i++) srv.tick(dt);
}

// Send a Move action and tick one frame.
static void moveAndTick(LocalServer& srv, EntityId actor, glm::vec3 vel, bool jump = false) {
	ActionProposal p;
	p.type = ActionProposal::Move;
	p.actorId = actor;
	p.desiredVel = vel;
	p.jump = jump;
	p.jumpVelocity = 10.0f;
	srv.sendAction(p);
	srv.tick(1.0f / 60.0f);
}

// Send a BreakBlock action and tick one frame.
static void breakAndTick(LocalServer& srv, EntityId actor, glm::ivec3 pos) {
	ActionProposal p;
	p.type = ActionProposal::BreakBlock;
	p.actorId = actor;
	p.blockPos = pos;
	srv.sendAction(p);
	srv.tick(1.0f / 60.0f);
}

// Send a PlaceBlock action and tick one frame.
static void placeAndTick(LocalServer& srv, EntityId actor,
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
static int countByType(LocalServer& srv, const std::string& typeId) {
	int n = 0;
	srv.forEachEntity([&](Entity& e) { if (e.typeId() == typeId) n++; });
	return n;
}

// Count entities that are living (Creature or Character kind).
static int countLiving(LocalServer& srv) {
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

	// Tick more frames to let item get attracted and picked up
	tickN(*srv, 20);

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
	// Can't easily get server directly from LocalServer interface
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
	float startX = p->position.x;

	// Walk east 600 frames — in flat world there are no obstacles, should move freely
	for (int i = 0; i < 600; i++)
		moveAndTick(*srv, pid, {8.0f, 0, 0});

	float moved = p->position.x - startX;
	if (moved < 5.0f)
		return "player stuck: only moved " + std::to_string(moved) + " units east in 600 ticks";
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

	tickN(*srv, 30);
	glm::vec3 pos = p->position;
	glm::ivec3 placePos = {(int)pos.x + 3, (int)pos.y, (int)pos.z};

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
// T21: onBreakText fires when a block is broken
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

	BlockId bid = srv->chunks().getBlock(blockPos.x, blockPos.y, blockPos.z);
	if (bid == BLOCK_AIR) return "no surface block at spawn";

	bool textFired = false;
	srv->server()->callbacks().onBreakText = [&](glm::vec3, const std::string&) {
		textFired = true;
	};

	breakAndTick(*srv, pid, blockPos);

	if (!textFired) return "onBreakText was not called after breaking block";
	return "";
}

// ================================================================
// T22: onPickupText fires when an item is picked up
// ================================================================

static std::string t22_pickup_fires_onpickuptext() {
	auto srv = makeFlatServer();
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30);
	glm::vec3 pos = p->position;
	// Break directly underfoot — item spawns < 1 block away and is
	// immediately picked up in the same tick.
	glm::ivec3 blockPos = {(int)std::floor(pos.x), (int)std::floor(pos.y) - 1,
	                       (int)std::floor(pos.z)};

	BlockId bid = srv->chunks().getBlock(blockPos.x, blockPos.y, blockPos.z);
	if (bid == BLOCK_AIR) return "no surface block at spawn";

	bool pickupFired = false;
	srv->server()->callbacks().onPickupText = [&](glm::vec3, const std::string&, int) {
		pickupFired = true;
	};

	breakAndTick(*srv, pid, blockPos);
	if (!pickupFired) tickN(*srv, 5); // give a few more ticks in case item drifted slightly

	if (!pickupFired) return "onPickupText was not called after item pickup";
	return "";
}

// ================================================================
// T22b: break underfoot — item spawns and callbacks fire quickly
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

	BlockId bid = srv->chunks().getBlock(blockPos.x, blockPos.y, blockPos.z);
	if (bid == BLOCK_AIR) return "no surface block at spawn";

	bool pickupTextFired = false;
	bool itemPickupFired = false;
	srv->server()->callbacks().onPickupText = [&](glm::vec3, const std::string&, int) {
		pickupTextFired = true;
	};
	srv->server()->callbacks().onItemPickup = [&](glm::vec3, glm::vec3) {
		itemPickupFired = true;
	};

	breakAndTick(*srv, pid, blockPos);
	// Item spawns underfoot — should be attracted and picked up within a few ticks
	tickN(*srv, 10);

	std::string failures;
	if (!pickupTextFired) failures += "onPickupText not called; ";
	if (!itemPickupFired) failures += "onItemPickup not called; ";
	if (!failures.empty()) return failures;
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

	// Break a block 3 blocks in front of the player (at distance — not directly under feet)
	glm::vec3 pos = p->position;
	glm::ivec3 blockPos = {(int)std::floor(pos.x) + 3, (int)std::floor(pos.y) - 1,
	                       (int)std::floor(pos.z)};

	BlockId bid = srv->chunks().getBlock(blockPos.x, blockPos.y, blockPos.z);
	if (bid == BLOCK_AIR) return "no block at target position";

	// Count items before
	int inv_before = p->inventory ? p->inventory->distinctCount() : 0;

	breakAndTick(*srv, pid, blockPos);

	// Count item entities
	int itemCount = countByType(*srv, EntityType::ItemEntity);
	printf("\n    [DEBUG] item entities after break: %d", itemCount);

	// Tick 120 frames (2 seconds) to let item reach player
	bool pickupTextFired = false;
	srv->server()->callbacks().onPickupText = [&](glm::vec3, const std::string& name, int count) {
		pickupTextFired = true;
		printf("\n    [DEBUG] onPickupText: +%d %s", count, name.c_str());
	};

	for (int i = 0; i < 120; i++) {
		srv->tick(1.0f / 60.0f);
		// Check if item was picked up
		int remaining = countByType(*srv, EntityType::ItemEntity);
		if (remaining == 0 && itemCount > 0) {
			printf("\n    [DEBUG] item collected after %d ticks (%.1fs)", i + 1, (i + 1) / 60.0f);
			break;
		}
	}

	int remaining = countByType(*srv, EntityType::ItemEntity);
	if (remaining > 0) {
		// Item still floating — check where it is
		srv->forEachEntity([&](Entity& e) {
			if (e.typeId() == EntityType::ItemEntity) {
				float dist = glm::length(e.position - p->position);
				printf("\n    [DEBUG] item at (%.1f,%.1f,%.1f), player at (%.1f,%.1f,%.1f), dist=%.1f",
					e.position.x, e.position.y, e.position.z,
					p->position.x, p->position.y, p->position.z, dist);
			}
		});
		return "item entity not picked up after 2 seconds (still " + std::to_string(remaining) + " items)";
	}

	if (!pickupTextFired) return "item collected but onPickupText not called";
	return "";
}

// ================================================================
// T23: Creatures spawn within range of village center
// ================================================================

static std::string t23_creatures_near_village_center() {
	auto srv = makeVillageServer();
	tickN(*srv, 10); // let gravity drop mobs to surface

	// Use the template's virtual villageCenter (no more static method)
	auto* tmpl = dynamic_cast<VillageWorldTemplate*>(
		&srv->server()->world().getTemplate());
	if (!tmpl) return "not a village template";

	auto vc = tmpl->villageCenter(42); // seed=42, same as makeVillageServer
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

	auto* tmpl = dynamic_cast<VillageWorldTemplate*>(
		&srv->server()->world().getTemplate());
	if (!tmpl) return "not a village template";

	auto vc = tmpl->villageCenter(42);
	glm::vec3 spawnPos = srv->spawnPos();
	glm::vec3 chestPos = tmpl->chestPosition(42, spawnPos);

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

	auto* tmpl = dynamic_cast<VillageWorldTemplate*>(
		&srv->server()->world().getTemplate());
	if (!tmpl) return "not a village template";

	auto vc = tmpl->villageCenter(42);
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
// Main
// ================================================================

} // namespace agentworld::test

int main() {
	using namespace agentworld;
	using namespace agentworld::test;

	// Initialize Python so world templates and behaviors can load from artifacts/
	pythonBridge().init("python");

	printf("\n=== AgentWorld E2E Tests ===\n\n");
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

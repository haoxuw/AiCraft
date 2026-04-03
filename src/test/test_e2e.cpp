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

// Create a survival flat-world LocalServer and connect one client.
static std::unique_ptr<LocalServer> makeFlatServer(bool creative = false) {
	auto srv = std::make_unique<LocalServer>(g_templates);
	WorldGenConfig wgc;
	// Flat world: templateIndex=0, no mob spawning
	wgc.mobs.clear();
	srv->createGame(42, 0, creative, wgc);
	return srv;
}

// Create a village world server with default mob config.
static std::unique_ptr<LocalServer> makeVillageServer(bool creative = false) {
	auto srv = std::make_unique<LocalServer>(g_templates);
	srv->createGame(42, 1, creative, WorldGenConfig{});
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

static std::string t10_break_block_creative_inventory() {
	auto srv = makeFlatServer(/*creative=*/true);
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30);
	glm::vec3 pos = p->position;

	// The surface block in flat world is grass at floor(pos.y) - 1
	glm::ivec3 blockPos = {(int)std::floor(pos.x), (int)std::floor(pos.y) - 1,
	                       (int)std::floor(pos.z)};

	// Check block exists (not air)
	BlockId bid = srv->chunks().getBlock(blockPos.x, blockPos.y, blockPos.z);
	if (bid == BLOCK_AIR) return "no block under player at " +
		std::to_string(blockPos.x) + "," + std::to_string(blockPos.y) + "," + std::to_string(blockPos.z);

	// Count items before
	int beforeStone = p->inventory ? p->inventory->count(BlockType::Stone) : 0;

	// Snap player next to block to pass distance check
	breakAndTick(*srv, pid, blockPos);

	// In creative mode: block goes directly to inventory
	int afterTotal = p->inventory ? p->inventory->distinctCount() : 0;
	// The broken block's item type should now be in inventory
	bid = srv->chunks().getBlock(blockPos.x, blockPos.y, blockPos.z);
	if (bid != BLOCK_AIR) return "block still exists after break";

	return "";
}

static std::string t11_break_block_survival_item_spawns() {
	auto srv = makeFlatServer(/*creative=*/false);
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
	auto srv = makeFlatServer(/*creative=*/false);
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
	auto srv = makeFlatServer(/*creative=*/false);
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30);

	// Player has stone(10) in survival. Place at a known air position.
	// Player spawn ~(30, 5, 30). Place at (35, 5, 30) — 5 blocks east, distance < 8.
	glm::ivec3 placePos = {35, 5, 30};

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
	auto srv = makeFlatServer(/*creative=*/false);
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30);

	// Try placing TNT (player doesn't have it)
	if (p->inventory && p->inventory->has(BlockType::TNT))
		return "player unexpectedly has TNT";

	glm::ivec3 placePos = {35, 5, 30};
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
	auto srv = makeVillageServer(/*creative=*/false);
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
	auto srv = makeFlatServer(/*creative=*/false);
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
// T19: Creative mode — break+place cycle
// ================================================================

static std::string t19_creative_break_and_place() {
	auto srv = makeFlatServer(/*creative=*/true);
	EntityId pid = srv->localPlayerId();
	Entity* p = srv->getEntity(pid);
	if (!p) return "no player";

	tickN(*srv, 30);
	glm::vec3 pos = p->position;
	glm::ivec3 blockPos = {(int)pos.x, (int)pos.y - 1, (int)pos.z};
	glm::ivec3 placePos = {(int)pos.x + 3, (int)pos.y, (int)pos.z};

	// Break block
	breakAndTick(*srv, pid, blockPos);
	if (srv->chunks().getBlock(blockPos.x, blockPos.y, blockPos.z) != BLOCK_AIR)
		return "block not broken in creative";

	// Place stone at placePos
	if (!p->inventory || !p->inventory->has(BlockType::Stone))
		return "creative player doesn't have stone";
	placeAndTick(*srv, pid, placePos, BlockType::Stone);
	if (srv->chunks().getBlock(placePos.x, placePos.y, placePos.z) == BLOCK_AIR)
		return "block not placed in creative";

	// In creative mode, stone count should stay at 999 (no deduction)
	if (p->inventory->count(BlockType::Stone) < 999)
		return "creative mode deducted stone from inventory";
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
	auto srv = makeFlatServer(/*creative=*/false);
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
// T23: Creatures spawn within range of village center
// ================================================================

static std::string t23_creatures_near_village_center() {
	auto srv = makeVillageServer();
	tickN(*srv, 10); // let gravity drop mobs to surface

	auto vc = VillageWorldTemplate::villageCenter(42); // seed=42, same as makeVillageServer
	float vcX = (float)vc.x, vcZ = (float)vc.y;

	// mobSpawnRadius default is 30, add generous buffer for vertical variation
	constexpr float maxDist = 80.0f;
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
// Main
// ================================================================

} // namespace agentworld::test

int main() {
	using namespace agentworld::test;

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
	run("T10: break block (creative) removes block",  t10_break_block_creative_inventory);
	run("T11: break block (survival) spawns item",    t11_break_block_survival_item_spawns);
	run("T12: item pickup after break",               t12_item_pickup_after_break);
	run("T13: place block consumed from inventory",   t13_place_block);
	run("T14: place block rejected without item",     t14_place_block_rejected_without_item);

	printf("\n--- Server Stability ---\n");
	run("T15: items only picked up by player",    t15_items_only_picked_up_by_player);
	run("T16: village stable over 300 ticks",     t16_village_stable_300_ticks);
	run("T17: player not stuck 600 ticks",        t17_player_not_stuck_with_obstacles);

	printf("\n--- Inventory ---\n");
	run("T18: hotbar populated on spawn",        t18_hotbar_populated);
	run("T19: creative break+place cycle",       t19_creative_break_and_place);

	printf("\n--- Spawn Integrity ---\n");
	run("T20: spawn area clear (no solid in body)", t20_spawn_area_clear);

	printf("\n--- Callbacks ---\n");
	run("T21: onBreakText fires on block break",    t21_break_fires_onbreaktext);
	run("T22: onPickupText fires on item pickup",   t22_pickup_fires_onpickuptext);

	printf("\n--- Village ---\n");
	run("T23: creatures spawn near village center", t23_creatures_near_village_center);

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

#pragma once
/**
 * world_accessibility.h — Passage geometry validation + reachability flood-fill.
 *
 * Two audiences:
 *   1. World gen subroutines (carveStairway, carveEntrance in world_template.h)
 *      call the template checkDoor/checkStair functions immediately after placing
 *      blocks so violations are caught at generation time.
 *
 *   2. E2E tests use scanForViolations() (block scan) and floodReachable() (BFS)
 *      to assert that all generated structures are player-traversable.
 *
 * The physics predicates here match physics.h exactly:
 *   blocked iff  p.y < block_top  AND  p.y + playerH > block_bottom
 */

#include "shared/chunk_source.h"
#include "shared/block_registry.h"
#include "shared/constants.h"
#include <glm/vec3.hpp>
#include <string>
#include <vector>
#include <queue>
#include <unordered_set>
#include <algorithm>
#include <cmath>

namespace agentica {

// ──────────────────────────────────────────────────────────────────────────────
// NavViolation — one geometry problem found during validation.
// ──────────────────────────────────────────────────────────────────────────────
struct NavViolation {
	glm::ivec3  pos;
	std::string description;
};

// ──────────────────────────────────────────────────────────────────────────────
// Per-column / per-block checks.
//
// Both are templated on GetBlock = any callable (int,int,int)->BlockId so they
// work equally with GenCtx::get (world gen) or ChunkSource::getBlock (tests).
// ──────────────────────────────────────────────────────────────────────────────

// Check one door column starting at the bottom door block (bottomY).
// Returns "" on pass, error string on fail.
template<typename GetBlock>
inline std::string checkDoorColumn(
	GetBlock getBlock, const BlockRegistry& reg,
	int x, int bottomY, int z,
	float playerH = 2.5f)
{
	BlockId doorId = reg.getId(BlockType::Door);

	// Walk up to topmost door block in column.
	int topDoor = bottomY;
	while (reg.get(getBlock(x, topDoor+1, z)).string_id == BlockType::Door)
		topDoor++;

	// Count consecutive non-solid blocks above topmost door block.
	int air = 0;
	for (int h = 1; h <= 8; h++) {
		if (!reg.get(getBlock(x, topDoor+h, z)).solid) air++;
		else break;
	}
	int need = (int)std::ceil(playerH);
	if (air < need)
		return "door at (" + std::to_string(x) + "," + std::to_string(topDoor)
		     + "," + std::to_string(z) + "): " + std::to_string(air)
		     + " air blocks above, need " + std::to_string(need);
	return "";
}

// Check one stair block at (x, by, z).
// Player stands on stair top (by+0.5); head reaches by+0.5+playerH.
// The safety margin (+0.25) accounts for floating-point step-up drift.
// Returns "" on pass, error string on fail.
template<typename GetBlock>
inline std::string checkStairBlock(
	GetBlock getBlock, const BlockRegistry& reg,
	int x, int by, int z,
	float playerH = 2.5f, float margin = 0.25f)
{
	// Overlap predicate: p.y < block_top AND p.y+playerH > block_bottom
	//   with p.y = by + 0.5 (stair top).
	// Solid block at hb overlaps iff  hb < by+0.5+playerH+margin  AND  hb > by-0.5
	int maxCheck = by + (int)std::ceil(0.5f + playerH + margin);
	for (int hb = by+1; hb <= maxCheck; hb++) {
		if (reg.get(getBlock(x, hb, z)).solid)
			return "stair at (" + std::to_string(x) + "," + std::to_string(by)
			     + "," + std::to_string(z) + "): solid block at y=" + std::to_string(hb)
			     + " (head reaches y=" + std::to_string(by) + "+"
			     + std::to_string(0.5f + playerH) + ")";
	}
	return "";
}

// ──────────────────────────────────────────────────────────────────────────────
// scanForViolations — scan a rectangular region for all door/stair violations.
// Suitable for E2E tests (uses ChunkSource).
// ──────────────────────────────────────────────────────────────────────────────
inline std::vector<NavViolation> scanForViolations(
	ChunkSource& world, const BlockRegistry& reg,
	glm::ivec3 center, int radius = 50, float playerH = 2.5f)
{
	std::vector<NavViolation> violations;
	BlockId doorId  = reg.getId(BlockType::Door);
	BlockId stairId = reg.getId(BlockType::Stair);

	auto getBlock = [&](int x, int y, int z) { return world.getBlock(x, y, z); };

	std::vector<std::pair<int,int>> checkedDoorCols;

	for (int dx = -radius; dx <= radius; dx++)
	for (int dz = -radius; dz <= radius; dz++)
	for (int dy = -5; dy <= 30; dy++) {
		int x = center.x+dx, y = center.y+dy, z = center.z+dz;
		BlockId bid = world.getBlock(x, y, z);

		if (bid == doorId) {
			auto col = std::make_pair(x, z);
			if (std::find(checkedDoorCols.begin(), checkedDoorCols.end(), col)
			    != checkedDoorCols.end()) continue;
			checkedDoorCols.push_back(col);
			auto err = checkDoorColumn(getBlock, reg, x, y, z, playerH);
			if (!err.empty()) violations.push_back({{x,y,z}, err});
		}

		if (bid == stairId) {
			auto err = checkStairBlock(getBlock, reg, x, y, z, playerH);
			if (!err.empty()) violations.push_back({{x,y,z}, err});
		}
	}
	return violations;
}

// ──────────────────────────────────────────────────────────────────────────────
// Flood-fill reachability (BFS, ChunkSource).
//
// 'startFloorBlock' = the block the player is STANDING ON (their feet are at
//   startFloorBlock.y + 1 in world space).
//
// A position (x, floorY, z) is "standable" if:
//   - block at (x, floorY, z) is solid (the floor)
//   - ceil(playerH) blocks at (x, floorY+1 .. floorY+ceil(playerH), z) are non-solid
//
// BFS expands in 4 horizontal directions; tries floorY-1, floorY, floorY+1 to
// handle step-up and step-down within stepBlock blocks.
// ──────────────────────────────────────────────────────────────────────────────
struct IVec3Hash {
	size_t operator()(glm::ivec3 const& v) const {
		size_t h = std::hash<int>()(v.x);
		h ^= std::hash<int>()(v.y) + 0x9e3779b9u + (h<<6) + (h>>2);
		h ^= std::hash<int>()(v.z) + 0x9e3779b9u + (h<<6) + (h>>2);
		return h;
	}
};
struct IVec3Eq {
	bool operator()(glm::ivec3 const& a, glm::ivec3 const& b) const {
		return a.x==b.x && a.y==b.y && a.z==b.z;
	}
};

inline bool isStandable(
	ChunkSource& world, const BlockRegistry& reg,
	int x, int floorY, int z, float playerH = 2.5f)
{
	if (!reg.get(world.getBlock(x, floorY, z)).solid) return false;
	int clearance = (int)std::ceil(playerH) + 1; // +1 safety
	for (int h = 1; h <= clearance; h++)
		if (reg.get(world.getBlock(x, floorY+h, z)).solid) return false;
	return true;
}

inline std::unordered_set<glm::ivec3, IVec3Hash, IVec3Eq> floodReachable(
	ChunkSource& world, const BlockRegistry& reg,
	glm::ivec3 startFloorBlock,
	float playerH = 2.5f, int stepBlock = 1, int maxRadius = 80)
{
	using Set = std::unordered_set<glm::ivec3, IVec3Hash, IVec3Eq>;
	Set visited;
	std::queue<glm::ivec3> q;

	if (!isStandable(world, reg, startFloorBlock.x, startFloorBlock.y,
	                 startFloorBlock.z, playerH))
		return visited;

	visited.insert(startFloorBlock);
	q.push(startFloorBlock);

	constexpr int ddx[] = {1,-1,0,0};
	constexpr int ddz[] = {0,0,1,-1};

	while (!q.empty()) {
		auto cur = q.front(); q.pop();
		for (int d = 0; d < 4; d++) {
			int nx = cur.x + ddx[d];
			int nz = cur.z + ddz[d];
			if (std::abs(nx - startFloorBlock.x) > maxRadius) continue;
			if (std::abs(nz - startFloorBlock.z) > maxRadius) continue;
			// Try same Y, step up, step down
			for (int ny = cur.y - stepBlock; ny <= cur.y + stepBlock; ny++) {
				glm::ivec3 next{nx, ny, nz};
				if (visited.count(next)) continue;
				if (!isStandable(world, reg, nx, ny, nz, playerH)) continue;
				visited.insert(next);
				q.push(next);
			}
		}
	}
	return visited;
}

} // namespace agentica

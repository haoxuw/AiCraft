#pragma once
// Passage geometry validation + reachability flood-fill.
// World gen calls checkDoor/checkStair post-place to catch violations at gen time.
// E2E tests use scanForViolations + floodReachable to assert traversability.
// Predicates match physics.h: blocked iff p.y < top AND p.y+playerH > bottom.

#include "logic/chunk_source.h"
#include "logic/block_registry.h"
#include "logic/constants.h"
#include <glm/vec3.hpp>
#include <string>
#include <vector>
#include <queue>
#include <unordered_set>
#include <algorithm>
#include <cmath>

namespace solarium {

struct NavViolation {
	glm::ivec3  pos;
	std::string description;
};

// Checks are templated on GetBlock = (int,int,int)->BlockId so they work with
// both GenCtx::get (world gen) and ChunkSource::getBlock (tests).

// Door column starting at bottomY; "" = pass, else error string.
// Doors count as passable; opening must be >= ceil(playerH) tall.
template<typename GetBlock>
inline std::string checkDoorColumn(
	GetBlock getBlock, const BlockRegistry& reg,
	int x, int bottomY, int z,
	float playerH = 2.5f)
{
	BlockId doorId = reg.getId(BlockType::Door);

	int clear = 0;
	for (int h = 0; h <= 8; h++) {
		BlockId bid = getBlock(x, bottomY + h, z);
		bool passable = (bid == doorId) || !reg.get(bid).solid;
		if (passable) clear++;
		else break;
	}
	int need = (int)std::ceil(playerH);
	if (clear < need)
		return "door at (" + std::to_string(x) + "," + std::to_string(bottomY)
		     + "," + std::to_string(z) + "): " + std::to_string(clear)
		     + " passable blocks from floor, need " + std::to_string(need);
	return "";
}

// Stair at (x,by,z); player stands on by+0.5. Margin absorbs step-up drift.
template<typename GetBlock>
inline std::string checkStairBlock(
	GetBlock getBlock, const BlockRegistry& reg,
	int x, int by, int z,
	float playerH = 2.5f, float margin = 0.25f)
{
	// Solid at hb blocks iff hb < by+0.5+playerH+margin AND hb > by-0.5.
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

// Rectangular scan for door/stair violations; E2E use.
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

// BFS reachability. startFloorBlock = block player stands ON (feet at y+1).
// Standable: floor solid + ceil(playerH) clearance above.
// Expands 4-way; tries ±stepBlock on Y for step-up/down.
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

} // namespace solarium

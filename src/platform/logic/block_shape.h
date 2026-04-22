#pragma once

// Shape-aware geometry for block types — the source of truth for every
// geometric question about a block.
//
// A BlockShape describes a block as a list of axis-aligned SubBoxes in
// world space. Rendering (crack overlay, ghost preview, chunk mesh) and
// physics (collision) both derive from this list, so adding a new block
// geometry means one new BlockShape subclass instead of editing every
// system's per-mesh_type switch.
//
// Usage:
//   const BlockShape& s = getBlockShape(bdef.mesh_type);
//   std::vector<SubBox> out;
//   s.emitSubBoxes(blockCorner, param2, neighbors, out);
//   for (const auto& b : out) drawCrackOverlay(scene, &b.min.x, &b.max.x, …);
//
// Each MeshType maps to exactly one BlockShape singleton. To add a new
// block geometry: add a new MeshType value, a new BlockShape subclass,
// and one case in the getBlockShape() factory.

#include "logic/block_registry.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace civcraft {

// One axis-aligned sub-volume of a block, in world coordinates. A full
// cube is a single SubBox spanning (bc, bc+1). A stair is two SubBoxes.
// Plants and glass emit zero. Non-axis-aligned shapes (slopes) still
// emit their AABB here — the actual sloped mesh is generated separately;
// this list drives collision, crack overlay, and ghost preview, all of
// which work fine on the shape's AABB approximation.
struct SubBox {
	glm::vec3 min;
	glm::vec3 max;
};

// Which of the 6 cardinal neighbors are "solid full cubes". Used by
// shapes that connect to neighbors (fence, wall, glass-pane). Face
// indices match the terrain mesher: 0=-X, 1=+X, 2=-Y, 3=+Y, 4=-Z, 5=+Z.
struct NeighborMask {
	uint8_t bits = 0;
	constexpr bool has(int face) const { return (bits >> face) & 1u; }
	constexpr void set(int face)       { bits |= (uint8_t)(1u << face); }
};

class BlockShape {
public:
	virtual ~BlockShape() = default;

	// How many distinct rotation states this shape supports. 1 = not
	// rotatable. Used by the placement UI so Tab / MMB-scroll cycles
	// through exactly the shape's valid states. Auto-rotating shapes
	// (fence, door) that derive param2 from neighbors also return 1 —
	// the player doesn't pick their rotation.
	virtual int rotationCount() const { return 1; }

	// Emit this shape's sub-boxes in world space.
	//   blockCorner — integer world corner of the block.
	//   param2      — orientation byte from the chunk (FourDir etc.).
	//                 Ignored by shapes that aren't rotatable.
	//   neighbors   — which cardinal neighbors are solid full cubes, for
	//                 shapes like fence/wall that grow arms toward them.
	//                 Shapes that don't care can pass NeighborMask{}.
	//   out         — appended to; caller clears before calling.
	virtual void emitSubBoxes(glm::ivec3    blockCorner,
	                           uint8_t       param2,
	                           NeighborMask  neighbors,
	                           std::vector<SubBox>& out) const = 0;
};

// Factory. Returns a statically-allocated singleton for the given mesh
// type — never null; unknown types fall back to an empty shape.
const BlockShape& getBlockShape(MeshType mt);

}  // namespace civcraft

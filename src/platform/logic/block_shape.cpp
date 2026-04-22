// BlockShape implementations — one concrete subclass per MeshType.
//
// To add a new block geometry:
//   1. Add the new value to `enum class MeshType` in block_registry.h.
//   2. Add a `MyNewShape : public BlockShape` subclass below, overriding
//      `emitSubBoxes` to describe its sub-box layout (and any other
//      virtuals as they appear — rotationCount, visual hooks).
//      Rotatable shapes read `param2`; neighbor-connecting shapes read
//      `neighbors`.
//   3. Register a static singleton and add a `case` to `getBlockShape`.
// The rest of the engine picks up the new shape via
// `getBlockShape(bdef.mesh_type)` — no other file changes.

#include "logic/block_shape.h"

namespace civcraft {

namespace {

// ── Full unit cube (stone, dirt, wood, glass, etc.) ───────────────────
class CubeShape : public BlockShape {
public:
	void emitSubBoxes(glm::ivec3 bc, uint8_t /*p2*/, NeighborMask /*nm*/,
	                   std::vector<SubBox>& out) const override {
		glm::vec3 b(bc);
		out.push_back({b, b + glm::vec3(1.0f, 1.0f, 1.0f)});
	}
};

// ── Half-height slab. param2 bit 0: 1 = top half, 0 = bottom half.
// Two rotation states so MMB/R flips between them during placement.
class SlabShape : public BlockShape {
public:
	int rotationCount() const override { return 2; }

	void emitSubBoxes(glm::ivec3 bc, uint8_t p2, NeighborMask /*nm*/,
	                   std::vector<SubBox>& out) const override {
		glm::vec3 b(bc);
		bool top = (p2 & 0x1) != 0;
		out.push_back({
			b + glm::vec3(0.0f, top ? 0.5f : 0.0f, 0.0f),
			b + glm::vec3(1.0f, top ? 1.0f : 0.5f, 1.0f)
		});
	}
};

// ── Thin square column along one axis. param2 picks the axis:
// 0=Y (default vertical), 1=X (horizontal east-west), 2=Z (north-south).
class PillarShape : public BlockShape {
public:
	int rotationCount() const override { return 3; }

	void emitSubBoxes(glm::ivec3 bc, uint8_t p2, NeighborMask /*nm*/,
	                   std::vector<SubBox>& out) const override {
		glm::vec3 b(bc);
		const float t = 0.25f;  // how thin the pillar is (0.5 = normal width)
		switch (p2 % 3) {
			case 0: out.push_back({  // Y-axis: cross-section in X/Z
				b + glm::vec3(t, 0.0f, t),
				b + glm::vec3(1.0f - t, 1.0f, 1.0f - t)}); break;
			case 1: out.push_back({  // X-axis: cross-section in Y/Z
				b + glm::vec3(0.0f, t, t),
				b + glm::vec3(1.0f, 1.0f - t, 1.0f - t)}); break;
			case 2: out.push_back({  // Z-axis: cross-section in X/Y
				b + glm::vec3(t, t, 0.0f),
				b + glm::vec3(1.0f - t, 1.0f - t, 1.0f)}); break;
		}
	}
};

// ── Trapdoor: horizontal door-like panel. param2 bits 0-1 = hinge edge
// (0=-Z, 1=+X, 2=+Z, 3=-X); bit 2 = open-position (0=bottom, 1=top).
class TrapdoorShape : public BlockShape {
public:
	int rotationCount() const override { return 8; }

	void emitSubBoxes(glm::ivec3 bc, uint8_t p2, NeighborMask /*nm*/,
	                   std::vector<SubBox>& out) const override {
		glm::vec3 b(bc);
		bool open = (p2 & 0x4) != 0;
		if (!open) {
			// Closed: thin slab at the bottom.
			out.push_back({b, b + glm::vec3(1.0f, 0.1f, 1.0f)});
		} else {
			// Open: thin panel on one vertical face, hinged on the
			// matching edge. Bit 0-1 picks which wall.
			switch (p2 & 0x3) {
				case 0: out.push_back({b, b + glm::vec3(1.0f, 1.0f, 0.1f)}); break;  // -Z wall
				case 1: out.push_back({b + glm::vec3(0.9f, 0, 0),
				                       b + glm::vec3(1.0f, 1.0f, 1.0f)}); break;    // +X wall
				case 2: out.push_back({b + glm::vec3(0, 0, 0.9f),
				                       b + glm::vec3(1.0f, 1.0f, 1.0f)}); break;    // +Z wall
				case 3: out.push_back({b, b + glm::vec3(0.1f, 1.0f, 1.0f)}); break;  // -X wall
			}
		}
	}
};

// ── Torch: thin central post offset toward the supporting surface.
// param2: 0=floor, 1=+X wall, 2=-X wall, 3=+Z wall, 4=-Z wall, 5=ceiling.
class TorchShape : public BlockShape {
public:
	int rotationCount() const override { return 6; }

	void emitSubBoxes(glm::ivec3 bc, uint8_t p2, NeighborMask /*nm*/,
	                   std::vector<SubBox>& out) const override {
		glm::vec3 b(bc);
		const float r = 0.07f;  // half-width
		const float len = 0.6f;
		glm::vec3 mn, mx;
		switch (p2 % 6) {
			case 0:  mn = {0.5f - r, 0.0f, 0.5f - r};
			         mx = {0.5f + r, len,  0.5f + r}; break;  // floor
			case 1:  mn = {0.65f,    0.2f, 0.5f - r};
			         mx = {0.65f + 0.2f, 0.2f + len, 0.5f + r}; break;  // +X wall
			case 2:  mn = {0.15f,    0.2f, 0.5f - r};
			         mx = {0.35f,    0.2f + len, 0.5f + r}; break;  // -X wall
			case 3:  mn = {0.5f - r, 0.2f, 0.65f};
			         mx = {0.5f + r, 0.2f + len, 0.85f}; break;  // +Z wall
			case 4:  mn = {0.5f - r, 0.2f, 0.15f};
			         mx = {0.5f + r, 0.2f + len, 0.35f}; break;  // -Z wall
			default: mn = {0.5f - r, 1.0f - len, 0.5f - r};
			         mx = {0.5f + r, 1.0f,       0.5f + r}; break;  // ceiling
		}
		out.push_back({b + mn, b + mx});
	}
};

// ── L-shaped stair: bottom slab + upper quarter in the rise direction ─
// param2 FourDir (bits 0-1): 0=+Z, 1=+X, 2=-Z, 3=-X.
class StairShape : public BlockShape {
public:
	int rotationCount() const override { return 4; }

	void emitSubBoxes(glm::ivec3 bc, uint8_t p2, NeighborMask /*nm*/,
	                   std::vector<SubBox>& out) const override {
		glm::vec3 b(bc);
		// Bottom slab (always 1×0.5×1).
		out.push_back({b, b + glm::vec3(1.0f, 0.5f, 1.0f)});
		// Upper quarter — X/Z extents rotate with param2.
		switch (p2 & 0x3) {
			case 0: out.push_back({b + glm::vec3(0.0f, 0.5f, 0.5f),
			                       b + glm::vec3(1.0f, 1.0f, 1.0f)}); break;  // +Z
			case 1: out.push_back({b + glm::vec3(0.5f, 0.5f, 0.0f),
			                       b + glm::vec3(1.0f, 1.0f, 1.0f)}); break;  // +X
			case 2: out.push_back({b + glm::vec3(0.0f, 0.5f, 0.0f),
			                       b + glm::vec3(1.0f, 1.0f, 0.5f)}); break;  // -Z
			case 3: out.push_back({b + glm::vec3(0.0f, 0.5f, 0.0f),
			                       b + glm::vec3(0.5f, 1.0f, 1.0f)}); break;  // -X
		}
	}
};

// ── Corner stair: L-shape in plan view (bottom slab + two upper
// quarters forming an L). param2 FourDir selects the exposed corner
// direction (0=+X+Z, 1=-X+Z, 2=-X-Z, 3=+X-Z). The L wraps around the
// opposite corner — the "indentation" points toward param2's dir.
class CornerStairShape : public BlockShape {
public:
	int rotationCount() const override { return 4; }

	void emitSubBoxes(glm::ivec3 bc, uint8_t p2, NeighborMask /*nm*/,
	                   std::vector<SubBox>& out) const override {
		glm::vec3 b(bc);
		// Bottom full slab — always present.
		out.push_back({b, b + glm::vec3(1.0f, 0.5f, 1.0f)});
		// Upper level is 3/4 of a slab; the missing quarter points at
		// the exposed-corner dir.
		switch (p2 & 0x3) {
			case 0:  // missing +X+Z corner (quarter at max-X, max-Z gone)
				out.push_back({b + glm::vec3(0.0f, 0.5f, 0.0f),
				               b + glm::vec3(0.5f, 1.0f, 1.0f)});
				out.push_back({b + glm::vec3(0.5f, 0.5f, 0.0f),
				               b + glm::vec3(1.0f, 1.0f, 0.5f)});
				break;
			case 1:  // missing -X+Z
				out.push_back({b + glm::vec3(0.5f, 0.5f, 0.0f),
				               b + glm::vec3(1.0f, 1.0f, 1.0f)});
				out.push_back({b + glm::vec3(0.0f, 0.5f, 0.0f),
				               b + glm::vec3(0.5f, 1.0f, 0.5f)});
				break;
			case 2:  // missing -X-Z
				out.push_back({b + glm::vec3(0.5f, 0.5f, 0.0f),
				               b + glm::vec3(1.0f, 1.0f, 1.0f)});
				out.push_back({b + glm::vec3(0.0f, 0.5f, 0.5f),
				               b + glm::vec3(0.5f, 1.0f, 1.0f)});
				break;
			case 3:  // missing +X-Z
				out.push_back({b + glm::vec3(0.0f, 0.5f, 0.0f),
				               b + glm::vec3(0.5f, 1.0f, 1.0f)});
				out.push_back({b + glm::vec3(0.5f, 0.5f, 0.5f),
				               b + glm::vec3(1.0f, 1.0f, 1.0f)});
				break;
		}
	}
};

// ── Door: thin panel on -Z face (auto-hinged by the server, so the
// rotation is not user-picked; rotationCount stays 1). ────────────────
class DoorShape : public BlockShape {
public:
	void emitSubBoxes(glm::ivec3 bc, uint8_t /*p2*/, NeighborMask /*nm*/,
	                   std::vector<SubBox>& out) const override {
		glm::vec3 b(bc);
		out.push_back({b, b + glm::vec3(1.0f, 1.0f, 0.1f)});
	}
};

// ── No sub-boxes. Used by plant bezier geometry (no meaningful AABB)
// and as the fallback for unknown MeshTypes. ─────────────────────────
class NullShape : public BlockShape {
public:
	void emitSubBoxes(glm::ivec3, uint8_t, NeighborMask,
	                   std::vector<SubBox>&) const override {}
};

static const CubeShape        kCube;
static const SlabShape        kSlab;
static const PillarShape      kPillar;
static const TrapdoorShape    kTrapdoor;
static const TorchShape       kTorch;
static const StairShape       kStair;
static const CornerStairShape kCornerStair;
static const DoorShape        kDoor;
static const NullShape        kNull;

}  // namespace

const BlockShape& getBlockShape(MeshType mt) {
	switch (mt) {
		case MeshType::Cube:        return kCube;
		case MeshType::Slab:        return kSlab;
		case MeshType::Pillar:      return kPillar;
		case MeshType::Trapdoor:    return kTrapdoor;
		case MeshType::Torch:       return kTorch;
		case MeshType::Stair:       return kStair;
		case MeshType::CornerStair: return kCornerStair;
		case MeshType::Door:        return kDoor;
		case MeshType::DoorOpen:    return kDoor;
		case MeshType::Plant:       return kNull;
	}
	return kNull;
}

}  // namespace civcraft

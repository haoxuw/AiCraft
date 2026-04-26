#pragma once

// ECEF → local ENU rotate + bake. Ports planGlobalStrict +
// bakeRotScaleIntoVerticesAfterApply from VoxelEarth's AssimpDracoDecode.java
// (which itself ports rotateUtils.cjs).
//
// Photorealistic 3D Tiles ship each leaf with vertices in mesh space and an
// ECEF translation on the root node. To voxelize them with +Y as up (so
// buildings stand upright in our world), we:
//
//   1. Pick a shared origin C (ECEF). Caller passes the same C for every
//      tile in a region so they all align in the same local frame.
//   2. up_ecef = normalize(C)              ← ellipsoid normal at C
//   3. q       = quatFromUnitVectors(up_ecef, +Y)
//   4. For each vertex v in mesh space:
//        v_world = root_matrix · v
//        v_local = q · (v_world − C)
//   5. Reset root TRS to identity → mesh sits in local ENU meters.

#include "server/voxel_earth/glb_loader.h"

#include <array>

namespace civcraft::voxel_earth {

// Convenience: each tile's own ECEF translation as origin. For multi-tile
// regions, the orchestrator picks ONE origin (e.g. the first tile's) and
// reuses it for every tile so they connect.
std::array<double, 3> origin_from_root(const Glb& glb);

// In-place rotate. After this, glb.meshes[i].positions are in local ENU
// meters around `origin`, +Y is geodetic up, and root TRS is identity.
void rotate_to_local_enu(Glb& glb, const std::array<double, 3>& origin);

}  // namespace civcraft::voxel_earth

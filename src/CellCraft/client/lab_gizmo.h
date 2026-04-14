// CellCraft — transform gizmo for the selected part in the Creature Lab.
// The gizmo is a dashed chalk bounding box with four corner scale handles,
// a rotation handle above the top edge, and a delete X in the top-right.

#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "CellCraft/client/chalk_stroke.h"

namespace civcraft::cellcraft {

enum LabGizmoHandle {
	LAB_GIZMO_NONE     = -1,
	LAB_GIZMO_CORNER_0 = 0, // TL
	LAB_GIZMO_CORNER_1 = 1, // TR
	LAB_GIZMO_CORNER_2 = 2, // BR
	LAB_GIZMO_CORNER_3 = 3, // BL
	LAB_GIZMO_ROTATE   = 4,
	LAB_GIZMO_DELETE   = 5,
};

// Append chalk strokes for a gizmo at pixel-space bbox (x,y,w,h).
void lab_gizmo_append_strokes(float x, float y, float w, float h,
                              std::vector<ChalkStroke>& out);

// Hit-test. Returns one of LAB_GIZMO_* or LAB_GIZMO_NONE.
int lab_gizmo_handle_hit(glm::vec2 p_px, float x, float y, float w, float h);

} // namespace civcraft::cellcraft

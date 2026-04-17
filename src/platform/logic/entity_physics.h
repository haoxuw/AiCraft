#pragma once

#include "logic/entity.h"
#include "logic/physics.h"

namespace civcraft {

// Rule 6: single physics entry point — keeps client/server bit-for-bit aligned.
// Callers: entity_manager (server), gameplay_movement (local player), network_server (remote prediction).
inline MoveResult stepEntityPhysics(Entity& e, glm::vec3 desiredVel,
                                     const BlockSolidFn& isSolid, float dt,
                                     bool fly) {
	const auto& def = e.def();
	MoveParams mp = makeMoveParams(def.collision_box_min, def.collision_box_max,
	                               def.gravity_scale, def.isLiving(), fly);
	MoveResult r = moveAndCollide(isSolid, e.position, desiredVel, dt, mp, e.onGround);
	e.position = r.position;
	e.velocity = r.velocity;
	e.onGround = r.onGround;
	return r;
}

// Overload: reads fly-mode from entity "fly_mode" property.
inline MoveResult stepEntityPhysics(Entity& e, glm::vec3 desiredVel,
                                     const BlockSolidFn& isSolid, float dt) {
	return stepEntityPhysics(e, desiredVel, isSolid, dt,
	                         e.getProp<bool>("fly_mode", false));
}

} // namespace civcraft

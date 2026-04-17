#pragma once

#include "logic/entity.h"
#include "logic/physics.h"

namespace civcraft {

// Step one tick of physics for an Entity. The single entry point used by:
//   - server/entity_manager.h  (authoritative NPC physics)
//   - client/gameplay_movement.cpp  (local player prediction)
//   - client/network_server.h  (non-local entity prediction between broadcasts)
// Guarantees every caller builds MoveParams identically and writes results
// back in the same way, so client and server physics stay bit-for-bit aligned.
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

// Overload that reads fly-mode from the entity's "fly_mode" property.
inline MoveResult stepEntityPhysics(Entity& e, glm::vec3 desiredVel,
                                     const BlockSolidFn& isSolid, float dt) {
	return stepEntityPhysics(e, desiredVel, isSolid, dt,
	                         e.getProp<bool>("fly_mode", false));
}

} // namespace civcraft

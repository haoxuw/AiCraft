#pragma once

// Pathfinding-debug log sink. Expands to solarium::entityLog(...) when
// SOLARIUM_PATHFINDING_DEBUG is defined at build time (CMake option
// `solarium_pathfinding_debug`); expands to a no-op otherwise, so the
// variadic arguments never evaluate in release builds.
//
// The six logging hooks the design specified (nav start/end, direct-move
// while planning, plan receive, execution start, route replacement,
// repetitive-decide warning) all go through PATHLOG. The runtime cooldown
// in AgentClient::DecidePacer is NOT guarded by this flag — it changes
// dispatch behavior, not just diagnostics, and must ship in every build.

#include "debug/entity_log.h"

#ifdef SOLARIUM_PATHFINDING_DEBUG
#  define PATHLOG(eid, ...) ::solarium::entityLog((eid), __VA_ARGS__)
#  define SOLARIUM_PATHFINDING_DEBUG_ENABLED 1
#else
#  define PATHLOG(eid, ...) ((void)0)
#  define SOLARIUM_PATHFINDING_DEBUG_ENABLED 0
#endif

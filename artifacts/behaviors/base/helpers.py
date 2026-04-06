"""Behavior helper functions.

Import these in your behavior code to simplify common tasks.

Usage:
    from modcraft.behaviors.helpers import nearest_entity, is_threatened
"""

import math
import random


def nearest_entity(world, pos, radius=16.0, category=None, type_id=None):
    """Find the closest entity matching filters.

    Args:
        world: The WorldView provided to decide()
        pos: Center position to search from
        radius: Search radius in blocks
        category: Filter by category ("player", "animal", etc.)
        type_id: Filter by exact type ("base:pig", etc.)

    Returns:
        The closest matching entity, or None.
    """
    entities = world.get_entities_in_radius(pos, radius, category=category)
    if type_id:
        entities = [e for e in entities if e.type_id == type_id]
    if not entities:
        return None
    return min(entities, key=lambda e: e.distance)


def is_threatened(world, pos, threat_radius=5.0):
    """Check if any player is within threat_radius.

    Returns:
        The closest threatening player, or None.
    """
    return nearest_entity(world, pos, threat_radius, category="player")


def random_wander_target(pos, radius=8.0):
    """Pick a random position near pos for wandering.

    Returns:
        A Vec3 position offset from pos by up to radius blocks.
    """
    angle = random.uniform(0, 2 * math.pi)
    dist = random.uniform(2.0, radius)
    from modcraft.api.types import Vec3
    return Vec3(
        pos.x + math.cos(angle) * dist,
        pos.y,
        pos.z + math.sin(angle) * dist,
    )


def find_block_nearby(world, pos, block_type, radius=8):
    """Search for a specific block type near a position.

    Args:
        world: The WorldView
        pos: Center position
        block_type: Block type ID (e.g., "base:grass")
        radius: Search radius in blocks

    Returns:
        BlockPos of the first match, or None.
    """
    blocks = world.get_blocks_in_radius(pos, radius)
    for bp, bid in blocks:
        if bid == block_type:
            return bp
    return None

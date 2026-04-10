"""actions.py — Python wrappers for common action shorthands.

The server accepts exactly four action types (Move, Relocate, Convert, Interact).
These helpers make the most frequent patterns more readable.  They are imported
into every behavior namespace by loadBehavior via ``from actions import *``.

Read queries
------------
``get_block(x, y, z)`` is NOT an action — it is a read-only query exposed by the
C++ bridge that probes the agent's local chunk cache.  It does not send anything
to the server and cannot change world state.

Write actions (the four primitives)
------------------------------------
``Move(x, y, z, speed=2.0)``   — server type 0 (TYPE_MOVE)
``Relocate(...)``               — server type 1 (TYPE_RELOCATE)
``Convert(...)``                — server type 2 (TYPE_CONVERT)
``Interact(x, y, z)``           — server type 3 (TYPE_INTERACT)

Container references (used as convert_from / convert_into / relocate_from / relocate_to)
------------------------------------------------------------------------------------------
``Self()``              — actor's own inventory (default)
``Ground()``            — world ground (drop or spawn item entity)
``Entity(entity_id)``   — another entity's inventory
``Block(x, y, z)``      — world block at position (mine source or placement target)

Convenience wrappers (defined here, not in C++)
------------------------------------------------
All of the below are just Relocate or Convert with common arguments preset.
They exist only in Python — the C++ bridge does not know about them.
"""

from modcraft_engine import Relocate, Convert, Self, Ground, Entity, Block


def BreakBlock(x, y, z):
    """Mine the block at (x, y, z) and drop the result on the ground."""
    return Convert(from_item="", convert_from=Block(x, y, z), convert_into=Ground())


def DropItem(item_type, count=1):
    """Drop item_type from inventory at feet."""
    return Relocate(relocate_to=Ground(), item_id=item_type, count=count)


def PickupItem(entity_id):
    """Pick up a dropped item entity."""
    return Relocate(relocate_from=Entity(entity_id))


def StoreItem(entity_id, item_id="", count=0):
    """Deposit items into another entity's inventory (chest, Creatures, etc.).

    If item_id is empty, deposits all items. Otherwise deposits item_id × count.
    """
    if item_id:
        return Relocate(relocate_to=Entity(int(entity_id)), item_id=item_id, count=count or 1)
    return Relocate(relocate_to=Entity(int(entity_id)))


def TakeItem(entity_id, item_id, count=1):
    """Take item_id × count from another entity's inventory."""
    return Relocate(relocate_from=Entity(int(entity_id)), item_id=item_id, count=count)

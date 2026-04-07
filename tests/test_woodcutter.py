"""Unit tests for the woodcutter behavior.

Tests the WoodcutterBehavior decision logic in isolation — no server, no
OpenGL, no TCP.  Imports the behavior directly and mocks the engine types.

Run from the repo root:
    python3 tests/test_woodcutter.py
    python3 -m pytest tests/test_woodcutter.py -q
"""

import sys
import os
import math
import types

# ── Path setup ────────────────────────────────────────────────────────────────
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO_ROOT, "python"))
sys.path.insert(0, os.path.join(REPO_ROOT, "artifacts", "behaviors", "base"))

# ── Mock modcraft_engine ──────────────────────────────────────────────────────

class _Action:
    def __init__(self, name, **kwargs):
        self._name = name
        self._kwargs = kwargs
    def __repr__(self):
        return "%s(%s)" % (self._name, self._kwargs)

def Idle():           return _Action("Idle")
def Wander(**kw):     return _Action("Wander", **kw)
def MoveTo(x, y, z, speed=3.0):
    return _Action("MoveTo", x=x, y=y, z=z, speed=speed)
def BreakBlock(x, y, z):
    return _Action("BreakBlock", x=x, y=y, z=z)
def ConvertObject(from_item, to_item, block_pos=None,
                  convert_from_block=False, direct=False):
    return _Action("ConvertObject", from_item=from_item, to_item=to_item,
                   block_pos=block_pos, convert_from_block=convert_from_block,
                   direct=direct)
def StoreItem(entity_id):
    return _Action("StoreItem", entity_id=entity_id)

_engine_mock = types.ModuleType("modcraft_engine")
for _sym in (Idle, Wander, MoveTo, BreakBlock, ConvertObject, StoreItem):
    setattr(_engine_mock, _sym.__name__, _sym)
sys.modules["modcraft_engine"] = _engine_mock

from local_world import LocalWorld, SelfEntity   # noqa: E402
from woodcutter import WoodcutterBehavior        # noqa: E402

# ── Helpers ───────────────────────────────────────────────────────────────────

_ENTITY_ID = 42

def make_world(blocks=None, nearby=None, dt=0.1, time=0.5) -> LocalWorld:
    """Build a LocalWorld from raw dicts, sorted nearest-first (as C++ does)."""
    sorted_blocks = sorted(blocks or [], key=lambda b: b["distance"])
    return LocalWorld._from_raw({
        "blocks": sorted_blocks,
        "nearby": nearby or [],
        "dt": dt,
        "time": time,
    })

def make_block(type_id, x, y, z, distance=None):
    """Raw block dict (C++ bridge format — 'type' key, not 'type_id')."""
    if distance is None:
        distance = math.sqrt(x*x + z*z)
    return {"type": type_id, "x": x, "y": y, "z": z, "distance": distance}

def make_entity(x=0.0, y=64.0, z=0.0, **props) -> SelfEntity:
    """Build a SelfEntity from keyword args."""
    raw = {"id": _ENTITY_ID, "type_id": "base:villager",
           "x": x, "y": y, "z": z, "yaw": 0.0,
           "walk_speed": 3.0, "on_ground": True,
           "inventory": {}, "hp": 20}
    raw.update(props)
    return SelfEntity._from_raw(raw)

def make_nearby_entity(id, category, x, y, z, distance, type_id="base:chest", hp=20):
    """Raw nearby-entity dict (C++ bridge format)."""
    return {"id": id, "type_id": type_id, "category": category,
            "x": x, "y": y, "z": z, "distance": distance, "hp": hp}

def action_name(action):
    return action._name if isinstance(action, _Action) else type(action).__name__

# ── Test 1: block filtering ───────────────────────────────────────────────────

def test_chunk_has_trunk_blocks():
    """world.get('base:trunk') finds the nearest trunk among mixed block types."""
    blocks = [
        make_block("base:grass", 1, 63, 0, 1.0),
        make_block("base:trunk", 5, 64, 0, 5.0),
        make_block("base:trunk", 10, 64, 0, 10.0),
    ]
    world = make_world(blocks=blocks)
    trunk = world.get("base:trunk")
    assert trunk is not None
    assert trunk.distance == 5.0, "expected nearest trunk at dist=5, got %.1f" % trunk.distance
    print("PASS test_chunk_has_trunk_blocks")

# ── Test 2: searching finds nearest trunk ──────────────────────────────────────

def test_searching_finds_nearest_trunk():
    """When searching, woodcutter issues MoveTo for the nearest trunk."""
    blocks = [
        make_block("base:trunk",  8, 64, 0,  8.0),
        make_block("base:trunk", 20, 64, 0, 20.0),
        make_block("base:trunk", 50, 64, 0, 50.0),
    ]
    entity = make_entity(x=0, z=0)
    world = make_world(blocks=blocks)

    wc = WoodcutterBehavior()
    action, goal = wc.decide(entity, world)

    assert action_name(action) == "MoveTo", \
        "expected MoveTo, got %s (goal=%r)" % (action_name(action), goal)
    assert abs(action._kwargs["x"] - 8.5) < 0.1, \
        "expected target x≈8.5, got %.2f" % action._kwargs["x"]
    print("PASS test_searching_finds_nearest_trunk")

# ── Test 3: walk toward trunk ─────────────────────────────────────────────────

def test_walking_to_block():
    """While walking to trunk, woodcutter keeps issuing MoveTo."""
    target = make_block("base:trunk", 10, 64, 0, 10.0)
    entity = make_entity(x=0, z=0)
    world = make_world(blocks=[target])

    wc = WoodcutterBehavior()
    for i in range(2):
        action, goal = wc.decide(entity, world)
        assert action_name(action) == "MoveTo", \
            "call %d: expected MoveTo, got %s (goal=%r)" % (i+1, action_name(action), goal)
    print("PASS test_walking_to_block")

# ── Test 4: chop when close ───────────────────────────────────────────────────

def test_chopping_converts_trunk():
    """Within 2.5 blocks, issues ConvertObject(base:trunk→base:trunk, direct=True)."""
    trunk = make_block("base:trunk", 1, 64, 0, 1.0)
    entity = make_entity(x=0.0, z=0.0)
    world = make_world(blocks=[trunk])

    wc = WoodcutterBehavior()
    action, _ = wc.decide(entity, world)

    assert action_name(action) == "ConvertObject", \
        "expected ConvertObject at dist=1, got %s" % action_name(action)
    assert action._kwargs["from_item"] == "base:trunk"
    assert action._kwargs["to_item"] == "base:trunk"
    assert action._kwargs["convert_from_block"] is True
    assert action._kwargs["direct"] is True
    assert action._kwargs["block_pos"] == (1, 64, 0)
    print("PASS test_chopping_converts_trunk")

# ── Test 5: inventory count ───────────────────────────────────────────────────

def test_inventory_accumulates():
    """inventory.count() reads accumulated trunk items correctly."""
    for n in range(1, 6):
        entity = make_entity(inventory={"base:trunk": n})
        assert entity.inventory.count("base:trunk") == n
    print("PASS test_inventory_accumulates")

# ── Test 6: deposit when full ─────────────────────────────────────────────────

def test_returns_when_full():
    """When collect_goal reached, woodcutter walks to chest (depositing=True)."""
    collect_goal = 3
    entity = make_entity(
        collect_goal=collect_goal,
        chest_x=50.0, chest_y=64.0, chest_z=0.0,
        home_x=0.0, home_z=0.0,
        inventory={"base:trunk": collect_goal},
    )
    world = make_world()

    wc = WoodcutterBehavior()
    action, goal = wc.decide(entity, world)

    assert wc._depositing is True, "expected _depositing=True"
    assert action_name(action) == "MoveTo", \
        "expected MoveTo toward chest, got %s (goal=%r)" % (action_name(action), goal)
    print("PASS test_returns_when_full")

# ── Test 7: deposit at chest ──────────────────────────────────────────────────

def test_deposits_at_chest():
    """Near the chest with a chest entity present, StoreItem is issued."""
    chest_ent = make_nearby_entity(
        id=99, category="chest", x=2.0, y=64.0, z=0.0, distance=2.0)
    entity = make_entity(
        x=2.0, z=0.0,
        collect_goal=3,
        chest_x=2.0, chest_y=64.0, chest_z=0.0,
        home_x=0.0, home_z=0.0,
        inventory={"base:trunk": 3},
    )
    world = make_world(nearby=[chest_ent])

    wc = WoodcutterBehavior()
    wc._home  = (0.0, 64.0, 0.0)
    wc._chest = (2.0, 64.0, 0.0)
    wc._depositing = True

    action, goal = wc.decide(entity, world)

    assert action_name(action) == "StoreItem", \
        "expected StoreItem near chest, got %s (goal=%r)" % (action_name(action), goal)
    assert action._kwargs["entity_id"] == 99
    print("PASS test_deposits_at_chest")

# ── Test 8: full cycle ────────────────────────────────────────────────────────

def test_full_cycle():
    """Full story: search → chop × 5 → carry to chest → deposit."""
    collect_goal = 5
    chest_pos = (50.0, 64.0, 0.0)
    chest_ent = make_nearby_entity(
        id=77, category="chest",
        x=chest_pos[0], y=chest_pos[1], z=chest_pos[2], distance=0.5)

    wc = WoodcutterBehavior()
    inventory = {}

    # Phase 1: searching → MoveTo trunk
    trunk = make_block("base:trunk", 10, 64, 0, 10.0)
    action, _ = wc.decide(
        make_entity(collect_goal=collect_goal,
                    chest_x=chest_pos[0], chest_y=chest_pos[1], chest_z=chest_pos[2],
                    home_x=0.0, home_z=0.0, inventory={}),
        make_world(blocks=[trunk]))
    assert action_name(action) == "MoveTo", "Phase 1: expected MoveTo"

    # Phase 2: chop 5 trunks (entity standing right next to block)
    close_trunk = make_block("base:trunk", 1, 64, 0, 1.0)
    for chop_n in range(1, collect_goal + 1):
        wc._chop_cooldown = 0.0
        action, _ = wc.decide(
            make_entity(collect_goal=collect_goal,
                        chest_x=chest_pos[0], chest_y=chest_pos[1], chest_z=chest_pos[2],
                        home_x=0.0, home_z=0.0, inventory=dict(inventory)),
            make_world(blocks=[close_trunk]))
        assert action_name(action) == "ConvertObject", \
            "chop %d: expected ConvertObject, got %s" % (chop_n, action_name(action))
        inventory["base:trunk"] = chop_n

    assert inventory.get("base:trunk", 0) == collect_goal

    # Phase 3: full inventory → walk to chest
    action, _ = wc.decide(
        make_entity(collect_goal=collect_goal,
                    chest_x=chest_pos[0], chest_y=chest_pos[1], chest_z=chest_pos[2],
                    home_x=0.0, home_z=0.0, inventory=dict(inventory)),
        make_world(blocks=[]))
    assert wc._depositing is True, "Phase 3: expected _depositing=True"
    assert action_name(action) == "MoveTo", "Phase 3: expected MoveTo to chest"

    # Phase 4: at chest → StoreItem
    wc._chest = chest_pos
    wc._home  = (0.0, 64.0, 0.0)
    action, _ = wc.decide(
        make_entity(x=chest_pos[0], z=chest_pos[2],
                    collect_goal=collect_goal,
                    chest_x=chest_pos[0], chest_y=chest_pos[1], chest_z=chest_pos[2],
                    home_x=0.0, home_z=0.0, inventory=dict(inventory)),
        make_world(nearby=[chest_ent]))
    assert action_name(action) == "StoreItem", \
        "Phase 4: expected StoreItem, got %s" % action_name(action)
    assert action._kwargs["entity_id"] == 77
    print("PASS test_full_cycle")

# ── Test 9: no double-chop of same block ──────────────────────────────────────

def test_no_double_chop_same_block():
    """Chop cooldown prevents firing ConvertObject twice in the same 0.4s window."""
    trunk = make_block("base:trunk", 1, 64, 0, 1.0)
    entity = make_entity(x=0.0, z=0.0)
    world = make_world(blocks=[trunk])

    wc = WoodcutterBehavior()
    wc._chop_cooldown = 0.0
    a1, _ = wc.decide(entity, world)
    assert action_name(a1) == "ConvertObject"

    # Still in cooldown — must NOT fire ConvertObject again
    a2, _ = wc.decide(entity, world)
    assert action_name(a2) != "ConvertObject", \
        "Should not issue back-to-back ConvertObject (SourceBlockGone)"
    print("PASS test_no_double_chop_same_block")

# ── Test 10: only leaves, no trunks ──────────────────────────────────────────

def test_only_leaves_falls_back_to_chopping_leaves():
    """No trunks visible → fall back to chopping nearest leaves."""
    blocks = [make_block("base:leaves", d, 65, 0, float(d)) for d in [5, 8, 12]]
    world = make_world(blocks=blocks)

    wc = WoodcutterBehavior()
    action, goal = wc.decide(make_entity(), world)

    # Should walk toward the nearest leaves block (dist=5, x=5)
    assert action_name(action) == "MoveTo", \
        "expected MoveTo toward leaves, got %s (goal=%r)" % (action_name(action), goal)
    assert abs(action._kwargs["x"] - 5.5) < 0.1, \
        "expected target x≈5.5 (nearest leaf), got %.2f" % action._kwargs["x"]
    assert "leaves" in goal.lower() or "walking" in goal.lower(), \
        "goal should mention leaves or walking, got %r" % goal
    print("PASS test_only_leaves_falls_back_to_chopping_leaves")

def test_no_choppable_blocks_wanders():
    """No trunks or leaves → wander and search."""
    blocks = [make_block("base:stone", 3, 64, 0, 3.0)]
    world = make_world(blocks=blocks)

    wc = WoodcutterBehavior()
    action, _ = wc.decide(make_entity(), world)

    assert action_name(action) in ("Wander", "Idle"), \
        "expected Wander/Idle with no choppable blocks, got %s" % action_name(action)
    print("PASS test_no_choppable_blocks_wanders")

# ── Test 11: ignores base:wood (buildings) ────────────────────────────────────

def test_ignores_base_wood_buildings():
    """world.get('base:trunk') must skip base:wood building blocks."""
    blocks = [
        make_block("base:wood",  3, 64, 0,  3.0),
        make_block("base:wood",  5, 64, 2,  5.4),
        make_block("base:trunk", 20, 64, 0, 20.0),
    ]
    world = make_world(blocks=blocks)

    wc = WoodcutterBehavior()
    action, _ = wc.decide(make_entity(), world)

    assert action_name(action) == "MoveTo", "expected MoveTo toward trunk"
    assert abs(action._kwargs["x"] - 20.5) < 0.1, \
        "expected target x≈20.5, got %.2f" % action._kwargs["x"]
    print("PASS test_ignores_base_wood_buildings")

# ── Test 12: max_dist respected ──────────────────────────────────────────────

def test_nearest_block_respects_max_dist():
    """world.get('base:trunk', max_dist=15) must not return blocks further than 15."""
    blocks = [
        make_block("base:trunk", 10, 64, 0, 10.0),
        make_block("base:trunk", 20, 64, 0, 20.0),
        make_block("base:trunk", 30, 64, 0, 30.0),
    ]
    world = make_world(blocks=blocks)

    b = world.get("base:trunk", max_dist=15.0)
    assert b is not None and b.distance == 10.0

    b2 = world.get("base:trunk", max_dist=5.0)
    assert b2 is None, "no trunk within 5 blocks"
    print("PASS test_nearest_block_respects_max_dist")

# ── Runner ────────────────────────────────────────────────────────────────────

def main():
    tests = [
        test_chunk_has_trunk_blocks,
        test_searching_finds_nearest_trunk,
        test_walking_to_block,
        test_chopping_converts_trunk,
        test_inventory_accumulates,
        test_returns_when_full,
        test_deposits_at_chest,
        test_full_cycle,
        test_no_double_chop_same_block,
        test_only_leaves_falls_back_to_chopping_leaves,
        test_no_choppable_blocks_wanders,
        test_ignores_base_wood_buildings,
        test_nearest_block_respects_max_dist,
    ]
    passed = failed = 0
    for t in tests:
        try:
            t()
            passed += 1
        except Exception as e:
            print("FAIL %s: %s" % (t.__name__, e))
            import traceback; traceback.print_exc()
            failed += 1
    print("\n%d passed, %d failed" % (passed, failed))
    sys.exit(0 if failed == 0 else 1)

if __name__ == "__main__":
    main()

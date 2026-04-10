"""E2E woodcutter behavior test — real server, real agents, real physics.

Launches modcraft-server with the test_behaviors template, waits for the
woodcutter villager to go through its full work cycle:
  1. Find and walk to trees
  2. Chop trunk/leaves blocks
  3. Walk to chest and deposit logs
  4. Return to work

Success = the woodcutter's inventory or goal text shows it completed
at least one deposit cycle within the timeout.
"""

import time
import pytest

from .harness import GameHarness

VILLAGER_TYPE = "base:villager"
TIMEOUT_STARTUP = 30       # seconds to wait for entities to appear
TIMEOUT_WORK_CYCLE = 180   # seconds for woodcutter to complete at least one chop


@pytest.fixture(scope="module")
def game():
    """Start a game server for the entire test module."""
    with GameHarness(template=2, seed=100) as g:
        g.wait_for_entities(min_count=3, timeout=TIMEOUT_STARTUP)
        g.wait_for_type(VILLAGER_TYPE, timeout=TIMEOUT_STARTUP)
        # Let agents start up and begin deciding
        g.observer.poll(duration=3.0)
        yield g


def _find_villager(game):
    """Find the woodcutter villager entity."""
    villagers = game.observer.find_entities_by_type(VILLAGER_TYPE)
    assert len(villagers) > 0, "No villager entity found"
    return villagers[0]


class TestWoodcutterLifecycle:
    """Validate the woodcutter goes through its state machine."""

    def test_villager_spawned(self, game):
        """A villager entity exists in the world."""
        v = _find_villager(game)
        assert v.hp > 0, f"Villager is dead: hp={v.hp}"

    def test_villager_has_goal(self, game):
        """Villager has a non-empty goal text (behavior is running)."""
        ok = game.observer.poll_until(
            lambda: any(
                e.type == VILLAGER_TYPE and e.goal_text
                for e in game.observer.entities.values()
            ),
            timeout=30.0,
        )
        v = _find_villager(game)
        assert ok, f"Villager never got a goal text (goal={v.goal_text!r})"

    def test_villager_starts_working(self, game):
        """Villager goal text eventually mentions work-related activity."""
        work_keywords = [
            "walking to", "searching", "chopping", "carrying",
            "depositing", "stuck",
        ]

        def has_work_goal():
            for e in game.observer.entities.values():
                if e.type != VILLAGER_TYPE:
                    continue
                goal = e.goal_text.lower()
                if any(kw in goal for kw in work_keywords):
                    return True
            return False

        ok = game.observer.poll_until(has_work_goal, timeout=60.0)
        v = _find_villager(game)
        assert ok, (
            f"Villager never showed a work goal after 60s. "
            f"Last goal: {v.goal_text!r}"
        )

    def test_villager_chops_wood(self, game):
        """Villager goal text mentions 'chopping' at some point."""
        seen_chopping = False

        def check():
            nonlocal seen_chopping
            for e in game.observer.entities.values():
                if e.type == VILLAGER_TYPE and "chop" in e.goal_text.lower():
                    seen_chopping = True
            return seen_chopping

        game.observer.poll_until(check, timeout=TIMEOUT_WORK_CYCLE)
        assert seen_chopping, "Villager never showed 'chopping' goal"

    def test_villager_deposits_logs(self, game):
        """Villager goal text mentions 'depositing' — it reached the chest."""
        seen_deposit = False

        def check():
            nonlocal seen_deposit
            for e in game.observer.entities.values():
                if e.type == VILLAGER_TYPE and "deposit" in e.goal_text.lower():
                    seen_deposit = True
            return seen_deposit

        game.observer.poll_until(check, timeout=TIMEOUT_WORK_CYCLE)
        assert seen_deposit, "Villager never deposited logs"

    def test_chest_receives_logs(self, game):
        """After depositing, the chest entity's inventory should contain logs."""
        CHEST_TYPE = "base:chest"

        def chest_has_logs():
            # Find chest entities
            for e in game.observer.entities.values():
                if e.type == CHEST_TYPE:
                    inv = game.observer.get_inventory(e.id)
                    if inv and inv.count("base:logs") > 0:
                        return True
            return False

        ok = game.observer.poll_until(chest_has_logs, timeout=TIMEOUT_WORK_CYCLE)
        assert ok, (
            "Chest never received logs. "
            f"Chest entities: {game.observer.find_entities_by_type(CHEST_TYPE)}, "
            f"Inventories: {list(game.observer.inventories.keys())}"
        )

    def test_villager_moves(self, game):
        """Villager position changes over time (not stuck at spawn)."""
        v = _find_villager(game)
        start_pos = v.position

        def moved():
            for e in game.observer.entities.values():
                if e.type != VILLAGER_TYPE:
                    continue
                dx = e.position[0] - start_pos[0]
                dz = e.position[2] - start_pos[2]
                if (dx * dx + dz * dz) > 4.0:  # moved > 2 blocks
                    return True
            return False

        ok = game.observer.poll_until(moved, timeout=30.0)
        assert ok, "Villager did not move from its starting position"

    def test_villager_stays_near_home(self, game):
        """Villager must not wander far from its bed while working.

        Two layered fixes both contribute to this guarantee:
          1. block_search now picks the *nearest* non-empty chunk and exits
             at the first hit, so even an unanchored search can't walk
             past a closer tree to reach a denser one.
          2. woodcutter passes near=self._home into scan_blocks, anchoring
             every search at the bed even after the villager has wandered
             partway toward the previous tree.

        Together, drift should stay roughly within work_radius + path slop.
        Bound is set well below the buggy worst case (~45 blocks) to act as
        a regression catch.
        """
        v = _find_villager(game)
        home_x = float(v.props.get("home_x", v.position[0]))
        home_z = float(v.props.get("home_z", v.position[2]))
        work_radius = float(v.props.get("work_radius", 40.0))
        # Both fixes together: search anchored at home + nearest-chunk-first.
        # Allow half the work radius + slack for path/standoff.
        max_drift = work_radius * 0.5 + 8.0

        state = {"max_d": 0.0, "worst_pos": None}

        def probe():
            for e in game.observer.entities.values():
                if e.type != VILLAGER_TYPE:
                    continue
                dx = e.position[0] - home_x
                dz = e.position[2] - home_z
                d = (dx * dx + dz * dz) ** 0.5
                if d > state["max_d"]:
                    state["max_d"] = d
                    state["worst_pos"] = e.position
            return False  # poll the full timeout — we want the max, not first-hit

        game.observer.poll_until(probe, timeout=TIMEOUT_WORK_CYCLE)
        assert state["max_d"] <= max_drift, (
            f"Villager strayed {state['max_d']:.1f} blocks from home "
            f"({home_x:.1f},{home_z:.1f}) "
            f"(allowed {max_drift:.1f}, work_radius={work_radius:.0f}). "
            f"Worst position: {state['worst_pos']}"
        )

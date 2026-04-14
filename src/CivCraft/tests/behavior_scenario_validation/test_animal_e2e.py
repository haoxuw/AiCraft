"""E2E animal behavior tests — chickens flee, pigs wander.

Validates that animal entities spawned by the test world are alive,
have active behaviors (non-empty goal text), and exhibit expected
behavioral patterns over time.
"""

import pytest

from .harness import GameHarness

CHICKEN_TYPE = "chicken"
PIG_TYPE = "pig"
TIMEOUT_STARTUP = 30


@pytest.fixture(scope="module")
def game():
    with GameHarness(template=2, seed=200) as g:
        g.wait_for_entities(min_count=3, timeout=TIMEOUT_STARTUP)
        # Let agents boot up
        g.observer.poll(duration=5.0)
        yield g


class TestChickenBehavior:

    def test_chicken_spawned(self, game):
        """At least one chicken exists."""
        chickens = game.observer.find_entities_by_type(CHICKEN_TYPE)
        assert len(chickens) > 0, "No chicken entity found"

    def test_chicken_alive(self, game):
        chickens = game.observer.find_entities_by_type(CHICKEN_TYPE)
        for c in chickens:
            assert c.hp > 0, f"Chicken {c.id} is dead"

    def test_chicken_has_behavior(self, game):
        """Chicken gets a non-empty goal text (peck behavior is running)."""
        ok = game.observer.poll_until(
            lambda: any(
                e.type == CHICKEN_TYPE and e.goal_text
                for e in game.observer.entities.values()
            ),
            timeout=30.0,
        )
        assert ok, "Chicken never received a goal from its behavior"

    def test_chicken_moves(self, game):
        """Chicken position changes over time (not frozen)."""
        chickens = game.observer.find_entities_by_type(CHICKEN_TYPE)
        if not chickens:
            pytest.skip("No chicken to track")
        start_pos = chickens[0].position
        cid = chickens[0].id

        def moved():
            e = game.observer.get_entity(cid)
            if not e:
                return False
            dx = e.position[0] - start_pos[0]
            dz = e.position[2] - start_pos[2]
            return (dx * dx + dz * dz) > 1.0

        ok = game.observer.poll_until(moved, timeout=60.0)
        assert ok, "Chicken did not move"

    def test_chicken_exhibits_behavior_goals(self, game):
        """Chicken shows behavior-specific goals (pecking, wandering, flock, etc)."""
        behavior_keywords = [
            "peck", "wander", "flock", "roost", "bawk", "scatter",
            "searching", "morning", "sleeping",
        ]
        seen_any = False

        def check():
            nonlocal seen_any
            for e in game.observer.entities.values():
                if e.type != CHICKEN_TYPE:
                    continue
                goal = e.goal_text.lower()
                if any(kw in goal for kw in behavior_keywords):
                    seen_any = True
            return seen_any

        game.observer.poll_until(check, timeout=60.0)
        assert seen_any, "Chicken never showed a behavior goal"

    def test_chicken_lays_eggs(self, game):
        """Chicken drops a base:egg item entity when fleeing threats.

        The test_behaviors world gives the chicken scatter_range=30 and
        egg_cooldown=1.0 so encountering any other mob reliably triggers
        egg laying within the timeout.
        """
        def egg_on_ground():
            for e in game.observer.entities.values():
                if e.type != "item_entity":
                    continue
                if e.props.get("item_type") == "egg":
                    return True
            return False

        ok = game.observer.poll_until(egg_on_ground, timeout=60.0)
        if not ok:
            items_seen = [
                (e.id, e.props.get("item_type"))
                for e in game.observer.entities.values()
                if e.type == "item_entity"
            ]
            chickens = game.observer.find_entities_by_type(CHICKEN_TYPE)
            assert ok, (
                f"No egg item entity appeared in 60s. "
                f"Item entities seen: {items_seen}. "
                f"Chicken goals: {[c.goal_text for c in chickens]}"
            )


class TestPigBehavior:

    def test_pig_spawned(self, game):
        pigs = game.observer.find_entities_by_type(PIG_TYPE)
        assert len(pigs) > 0, "No pig entity found"

    def test_pig_alive(self, game):
        pigs = game.observer.find_entities_by_type(PIG_TYPE)
        for p in pigs:
            assert p.hp > 0, f"Pig {p.id} is dead"

    def test_pig_has_behavior(self, game):
        """Pig gets a non-empty goal text (wander behavior is running)."""
        ok = game.observer.poll_until(
            lambda: any(
                e.type == PIG_TYPE and e.goal_text
                for e in game.observer.entities.values()
            ),
            timeout=30.0,
        )
        assert ok, "Pig never received a goal from its behavior"

    def test_pig_moves(self, game):
        """Pig position changes over time."""
        pigs = game.observer.find_entities_by_type(PIG_TYPE)
        if not pigs:
            pytest.skip("No pig to track")
        start_pos = pigs[0].position
        pid = pigs[0].id

        def moved():
            e = game.observer.get_entity(pid)
            if not e:
                return False
            dx = e.position[0] - start_pos[0]
            dz = e.position[2] - start_pos[2]
            return (dx * dx + dz * dz) > 1.0

        ok = game.observer.poll_until(moved, timeout=60.0)
        assert ok, "Pig did not move"

    def test_pig_exhibits_behavior_goals(self, game):
        """Pig shows wander-specific goals."""
        behavior_keywords = [
            "wander", "graze", "grazing", "sleeping", "morning",
            "heading home", "fleeing",
        ]
        seen_any = False

        def check():
            nonlocal seen_any
            for e in game.observer.entities.values():
                if e.type != PIG_TYPE:
                    continue
                goal = e.goal_text.lower()
                if any(kw in goal for kw in behavior_keywords):
                    seen_any = True
            return seen_any

        game.observer.poll_until(check, timeout=60.0)
        assert seen_any, "Pig never showed a behavior goal"

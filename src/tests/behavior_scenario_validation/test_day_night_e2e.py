"""E2E day/night cycle test — validates time-of-day behavior transitions.

The game runs a 20-minute day cycle (configurable). This test runs long
enough to observe at least one dawn/dusk transition and validates that:
  - World time advances
  - Entities change goals at night (sleep) vs day (work/wander)
  - The decision queue's time-of-day active trigger fires correctly
"""

import pytest

from .harness import GameHarness

VILLAGER_TYPE = "villager"
TIMEOUT_STARTUP = 30


@pytest.fixture(scope="module")
def game():
    with GameHarness(template=1, seed=300) as g:
        g.wait_for_entities(min_count=3, timeout=TIMEOUT_STARTUP)
        g.observer.poll(duration=3.0)
        yield g


class TestWorldTime:

    def test_time_advances(self, game):
        """World time increases over a 10-second window."""
        t0 = game.observer.world_time
        game.observer.poll(duration=10.0)
        t1 = game.observer.world_time
        # Time should have advanced (wraps at 1.0)
        assert t1 != t0, f"World time did not advance: {t0} -> {t1}"

    def test_time_received(self, game):
        """Observer receives S_TIME and world_time is in [0, 1)."""
        game.observer.poll(duration=2.0)
        t = game.observer.world_time
        assert 0.0 <= t < 1.0, f"World time out of range: {t}"


class TestDayNightTransitions:

    def test_entities_have_goals_during_day(self, game):
        """During daytime, entities show active goals (not just sleeping)."""
        day_keywords = [
            "walking", "searching", "chopping", "carrying", "depositing",
            "wander", "peck", "graze", "flock",
        ]

        def has_day_goal():
            for e in game.observer.entities.values():
                goal = e.goal_text.lower()
                if any(kw in goal for kw in day_keywords):
                    return True
            return False

        ok = game.observer.poll_until(has_day_goal, timeout=60.0)
        assert ok, "No entity showed a daytime activity goal"

    def test_sleep_goals_appear_at_night(self, game):
        """Eventually, night arrives and at least one entity shows sleep/home goal."""
        sleep_keywords = ["sleep", "zzz", "home", "roost", "heading home"]

        def has_sleep_goal():
            for e in game.observer.entities.values():
                goal = e.goal_text.lower()
                if any(kw in goal for kw in sleep_keywords):
                    return True
            return False

        # This may take a while — need to wait for night to come.
        # With 20-min cycle, night is at most ~10 min away.
        # Use a generous timeout.
        ok = game.observer.poll_until(has_sleep_goal, timeout=600.0)
        assert ok, (
            f"No entity showed a sleep goal after 10 minutes. "
            f"World time: {game.observer.world_time:.3f}"
        )

    def test_goals_change_across_cycle(self, game):
        """Over time, entities show at least 2 distinct goal texts (not stuck)."""
        goal_history = set()

        def has_variety():
            for e in game.observer.entities.values():
                if e.goal_text:
                    goal_history.add(e.goal_text)
            return len(goal_history) >= 3

        ok = game.observer.poll_until(has_variety, timeout=120.0)
        assert ok, (
            f"Only saw {len(goal_history)} distinct goals in 120s: {goal_history}"
        )


class TestDecisionQueueIntegration:
    """Validate that the decision queue is working by observing behavior effects."""

    def test_multiple_entities_active_simultaneously(self, game):
        """Multiple entity types show goals concurrently — queue handles all."""
        game.observer.poll(duration=10.0)
        entities_with_goals = [
            e for e in game.observer.entities.values()
            if e.goal_text and e.id != game.observer.player_id
        ]
        assert len(entities_with_goals) >= 2, (
            f"Expected >= 2 NPCs with goals, got {len(entities_with_goals)}. "
            f"Entities: {list(game.observer.entities.values())}"
        )

    def test_entities_respond_promptly(self, game):
        """Entities get goals within a few seconds of spawning — queue schedules quickly."""
        # By the time we run this test, entities should already have goals.
        # If the queue were broken, entities would stay with empty goals.
        # Exclude non-living entities (items, chests and other structures).
        NON_LIVING = {"item_entity", "chest"}
        game.observer.poll(duration=5.0)
        npcs = [
            e for e in game.observer.entities.values()
            if e.id != game.observer.player_id and e.type not in NON_LIVING
        ]
        goalless = [e for e in npcs if not e.goal_text]
        assert len(goalless) == 0, (
            f"{len(goalless)} NPCs have no goal: "
            + ", ".join(f"{e.type}(id={e.id})" for e in goalless)
        )

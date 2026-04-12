"""rules.py — Rule-list base for behaviors.

A behavior is a list of `(condition, action)` pairs. First rule whose
condition is True runs. If that action returns None (declining — e.g.
cooldown not ready), fall through to the next rule.

Usage:
    from rules import RulesBehavior
    from conditions_lib import Threatened, IsEveningOrNight, Always
    from actions_lib    import Flee, GoHome, Wander

    class PigBehavior(RulesBehavior):
        def __init__(self):
            super().__init__()
            self.rules = [
                (Threatened(range=5),    Flee()),
                (IsEveningOrNight(),     GoHome()),
                (Always(),               Wander(radius=12)),
            ]

Conditions combine with `&`, `|`, `~`.
"""

from modcraft_engine import Move
from behavior_base import Behavior


class Condition:
    def check(self, entity, world, ctx):
        raise NotImplementedError

    def __and__(self, other): return _And(self, other)
    def __or__(self, other):  return _Or(self, other)
    def __invert__(self):     return _Not(self)


class _And(Condition):
    def __init__(self, a, b): self.a, self.b = a, b
    def check(self, e, w, c): return self.a.check(e, w, c) and self.b.check(e, w, c)


class _Or(Condition):
    def __init__(self, a, b): self.a, self.b = a, b
    def check(self, e, w, c): return self.a.check(e, w, c) or self.b.check(e, w, c)


class _Not(Condition):
    def __init__(self, a): self.a = a
    def check(self, e, w, c): return not self.a.check(e, w, c)


class Action:
    """Base for rule actions.

    `run(entity, world, ctx)` returns `(plan_or_action, goal_str[, duration])`
    in the same shape as a legacy `decide()` return — or `None` to decline
    (letting RulesBehavior try the next matching rule).

    Optional `tick(dt)` runs every decide() for time-accumulated state
    (cooldowns). It is called on every rule's action, not just the one
    that fired, so cooldowns drain whether or not the action ran.
    """
    def run(self, entity, world, ctx):
        raise NotImplementedError


def init_home(entity, ctx):
    """Resolve home position from ctx cache, entity.home_* props, or
    entity's first-seen position. Cached in ctx so later rules see it."""
    home = ctx.get("home")
    if home is not None:
        return home
    hx = entity.get("home_x")
    hy = entity.get("home_y")
    hz = entity.get("home_z")
    if hx is not None and hz is not None:
        home = (float(hx),
                float(hy) if hy is not None else entity.y,
                float(hz))
    else:
        home = (entity.x, entity.y, entity.z)
    ctx["home"] = home
    return home


class RulesBehavior(Behavior):
    """Behavior that evaluates a rule list each decide().

    Subclass sets `self.rules = [(Condition, Action), ...]` in __init__.
    """
    def __init__(self):
        self.ctx = {}
        self.rules = []

    def decide(self, entity, world):
        dt = world.dt
        for _cond, action in self.rules:
            if hasattr(action, "tick"):
                action.tick(dt)
        for cond, action in self.rules:
            if cond.check(entity, world, self.ctx):
                result = action.run(entity, world, self.ctx)
                if result is not None:
                    return result
        return Move(entity.x, entity.y, entity.z), "Idle"

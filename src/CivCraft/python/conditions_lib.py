"""conditions_lib.py — Reusable predicates for rule-based behaviors.

Combine with `&`, `|`, `~`:
    Threatened(range=5) & ~NearType("cat")
"""
import random
from rules import Condition, init_home


class Always(Condition):
    def check(self, e, w, c): return True


class Never(Condition):
    def check(self, e, w, c): return False


class Chance(Condition):
    """True with probability p each check. Stateless."""
    def __init__(self, p): self.p = p
    def check(self, e, w, c): return random.random() < self.p


# ── Time-of-day ──────────────────────────────────────────────────────────

class IsNight(Condition):
    def check(self, e, w, c): return w.time < 0.25


class IsMorning(Condition):
    def check(self, e, w, c): return 0.25 <= w.time < 0.50


class IsAfternoon(Condition):
    def check(self, e, w, c): return 0.50 <= w.time < 0.75


class IsEvening(Condition):
    def check(self, e, w, c): return w.time >= 0.75


class IsEveningOrNight(Condition):
    def check(self, e, w, c): return w.time >= 0.75 or w.time < 0.25


# ── Proximity ────────────────────────────────────────────────────────────

class NearType(Condition):
    """True if an entity of `type_id` is within `range`."""
    def __init__(self, type_id, range=20.0):
        self.type_id = type_id
        self.range = range
    def check(self, e, w, c):
        return w.get(self.type_id, max_dist=self.range) is not None


class NearPlayer(Condition):
    def __init__(self, range=10.0): self.range = range
    def check(self, e, w, c):
        return w.get("player", max_dist=self.range) is not None


class Threatened(Condition):
    """True if any living entity of a different species is within `range`.
    If `types` is given, only those type ids count as threats."""
    def __init__(self, range=5.0, types=None):
        self.range = range
        self.types = set(types) if types else None
    def check(self, e, w, c):
        for x in w.entities:
            if x.distance > self.range:
                continue
            if self.types is None:
                if x.kind == "living" and x.type != e.type:
                    return True
            elif x.type in self.types:
                return True
        return False


class FarFromHome(Condition):
    def __init__(self, radius=25.0): self.radius = radius
    def check(self, e, w, c):
        home = init_home(e, c)
        dx = e.x - home[0]; dz = e.z - home[2]
        return dx * dx + dz * dz > self.radius * self.radius


class FarFromFlock(Condition):
    """True if nearest same-species entity exists but is > range away."""
    def __init__(self, range=6.0): self.range = range
    def check(self, e, w, c):
        friends = w.all(e.type)
        if not friends:
            return False
        nearest = min(friends, key=lambda f: f.distance)
        return nearest.distance > self.range


# ── Self state ───────────────────────────────────────────────────────────

class HpBelow(Condition):
    def __init__(self, threshold): self.threshold = threshold
    def check(self, e, w, c): return e.hp < self.threshold


class OnBlockType(Condition):
    """True if the block directly beneath the entity matches `block_id`."""
    def __init__(self, block_id): self.block_id = block_id
    def check(self, e, w, c):
        for b in w.all(self.block_id):
            if abs(b.x - e.x) < 1.5 and abs(b.z - e.z) < 1.5 \
                    and abs(b.y - (e.y - 1)) < 1.5:
                return True
        return False

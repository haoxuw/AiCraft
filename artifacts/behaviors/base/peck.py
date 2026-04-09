"""Peck — timid ground-feeding bird that flocks, pecks grass, and roosts.

Priority order:
  1. Evening/night — roost at home
  2. Too far from home — return
  3. Flee from threats (lay egg on first startle)
  4. Peck grass (if standing on grass block)
  5. Flock with same-type birds nearby
  6. Peck seeds or scratch ground

Entity props (optional):
  scatter_range — flee trigger distance (default 4)
  peck_chance   — probability of pecking vs scratching (default 0.45)
  home_radius   — max wander distance from spawn (default 25)
"""
import random
from modcraft_engine import Move, Convert, Ground, LivingName, ItemName, BlockType
from behavior_base import Behavior

EGG_COOLDOWN = 10.0
EGG_CHANCE   = 0.20


class PeckBehavior(Behavior):

    def __init__(self):
        self._home = None
        self._sleeping = False
        self._resting = False
        self._activity = "idle"
        self._activity_timer = 0.0
        self._egg_cooldown = 0.0
        self._rng_seeded = False

    def decide(self, entity, local_world):
        if not self._rng_seeded:
            random.seed(entity.id * 31337 + 42)
            self._rng_seeded = True

        dt = local_world.dt
        self._activity_timer -= dt
        self._egg_cooldown   -= dt
        self._home = self.init_home(entity, self._home)

        scatter_range = float(entity.get("scatter_range", 4.0))
        peck_chance   = float(entity.get("peck_chance",   0.45))
        home_radius   = float(entity.get("home_radius",  25.0))
        spd           = entity.walk_speed

        # ── DEBUG: log all visible entities every decide ─────────────────────
        print(f"[chicken {entity.id}] pos=({entity.x:.0f},{entity.z:.0f}) sees {len(local_world.entities)} entities:", flush=True)
        for e in local_world.entities:
            print(f"  - id={e.id} type={e.type_id} kind={e.kind} dist={e.distance:.1f}", flush=True)

        # ── Flee from threats — HIGHEST PRIORITY (always check first) ────────
        threats = [e for e in local_world.entities
                   if e.distance <= scatter_range
                   and e.kind == "living"
                   and e.type_id != entity.type_id]
        if threats:
            closest = min(threats, key=lambda t: t.distance)
            print(f"[chicken {entity.id}] FLEEING from {closest.type_id} at dist {closest.distance:.1f}", flush=True)
            if self._egg_cooldown <= 0 and entity.hp > 2:
                if random.random() < EGG_CHANCE:
                    self._egg_cooldown = EGG_COOLDOWN
                    return (Convert(from_item="hp", from_count=2,
                                         to_item=ItemName.Egg, to_count=1,
                                         convert_into=Ground()),
                            "BAWK!! *lays egg!*")
            return Move(*self.flee_pos(entity, closest), speed=6.0), "BAWK!! Scattering!"

        dist_home = self.dist2d(entity.x, entity.z, self._home[0], self._home[2])

        # ── Evening/Night: roost at home ─────────────────────────────────────
        if self.is_night(local_world) or self.is_evening(local_world):
            self._resting = True
            if self.is_night(local_world):
                self._sleeping = True
            if dist_home > 3:
                return Move(*self._home, speed=spd), "Heading home to roost..."
            if self._sleeping:
                return Move(entity.x, entity.y, entity.z),"Roosting zzz"
            return Move(entity.x, entity.y, entity.z),"Settling in to roost"

        if self._resting:
            self._resting = False
            self._sleeping = False
            return Move(entity.x, entity.y, entity.z),"Good morning! *cluck*"

        if dist_home > home_radius:
            return Move(*self._home, speed=spd * 0.8), "Wandering back home"

        # ── Peck grass (if standing on grass block) ──────────────────────────
        if self._activity == "peck_grass" and self._activity_timer > 0:
            return Move(entity.x, entity.y, entity.z),"Pecking grass"

        on_grass = local_world.get(BlockType.Grass) is not None and any(
            abs(b.x - entity.x) < 1.5 and
            abs(b.z - entity.z) < 1.5 and
            abs(b.y - (entity.y - 1)) < 1.5
            for b in local_world.all(BlockType.Grass)
        )
        if on_grass and random.random() < 0.10 and self._activity_timer <= 0:
            self._activity = "peck_grass"
            self._activity_timer = 2.0 + random.random() * 3.0
            return Move(entity.x, entity.y, entity.z),"Pecking grass"

        # ── Flock (only if same-type animals are actually nearby) ─────────────
        friends = local_world.all(entity.type_id)
        if friends:
            farthest = max(friends, key=lambda e: e.distance)
            if farthest.distance > 4:
                return Move(farthest.x, farthest.y, farthest.z, speed=spd), \
                       "Rejoining flock"

        # ── Peck or scratch ──────────────────────────────────────────────────
        self._activity = "idle"
        if random.random() < peck_chance:
            return Move(entity.x, entity.y, entity.z),"Pecking at seeds"

        return Move(*self.wander_target(entity, radius=8), speed=1.8), "Scratching ground"

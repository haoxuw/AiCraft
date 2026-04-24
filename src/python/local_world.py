"""local_world.py — Agent-side pydantic models: LocalWorld and SelfEntity.

Architecture
────────────────────────────────────────────────────────────────────────
Server  GlobalWorld (C++, authoritative)
  │  TCP: S_BLOCK, S_ENTITY, S_CHUNK
  ▼
Agent   LocalWorld (Python, cached subset)
  │
  ▼
Behavior.decide(entity: SelfEntity, world: LocalWorld) → (action, goal)

GlobalWorld is owned entirely by the server (C++). It is never exposed to
Python. LocalWorld is rebuilt each tick from the agent's C++ caches:

  • blocks   — nearby non-trivial blocks scanned from loaded chunks,
               within work_radius (default 80), pre-sorted nearest-first.
  • entities — all entities within 64 units, from the entity cache.

Staleness policy
────────────────
LocalWorld may be outdated — a block the agent sees might already be gone;
an entity might have moved. This is expected and acceptable. The server
validates every ActionProposal and rejects impossible ones (SourceBlockGone,
EntityGone, …). Behaviors recover on the next decide() tick.

Performance
───────────
model_construct() bypasses pydantic validators — data comes from C++ and
is trusted. model_post_init() is called manually to build the spatial
index after model_construct(). At 4 Hz this overhead is negligible.

Scale (future work)
───────────────────
Current: 1 civcraft-agent process per entity (one Python interpreter each).
For large deployments, migrate to multi-entity agent processes: one process
handles a batch of entities sharing one interpreter. LocalWorld is designed
to be compatible — each entity gets its own instance per tick.
"""

from __future__ import annotations
from typing import Optional, Union, Any
from pydantic import BaseModel, ConfigDict, Field, PrivateAttr


# ── Spatial primitives ────────────────────────────────────────────────────────

class BlockView(BaseModel):
    """A block visible to this agent within its loaded chunk cache.

    Comes pre-sorted nearest-first from the C++ block scanner.
    May be stale: the block might have been placed or broken since the last
    S_BLOCK TCP update arrived.

    `kind` is "block" for normal blocks and "annotation" for block decorators
    (flowers, moss, …) exposed via the same query API. See shared/annotation.h.
    """
    model_config = ConfigDict(frozen=True)

    x: int
    y: int
    z: int
    type: str    # e.g. "logs", "wood", "cobblestone"
    distance: float
    kind: str = "block"    # "block" or "annotation"


class EntityView(BaseModel):
    """A nearby entity visible to this agent (within 64-unit radius).

    From the agent's entity cache, updated on S_ENTITY broadcasts.
    """
    model_config = ConfigDict(frozen=True)

    id: int
    type: str    # e.g. "chicken", "villager"
    kind: str = "living"  # "living" or "item" (EntityKind)
    x: float
    y: float
    z: float
    distance: float
    hp: int
    tags: list[str] = Field(default_factory=list)  # feature tags: "humanoid", "hostile", etc.

    def has_tag(self, tag: str) -> bool:
        """Check if this entity has the given feature tag."""
        return tag in self.tags


# Unified return type for block-or-entity queries
Nearby = Union[BlockView, EntityView]


# ── Inventory ─────────────────────────────────────────────────────────────────

class InventoryView(BaseModel):
    """Read-only snapshot of an entity's inventory."""
    model_config = ConfigDict(frozen=True)

    items: dict[str, int] = Field(default_factory=dict)

    def count(self, item_id: str) -> int:
        """How many of item_id the entity is carrying."""
        return self.items.get(item_id, 0)

    def total_value(self) -> float:
        """Sum of material values across all items.

        Uses civcraft_engine.material_value() — single source of truth in
        src/shared/material_values.h. Never hardcode values in Python.
        """
        from civcraft_engine import material_value
        return sum(material_value(k) * v for k, v in self.items.items() if v > 0)

    def can_accept(self, item_id: str, count: int, capacity: float) -> bool:
        """Would adding (item × count) keep total value ≤ capacity?

        Mirrors C++ Inventory::canAccept (shared/inventory.h) — same logic,
        same values (via civcraft_engine.material_value).
        """
        from civcraft_engine import material_value
        if capacity <= 0:
            return False
        return self.total_value() + material_value(item_id) * count <= capacity + 1e-4

    def __bool__(self) -> bool:
        return bool(self.items)


# ── SelfEntity ────────────────────────────────────────────────────────────────

class SelfEntity(BaseModel):
    """Full state of the entity running this behavior.

    Standard engine attributes are typed fields. Server-assigned custom
    props (work_radius, chop_period, …) are accessed via .get(prop,
    default) — they vary per entity type and are set by server spawn logic.

    Usage
    -----
        entity.x, entity.y, entity.z      # position
        entity.hp, entity.walk_speed       # stats
        entity.inventory.count("logs")
        entity.get("work_radius", 80.0)    # custom server prop
    """
    model_config = ConfigDict(frozen=True)

    id:         int
    type:    str
    x:          float
    y:          float
    z:          float
    yaw:        float
    hp:         int
    walk_speed: float
    inventory_capacity: float = 0.0
    on_ground:  bool
    inventory:  InventoryView

    # All raw props from C++ — used by .get() for custom server-assigned values.
    # Includes standard fields too (id, x, y, …) which is fine for a trusted source.
    props: dict[str, Any] = Field(default_factory=dict)

    def get(self, prop: str, default=None):
        """Read a server-assigned custom property (e.g. 'work_radius')."""
        return self.props.get(prop, default)

    @classmethod
    def _from_raw(cls, raw: dict) -> "SelfEntity":
        """Construct from C++ bridge dict with full pydantic validation.

        Pydantic coerces types (e.g. "5" → 5 for int fields), so even if
        C++ passes string-encoded values they're converted to the right type.
        """
        return cls(
            id         = raw["id"],
            type    = raw["type"],
            x          = raw["x"],
            y          = raw["y"],
            z          = raw["z"],
            yaw        = raw["yaw"],
            hp         = raw["hp"],
            walk_speed = raw["walk_speed"],
            inventory_capacity = raw.get("inventory_capacity", 0.0),
            on_ground  = raw["on_ground"],
            inventory  = InventoryView(items=dict(raw.get("inventory", {}))),
            props=raw,
        )


# ── LocalWorld ────────────────────────────────────────────────────────────────

class LocalWorld(BaseModel):
    """Agent-side cache: a spatial subset of GlobalWorld.

    Rebuilt each tick from the agent's C++ block and entity caches.
    Populated via TCP events (S_BLOCK, S_ENTITY, S_CHUNK) as the server
    broadcasts changes. May be stale — this is by design and expected.

    Spatial index
    -------------
    On construction, blocks and entities are grouped by type into
    _by_type (nearest-first). Entities are also grouped by category into
    _by_type. All queries are O(1) index lookup + O(k) early-exit scan.

    Query API
    ---------
        world.get("logs")                 # nearest Block, or None
        world.get("logs", max_dist=40)    # nearest within 40 units
        world.get("spider")                # nearest spider Entity, or None
        world.all("logs")                 # [BlockView, …] nearest-first
        world.all("logs", max_dist=40)    # filtered by distance
        world.nearest("pig")                    # nearest pig EntityView
        world.nearest("chicken", max_dist=12)   # with distance cap
        # For "any playable character", filter by tag (no hardcoded type id):
        #   [x for x in world.entities if x.has_tag("playable")]
        world.time                              # 0.0–1.0 day fraction
        world.dt                                # frame delta seconds
    """
    model_config = ConfigDict(frozen=False)   # private attr mutation needs non-frozen

    time:     float                    # 0.0 (midnight) – 1.0 (next midnight)
    dt:       float                    # seconds since last decide()
    blocks:   list[BlockView]  = Field(default_factory=list)
    entities: list[EntityView] = Field(default_factory=list)
    goal:     Optional[dict]   = None  # {"x","y","z"} client-issued target, or None

    # Outcome of the previous plan (event-driven decide loop).
    # decide() is called only when the previous plan ended or was
    # interrupted; this field carries the "why" so a behavior can
    # branch (e.g. Failed("stuck") → pick a different target).
    #
    # last_outcome is one of:
    #   "success" — plan finished normally
    #   "failed"  — plan aborted; last_reason describes it
    #   "none"    — this is the first decide() for the entity
    last_outcome: str = "none"
    last_goal:    str = ""    # previous decide()'s goal string
    last_reason:  str = ""    # e.g. "stuck", "target_gone",
                              # "interrupt:hp", "interrupt:proximity",
                              # "interrupt:time_of_day"

    # ExecState::toString() — see src/platform/agent/outcome.h for the full
    # enum. Prefer this over string-matching last_reason when branching.
    last_state:       str = "Idle"
    last_fail_streak: int = 0    # consecutive Failed_* outcomes, reset on Success

    # Spatial indices — built in model_post_init, not exposed as pydantic fields
    _by_type:     dict[str, list[Nearby]]     = PrivateAttr(default_factory=dict)

    def model_post_init(self, __context: Any) -> None:
        """Build spatial index after construction."""
        by_type: dict[str, list] = {}
        for b in self.blocks:
            by_type.setdefault(b.type, []).append(b)

        ent_by_type: dict[str, list] = {}
        for e in self.entities:
            ent_by_type.setdefault(e.type, []).append(e)

        for lst in ent_by_type.values():
            lst.sort(key=lambda e: e.distance)

        for type, lst in ent_by_type.items():
            by_type.setdefault(type, []).extend(lst)

        self._by_type     = by_type

    # ── Block query (arbitrary world position) ────────────────────────────────

    def get_block(self, x: int, y: int, z: int) -> str:
        """Return the block type string at world position (x, y, z).

        Queries the agent's local chunk cache via the C++ bridge.
        Valid only inside decide(). Returns 'base:air' for unloaded positions.
        Primarily used by pathfinding helpers (see python/pathfind.py).
        """
        from civcraft_engine import get_block as _gb
        return _gb(int(x), int(y), int(z))

    # ── Spatial queries ───────────────────────────────────────────────────────

    def get(self, type: str, max_dist: float = None) -> Optional[Nearby]:
        """Nearest block or entity of type within max_dist, or None."""
        for obj in self._by_type.get(type, ()):
            if max_dist is None or obj.distance <= max_dist:
                return obj
        return None

    def all(self, type: str, max_dist: float = None) -> list[Nearby]:
        """All blocks/entities/annotations of type, nearest-first, within max_dist.

        Annotations (block decorators) are queried on-demand via the C++ bridge
        — they aren't pre-populated in the LocalWorld's block list because
        they don't occupy chunk cells. Each annotation hit carries kind=
        "annotation"; regular blocks carry kind="block".
        """
        hits = list(self._by_type.get(type, ()))
        if max_dist is not None:
            hits = [o for o in hits if o.distance <= max_dist]

        # Fresh-scan annotations every call (cheap: one C++ hash lookup per
        # loaded chunk in the search box). Callers get a unified block/ann list.
        try:
            from civcraft_engine import scan_annotations as _sa
            md = 80.0 if max_dist is None else float(max_dist)
            anns = _sa(type, None, md, 64)
            for a in anns:
                hits.append(BlockView(
                    x=int(a["x"]), y=int(a["y"]), z=int(a["z"]),
                    type=a["type"], distance=float(a["distance"]),
                    kind="annotation",
                ))
        except Exception:
            pass  # bridge not available (e.g. unit tests) — blocks-only fallback

        hits.sort(key=lambda o: o.distance)
        return hits

    def nearest(self, type: str, max_dist: float = None) -> Optional[EntityView]:
        """Nearest entity by type (e.g. 'base:player', 'base:chicken')."""
        for obj in self._by_type.get(type, ()):
            if not isinstance(obj, EntityView): continue
            if max_dist is None or obj.distance <= max_dist:
                return obj
        return None

    # ── Construction ──────────────────────────────────────────────────────────

    @classmethod
    def _from_raw(cls, raw: dict) -> "LocalWorld":
        """Construct from C++ bridge dict with full pydantic validation.
        Pydantic coerces string→int/float as needed.
        """
        blocks = [
            BlockView(
                x=b["x"], y=b["y"], z=b["z"],
                type=b["type"],
                distance=b["distance"],
            )
            for b in raw.get("blocks", [])
        ]
        entities = [
            EntityView(
                id=e["id"], type=e["type"],
                kind=e.get("kind", "living"),
                x=e["x"], y=e["y"], z=e["z"],
                distance=e["distance"], hp=e["hp"],
                tags=list(e.get("tags", [])),
            )
            for e in raw.get("nearby", [])
        ]
        return cls(
            time=raw["time"],
            dt=raw["dt"],
            blocks=blocks,
            entities=entities,
            goal=raw.get("goal"),
            last_outcome=raw.get("last_outcome", "none"),
            last_goal=raw.get("last_goal", ""),
            last_reason=raw.get("last_reason", ""),
            last_state=raw.get("last_state", "Idle"),
            last_fail_streak=int(raw.get("last_fail_streak", 0)),
        )

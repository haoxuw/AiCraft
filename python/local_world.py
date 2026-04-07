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
Current: 1 modcraft-agent process per entity (one Python interpreter each).
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
    """
    model_config = ConfigDict(frozen=True)

    x: int
    y: int
    z: int
    type_id: str    # e.g. "base:trunk", "base:wood", "base:cobblestone"
    distance: float


class EntityView(BaseModel):
    """A nearby entity visible to this agent (within 64-unit radius).

    From the agent's entity cache, updated on S_ENTITY broadcasts.
    """
    model_config = ConfigDict(frozen=True)

    id: int
    type_id: str    # e.g. "base:chicken", "base:villager", "base:spider"
    category: str   # e.g. "animal", "hostile", "player", "npc", "chest", "item"
    x: float
    y: float
    z: float
    distance: float
    hp: int


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

    def __bool__(self) -> bool:
        return bool(self.items)


# ── SelfEntity ────────────────────────────────────────────────────────────────

class SelfEntity(BaseModel):
    """Full state of the entity running this behavior.

    Standard engine attributes are typed fields. Server-assigned custom
    props (home_x, home_z, work_radius, collect_goal, chest_x, …) are
    accessed via .get(prop, default) — they vary per entity type and are
    set by server spawn logic.

    Usage
    -----
        entity.x, entity.y, entity.z      # position
        entity.hp, entity.walk_speed       # stats
        entity.inventory.count("base:trunk")
        entity.get("work_radius", 80.0)    # custom server prop
    """
    model_config = ConfigDict(frozen=True)

    id:         int
    type_id:    str
    x:          float
    y:          float
    z:          float
    yaw:        float
    hp:         int
    walk_speed: float
    on_ground:  bool
    inventory:  InventoryView

    # All raw props from C++ — used by .get() for custom server-assigned values.
    # Includes standard fields too (id, x, y, …) which is fine for a trusted source.
    props: dict[str, Any] = Field(default_factory=dict)

    def get(self, prop: str, default=None):
        """Read a server-assigned custom property (e.g. 'work_radius', 'home_x')."""
        return self.props.get(prop, default)

    @classmethod
    def _from_raw(cls, raw: dict) -> "SelfEntity":
        """Construct from C++ bridge dict, bypassing pydantic validation.

        Called once per decide() tick per entity. model_construct() skips
        all validators — the data is trusted (comes directly from C++).
        """
        return cls.model_construct(
            id         = raw["id"],
            type_id    = raw["type_id"],
            x          = raw["x"],
            y          = raw["y"],
            z          = raw["z"],
            yaw        = raw["yaw"],
            hp         = raw["hp"],
            walk_speed = raw["walk_speed"],
            on_ground  = raw["on_ground"],
            inventory  = InventoryView.model_construct(
                items=dict(raw.get("inventory", {}))
            ),
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
    On construction, blocks and entities are grouped by type_id into
    _by_type (nearest-first). Entities are also grouped by category into
    _by_category. All queries are O(1) index lookup + O(k) early-exit scan.

    Query API
    ---------
        world.get("base:trunk")                 # nearest Block, or None
        world.get("base:trunk", max_dist=40)    # nearest within 40 units
        world.get("base:spider")                # nearest spider Entity, or None
        world.all("base:trunk")                 # [BlockView, …] nearest-first
        world.all("base:trunk", max_dist=40)    # filtered by distance
        world.nearest("player")                 # nearest player EntityView
        world.nearest("animal", max_dist=12)    # with distance cap
        world.time                              # 0.0–1.0 day fraction
        world.dt                                # frame delta seconds
    """
    model_config = ConfigDict(frozen=False)   # private attr mutation needs non-frozen

    time:     float                    # 0.0 (midnight) – 1.0 (next midnight)
    dt:       float                    # seconds since last decide()
    blocks:   list[BlockView]  = Field(default_factory=list)
    entities: list[EntityView] = Field(default_factory=list)

    # Spatial indices — built in model_post_init, not exposed as pydantic fields
    _by_type:     dict[str, list[Nearby]]     = PrivateAttr(default_factory=dict)
    _by_category: dict[str, list[EntityView]] = PrivateAttr(default_factory=dict)

    def model_post_init(self, __context: Any) -> None:
        """Build spatial index after construction."""
        by_type: dict[str, list] = {}
        # Blocks are pre-sorted nearest-first by C++ — preserve that order
        for b in self.blocks:
            by_type.setdefault(b.type_id, []).append(b)

        ent_by_type: dict[str, list] = {}
        by_cat: dict[str, list] = {}
        for e in self.entities:
            ent_by_type.setdefault(e.type_id, []).append(e)
            by_cat.setdefault(e.category, []).append(e)

        for lst in ent_by_type.values():
            lst.sort(key=lambda e: e.distance)
        for lst in by_cat.values():
            lst.sort(key=lambda e: e.distance)

        for type_id, lst in ent_by_type.items():
            by_type.setdefault(type_id, []).extend(lst)

        self._by_type     = by_type
        self._by_category = by_cat

    # ── Spatial queries ───────────────────────────────────────────────────────

    def get(self, type_id: str, max_dist: float = None) -> Optional[Nearby]:
        """Nearest block or entity of type_id within max_dist, or None."""
        for obj in self._by_type.get(type_id, ()):
            if max_dist is None or obj.distance <= max_dist:
                return obj
        return None

    def all(self, type_id: str, max_dist: float = None) -> list[Nearby]:
        """All blocks or entities of type_id, nearest-first, within max_dist."""
        hits = self._by_type.get(type_id, ())
        if max_dist is None:
            return list(hits)
        return [o for o in hits if o.distance <= max_dist]

    def nearest(self, category: str, max_dist: float = None) -> Optional[EntityView]:
        """Nearest entity by category (e.g. 'player', 'animal', 'chest', 'hostile')."""
        for obj in self._by_category.get(category, ()):
            if max_dist is None or obj.distance <= max_dist:
                return obj
        return None

    # ── Construction ──────────────────────────────────────────────────────────

    @classmethod
    def _from_raw(cls, raw: dict) -> "LocalWorld":
        """Construct from C++ bridge dict, bypassing pydantic validation.

        Note: blocks use 'type' key in the C++ bridge dict; we rename to type_id.
        model_post_init is called manually since model_construct skips it.
        """
        blocks = [
            BlockView.model_construct(
                x=b["x"], y=b["y"], z=b["z"],
                type_id=b["type"],          # C++ bridge uses "type", not "type_id"
                distance=b["distance"],
            )
            for b in raw.get("blocks", [])
        ]
        entities = [
            EntityView.model_construct(
                id=e["id"], type_id=e["type_id"], category=e["category"],
                x=e["x"], y=e["y"], z=e["z"],
                distance=e["distance"], hp=e["hp"],
            )
            for e in raw.get("nearby", [])
        ]
        obj = cls.model_construct(
            time=raw["time"],
            dt=raw["dt"],
            blocks=blocks,
            entities=entities,
        )
        obj.model_post_init(None)
        return obj

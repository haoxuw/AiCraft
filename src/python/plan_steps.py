"""Typed plan-step models — the sole schema crossing Python → C++ executor.

Behaviors construct these and return a list from decide(). The C++ bridge
(parsePyResult in python_bridge.cpp) calls model_dump() and reads each field
unconditionally — no .contains() guards — because Pydantic defaults
guarantee every field is present.

Bridge contract: this file is authoritative. The matching C++ mirror lives
in src/platform/agent/behavior.h (struct PlanStep). When adding/renaming a
field here, update the parser in python_bridge.cpp::parsePyResult and the
PlanStep struct in behavior.h together.

To add a new step type:
  1. Add a `<Name>Step(BaseModel)` here with a `type: Literal["<tag>"]` and
     defaults on every optional field.
  2. Add the matching branch in parsePyResult keyed on the same `<tag>`.
  3. Make the C++ PlanStep struct field match the Python field name
     (snake_case → camelCase is fine; the bridge does the mapping).

All models are extra="forbid" so typos fail loudly at decide() time instead
of silently dropping fields on the C++ side.
"""
from typing import List, Literal

from pydantic import BaseModel, ConfigDict

_ENTITY_NONE = 0  # Matches constants.h::ENTITY_NONE.


class Vec3(BaseModel):
    model_config = ConfigDict(extra="forbid")
    x: float
    y: float
    z: float


# Movement is always target-directed — toward a resource (HarvestStep.candidates),
# a target entity (AttackStep.entity_id), or a chest (RelocateStep). A generic
# MoveStep(x,y,z) primitive would encourage "go to this coordinate" plans that
# go stale the instant the target moves, so there is no such step type.


class HarvestStep(BaseModel):
    """Chop at one of `candidates` until the inventory can't accept one more
    `item`. `gather_types` is priority-ordered (index 0 = highest). The
    per-swing scan radius and cooldown live on the executor — Python only
    declares *what* and *where*."""
    model_config = ConfigDict(extra="forbid")
    type: Literal["harvest"] = "harvest"
    candidates: List[Vec3]
    gather_types: List[str]
    item: str
    use_navigator: bool = False
    ignore_height: bool = False
    hold: float = 0.0


class AttackStep(BaseModel):
    model_config = ConfigDict(extra="forbid")
    type: Literal["attack"] = "attack"
    entity_id: int
    hold: float = 0.0


class RelocateStep(BaseModel):
    """Move `count` of `item` from self to self (the executor resolves the
    actual from/to containers via the step type)."""
    model_config = ConfigDict(extra="forbid")
    type: Literal["relocate"] = "relocate"
    item: str
    count: int = 1
    hold: float = 0.0

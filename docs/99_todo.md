# TODO

Deferred work that's intentional, not forgotten. Add bullets as you notice
them; move to a real task / commit when you pick one up.

## Open

- **`reason or "no valid path"` string-fallback audit.** Find and
  refactor Python sites that return free-form error strings where an enum
  (`ExecState::Failed_*`, a Python IntEnum mirror) would be type-checked.
  Grep hasn't surfaced the specific site yet — drop a concrete example
  here when one shows up.

## Landed (remove if you agree)

- ~~**Replace `pyWorld` dict with pydantic model.**~~ `python_bridge.cpp`
  now writes a flat schema-shaped dict (`entities`, `inventory={items:…}`,
  dedicated `props` bag); both `_from_raw`s collapsed to
  `cls.model_validate(raw)`. `SelfEntity` / `LocalWorld` / `BlockView` /
  `EntityView` / `InventoryView` run with `extra='forbid'` so a C++-side
  key typo raises immediately.
- ~~**Unify `RtsExecutor` + `BehaviorExecutor`.**~~ Unified `PathExecutor`
  serves both.
- ~~**Waypoint compression.**~~ Collinear-Walk compression in
  `pathfind.cpp` — a 56-block corridor ships 1 waypoint, not 56.
- ~~**Enum → string switch copies.**~~ `toString(PlanStep::Type)`,
  `toString(MoveKind)`, `toString(Navigator::Status)` hoisted next to
  their enums (matching the existing `toString(ExecState)` style in
  `outcome.h`). Five inline copies retired.

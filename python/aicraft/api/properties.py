"""Property descriptor for typed, validated, trackable Object attributes."""

from __future__ import annotations
from typing import Any


class Property:
    """A typed attribute on an Object.

    Properties are the data that defines what an Object IS.
    They are validated, serializable, and change-tracked.

    Args:
        default:     Default value for new instances.
        min_val:     Minimum allowed value (numeric types).
        max_val:     Maximum allowed value (numeric types).
        sync:        If True, changes are sent to clients.
        persist:     If True, saved to disk.
        tick_rate:   Auto-change per second (e.g. -0.01 for hunger decay).
        description: Human-readable description.
    """

    def __init__(
        self,
        default: Any = None,
        *,
        min_val: float | None = None,
        max_val: float | None = None,
        sync: bool = True,
        persist: bool = True,
        tick_rate: float = 0.0,
        description: str = "",
    ):
        self.default = default
        self.min_val = min_val
        self.max_val = max_val
        self.sync = sync
        self.persist = persist
        self.tick_rate = tick_rate
        self.description = description
        self._name: str = ""

    def __set_name__(self, owner: type, name: str):
        self._name = name

    def __get__(self, obj: Any, objtype: type | None = None) -> Any:
        if obj is None:
            return self
        return obj.__dict__.get(self._name, self.default)

    def __set__(self, obj: Any, value: Any):
        if self.min_val is not None and isinstance(value, (int, float)):
            value = max(self.min_val, value)
        if self.max_val is not None and isinstance(value, (int, float)):
            value = min(self.max_val, value)
        old = obj.__dict__.get(self._name, self.default)
        obj.__dict__[self._name] = value
        if old != value and hasattr(obj, '_dirty_fields'):
            obj._dirty_fields.add(self._name)

    def clamp(self, value: Any) -> Any:
        if self.min_val is not None:
            value = max(self.min_val, value)
        if self.max_val is not None:
            value = min(self.max_val, value)
        return value

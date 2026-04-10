"""stats.py — Lightweight aggregated counters for behavior diagnostics.

Tracks how many times key events happen across the agent lifetime,
broken down by entity type or block type. Prints a clean summary on
process exit (via atexit) — no per-tick log spam.

Usage from any Python module:
    from stats import stats
    stats.inc("decide", "base:villager")
    stats.inc("pathfind")
    stats.inc("stuck", "base:chicken")
"""

import atexit
import sys
from collections import defaultdict


class Stats:
    """Global counter registry. One instance per agent process."""

    def __init__(self):
        self._counters: dict[str, int] = defaultdict(int)
        self._sub: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))

    def inc(self, category: str, sub: str = None, n: int = 1):
        """Increment a counter. Optional sub-key for per-type breakdown."""
        self._counters[category] += n
        if sub:
            self._sub[category][sub] += n

    def get(self, category: str, sub: str = None) -> int:
        if sub:
            return self._sub[category].get(sub, 0)
        return self._counters.get(category, 0)

    def dump(self):
        """Print a formatted summary to stderr."""
        if not self._counters:
            return
        print("\n=== ModCraft Agent Stats ===", file=sys.stderr)
        # Sort categories alphabetically for stable output
        for cat in sorted(self._counters):
            total = self._counters[cat]
            print(f"  {cat:30s} {total:>8d}", file=sys.stderr)
            if cat in self._sub:
                for sub in sorted(self._sub[cat], key=lambda s: -self._sub[cat][s]):
                    count = self._sub[cat][sub]
                    print(f"    {sub:28s} {count:>8d}", file=sys.stderr)
        print("============================\n", file=sys.stderr)


stats = Stats()
atexit.register(stats.dump)

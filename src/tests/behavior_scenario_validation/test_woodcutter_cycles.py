"""E2E test — villager completes a gather→deposit cycle without stalling.

Regressions this catches (all seen live in `make game` template=1):

  1. Fall-in-hole stall — villager chops lower leaves/logs, falls into the
     resulting air pocket, the remaining trunk is now > GATHER_RADIUS
     vertically above them, and decide() keeps picking the same unreachable
     anchor. Villager's goal says "Chopping trees" indefinitely, but logs
     count never grows and y stays below 2.

  2. Never-deposit stall — humanoids register with
     inventory_capacity=infinity (for player looting), and the woodcutter's
     `_update_state` gates DEPOSIT on `not inventory.can_accept(...)`, which
     is never true for inf cap. Villager chops forever without a single
     StoreItem action.

The previous version of this test used template=2 (test_behaviors arena) and
observer-only state tracking. Neither reproduces the `make game` bug:
template=2 arenas don't stamp a village, and the observer's seat consumes
seat 1 so the agent-host lands in seat 2 where its villagers are outside
the observer's proximity filter.

Data source: the agent-host's per-entity elog files at
`/tmp/civcraft_entity_<id>.log`, plus the top-level `/tmp/civcraft_game.log`
for DECIDE goal broadcasts. These are written by the Python behaviors and
the DECIDE broadcaster regardless of seat visibility.
"""

import os
import re
import time
import pytest

from .harness import GameHarness


# Where the woodcutter's elog() calls land, and where the combat-log style
# DECIDE broadcaster writes its stream. Both are truncated on client start.
ENTITY_LOG_GLOB = "/tmp/civcraft_entity_{eid}.log"
GAME_LOG_PATH   = "/tmp/civcraft_game.log"


def _find_villager_ids(game_log_path):
    """Parse game.log for '[DECIDE] Villager #<id>: ...' to discover agent-
    driven villagers. Returns a set of entity IDs."""
    if not os.path.isfile(game_log_path):
        return set()
    ids = set()
    with open(game_log_path) as f:
        for line in f:
            m = re.search(r"\[DECIDE\] Villager #(\d+):", line)
            if m:
                ids.add(int(m.group(1)))
    return ids


_WORK_LINE = re.compile(
    r"^\[(\d\d:\d\d:\d\d)\] work: anchor=\((-?\d+),(-?\d+),(-?\d+)\) "
    r"self=\((-?[\d.]+),(-?[\d.]+),(-?[\d.]+)\) d=([\d.]+) logs=(\d+)"
)

_STATE_LINE = re.compile(
    r"^\[(\d\d:\d\d:\d\d)\] state (\S+) -> (\S+) "
    r"\(inv=([\d.]+|inf) cap=([\d.]+|inf) logs=(\d+)\)"
)

_STORE_LINE = re.compile(r"^\[(\d\d:\d\d:\d\d)\] deposit: StoreItem")


def _parse_villager_elog(path):
    """Return a dict summarising one villager's elog trail.

    { "work_samples": [(ts, ax, ay, az, sx, sy, sz, d, logs)],
      "states":       [(ts, prev, new, inv, cap, logs)],
      "store_events": [ts, ...],
      "max_logs":     int,    # peak logs seen in any line
      "final_y":      float,  # last observed self.y
      "final_logs":   int,    # last observed logs
      "inf_cap":      bool }  # ever saw cap=inf
    """
    out = {
        "work_samples": [], "states": [], "store_events": [],
        "max_logs": 0, "final_y": None, "final_logs": 0, "inf_cap": False,
    }
    if not os.path.isfile(path):
        return out
    with open(path) as f:
        for line in f:
            m = _WORK_LINE.match(line)
            if m:
                ts, ax, ay, az, sx, sy, sz, d, logs = m.groups()
                ax, ay, az = int(ax), int(ay), int(az)
                sx, sy, sz = float(sx), float(sy), float(sz)
                d, logs = float(d), int(logs)
                out["work_samples"].append((ts, ax, ay, az, sx, sy, sz, d, logs))
                out["max_logs"] = max(out["max_logs"], logs)
                out["final_y"] = sy
                out["final_logs"] = logs
                continue
            m = _STATE_LINE.match(line)
            if m:
                ts, prev, new, inv, cap, logs = m.groups()
                out["states"].append((ts, prev, new, inv, cap, int(logs)))
                if cap == "inf":
                    out["inf_cap"] = True
                continue
            m = _STORE_LINE.match(line)
            if m:
                out["store_events"].append(m.group(1))
    return out


class TestWoodcutterGatherCycle:
    """Village world, agent-host drives seat-2 villagers. Assertions read
    the elog files the agent writes for its owned entities."""

    # Template 1 (village), seed 42 is the exact `make game` default combo
    # where the stall reproduces. Observer takes seat 1; agent-host lands
    # in seat 2. The village stamp is deterministic per (seed, seat).
    TEMPLATE = 1
    SEED     = 42
    RUN_SECONDS = 90.0   # long enough for walk-to-tree + chop a few + maybe deposit

    @pytest.fixture(scope="class")
    def game(self):
        # Wipe any stale elog / game log so we only see this run.
        for p in list(os.listdir("/tmp")):
            if p.startswith("civcraft_entity_") and p.endswith(".log"):
                try: os.remove(os.path.join("/tmp", p))
                except OSError: pass
        for p in (GAME_LOG_PATH, GAME_LOG_PATH + ".prev"):
            try: os.remove(p)
            except FileNotFoundError: pass

        with GameHarness(template=self.TEMPLATE, seed=self.SEED,
                         spawn_agent_host=True) as g:
            # Let the agent-host import behaviors, adopt its seat's villagers,
            # and run the first handful of decide ticks.
            time.sleep(self.RUN_SECONDS)
            # Flush the observer once so any pending writes land before asserts.
            g.observer.poll(duration=1.0)
            yield g

    def _villager_summaries(self):
        """Collect (eid, elog_summary) for every villager the agent drove."""
        eids = _find_villager_ids(GAME_LOG_PATH)
        return [(eid, _parse_villager_elog(ENTITY_LOG_GLOB.format(eid=eid)))
                for eid in sorted(eids)]

    def test_agent_host_adopted_villagers(self, game):
        """Precondition: the agent-host's DECIDE stream mentions ≥ 1 villager.
        If this fails, everything else is moot — no behaviors ran."""
        eids = _find_villager_ids(GAME_LOG_PATH)
        assert eids, (
            f"No '[DECIDE] Villager #...' lines in {GAME_LOG_PATH} after "
            f"{self.RUN_SECONDS}s. Agent-host may not have adopted any villager."
        )

    def test_inventory_capacity_is_finite(self, game):
        """Regression: humanoids register with inventory_capacity=inf, and
        the woodcutter's `full = not can_accept(...)` never flips to True,
        so DEPOSIT is unreachable. Either the C++ default must change for
        civilian villagers, or the behavior must gate on collect_goal.

        Either fix is acceptable as long as the elog's observed capacity
        is finite OR the behavior deposits anyway. This test checks the
        *observable consequence* — if cap was inf in the elog AND no
        StoreItem was ever issued, the cycle is unreachable."""
        summaries = self._villager_summaries()
        if not summaries:
            pytest.skip("no villagers observed (upstream failure)")

        inf_caps = [eid for eid, s in summaries if s["inf_cap"]]
        stored   = [eid for eid, s in summaries if s["store_events"]]

        if inf_caps and not stored:
            pytest.fail(
                f"Villagers {inf_caps} report inventory_capacity=inf and NONE "
                f"of {[e for e,_ in summaries]} ever reached StoreItem in "
                f"{self.RUN_SECONDS}s. DEPOSIT state is unreachable."
            )

    def test_villagers_actually_chopped(self, game):
        """At least one villager must have held ≥ 1 log at some point.
        If every villager sits at logs=0 for the whole run, they never
        reached a tree at all — a different bug from the fall-in-hole stall,
        but equally fatal to the gather cycle."""
        summaries = self._villager_summaries()
        if not summaries:
            pytest.skip("no villagers observed")

        any_chopped = any(s["max_logs"] > 0 for _, s in summaries)
        per_v = [(e, s["max_logs"]) for e, s in summaries]
        assert any_chopped, (
            f"No villager ever reached logs > 0 after {self.RUN_SECONDS}s. "
            f"Peak logs per villager: {per_v}"
        )

    def test_no_fall_in_hole_stall(self, game):
        """The stall signature we reproduced in `make game`:

            final work-sample has y ≤ 2           (fell below spawn)
            AND horizontal d < 4                   (stopped near anchor)
            AND anchor.y - self.y ≥ 3              (anchor vertically out of reach)
            AND last 3 work samples are identical  (no progress)

        ANY villager matching this pattern fails the test — the bug is
        "villager wedges with unreachable anchor above", not "how many
        wedge at once". Being strict here turns this into the regression
        fence the existing one-cycle test never was."""
        summaries = self._villager_summaries()
        if not summaries:
            pytest.skip("no villagers observed")

        wedged = []
        for eid, s in summaries:
            samples = s["work_samples"]
            if len(samples) < 3:
                continue  # nothing to judge
            tail = samples[-3:]
            # Same anchor, same logs, same self.xyz across the trailing three.
            sig = {(t[1], t[2], t[3], round(t[4], 1), round(t[5], 1),
                    round(t[6], 1), t[8]) for t in tail}
            if len(sig) != 1:
                continue  # still moving / still chopping
            _ts, ax, ay, az, sx, sy, sz, d, logs = tail[-1]
            vertical_gap = ay - sy
            if sy <= 2.0 and d < 4.0 and vertical_gap >= 3.0:
                wedged.append({
                    "eid": eid,
                    "pos": (sx, sy, sz), "anchor": (ax, ay, az),
                    "d": d, "logs": logs, "vertical_gap": vertical_gap,
                })

        assert not wedged, (
            f"{len(wedged)} villager(s) wedged under an unreachable anchor "
            f"after {self.RUN_SECONDS}s — the fall-in-hole stall. "
            f"Either pathfinding must reject unreachable anchors, or the "
            f"executor must pick a reachable alternative when the top one "
            f"is vertically blocked.\n  {wedged}"
        )

    def test_logs_increased_over_run(self, game):
        """Over the full run, the sum of (max_logs) across villagers must
        grow — otherwise the chop loop is globally stuck. This is a coarser
        check than test_no_fall_in_hole_stall: it catches stalls where
        vertical_gap doesn't trigger (e.g. all villagers stuck horizontally
        blocked by something unrelated)."""
        summaries = self._villager_summaries()
        if not summaries:
            pytest.skip("no villagers observed")
        total_peak = sum(s["max_logs"] for _, s in summaries)
        assert total_peak > 0, (
            f"Across {len(summaries)} villagers, peak logs-held is 0 "
            f"after {self.RUN_SECONDS}s. The chop inner loop never "
            f"produced a single log."
        )

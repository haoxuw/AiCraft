#!/usr/bin/env python3
# Turn a formatSummary() dump (/tmp/solarium_perf_{client,server}_*.txt) into
# a per-phase breakdown table showing avg-ms, % of the enclosing total, and
# p99. Driven by `make perf_fps` / `make perf_server`.

import os
import sys


CLIENT = {
    "title":  "CLIENT FRAME BREAKDOWN — what the main thread spends each frame on",
    "total":  "client.frame.total_ms",
    "prefix": "client.phase.",
    "suffix": "",
    "order": [
        "net", "chunks", "agent", "events", "sim",
        "gpuWait",
        "world", "ents", "fx3d", "inv3d", "hud", "panels",
        "present",
    ],
    "cat": {
        "net":     "pre-render CPU (drain net queue)",
        "chunks":  "pre-render CPU (apply chunks + GPU upload)",
        "agent":   "pre-render CPU (AgentClient orchestration)",
        "events":  "pre-render CPU (entity delta scans)",
        "sim":     "pre-render CPU (input + local ticks)",
        "gpuWait": "GPU idle wait (vkWaitForFences + acquire)",
        "world":   "render CPU (record terrain draws)",
        "ents":    "render CPU (record entity draws)",
        "fx3d":    "render CPU (record effect draws)",
        "inv3d":   "render CPU (inventory 3D previews)",
        "hud":     "render CPU (HUD + hotbar + inventory)",
        "panels":  "render CPU (debug / tuning / dialog)",
        "present": "GPU submit (endFrame)",
    },
    "counter_fps": ("client.frames.total", "client.frames.slow_16ms",
                    "client.frames.dropped_33ms"),
}

PATH_EXEC = {
    "title":  "PATH EXECUTOR — per-tick AI nav cost (PathExecutor::tick phases, all entities)",
    "total":  "path.executor.tick_ms",
    "prefix": "path.executor.",
    "suffix": "_ms",
    "order": [
        "auto_close_doors", "detect_stall", "door_scan_probe",
        "pop_reached", "front_door_handshake", "slide_obstacle",
    ],
    "cat": {
        "auto_close_doors":     "auto-close passedDoors (politeness + cooldown gate)",
        "detect_stall":         "stall counter (kStallTicks pop / kDoorScanTicks probe)",
        "door_scan_probe":      "stall-triggered hidden-door BFS + Interact emit",
        "pop_reached":          "segment-crossing pop while-loop (was O(N) scan)",
        "front_door_handshake": "approach + Interact handshake on closed front cell",
        "slide_obstacle":       "wall-slide deflection of Move target",
    },
}

SERVER_FRAME = {
    "title":  "SERVER FRAME BREAKDOWN — one outer loop iteration (~1 ms sleep between)",
    "total":  "server.frame.total_ms",
    "prefix": "server.frame.",
    "suffix": "_ms",
    "order": [
        "accept", "receive", "sendchunks", "prune",
        "tick", "broadcast", "lan", "statuslog",
    ],
    "cat": {
        "accept":     "accept new TCP clients",
        "receive":    "read queued client messages",
        "sendchunks": "stream chunks to joining clients",
        "prune":      "GC dead entities / stale clients",
        "tick":       "world simulation tick (see breakdown below)",
        "broadcast":  "send S_ENTITY/S_CHUNK/... to all clients",
        "lan":        "LAN broadcast announcement",
        "statuslog":  "periodic status log",
    },
}

SERVER_TICK = {
    "title":  "SERVER TICK BREAKDOWN — inside the `tick` row above, per simulation step",
    "total":  "server.tick.total_ms",
    "prefix": "server.tick.",
    "suffix": "_ms",
    "order": [
        "resolve", "nav", "physics", "yaw",
        "blocks", "hpregen", "structregen", "structfeat", "stuck",
    ],
    "cat": {
        "resolve":    "ActionProposal → validate → apply",
        "nav":        "server-side greedy steer (player goals)",
        "physics":    "moveAndCollide for every entity",
        "yaw":        "yaw smoothing",
        "blocks":     "active block ticks (fuses, etc.)",
        "hpregen":    "HP regen pass",
        "structregen":"structure respawn",
        "structfeat": "structure feature evaluation",
        "stuck":      "stuck-detection pass",
    },
}


def parse(path):
    """Read the dump file. Return { metric_name: {count, avg, min, max, p50,
    p95, p99, p999} } for every row in the histogram table."""
    out = {}
    with open(path) as f:
        for line in f:
            parts = line.split()
            # histogram rows look like:
            #   "metric.name  count  avg  min  max  p50  p95  p99  p99.9"
            # identifier must have at least two dots; skip header/counters.
            if len(parts) < 9 or parts[0].count(".") < 2:
                continue
            try:
                nums = [float(x) for x in parts[1:9]]
            except ValueError:
                continue
            out[parts[0]] = {
                "count": int(nums[0]), "avg": nums[1],
                "min":   nums[2],      "max": nums[3],
                "p50":   nums[4],      "p95": nums[5],
                "p99":   nums[6],      "p999": nums[7],
            }
    return out


def format_section(metrics, conf):
    total_key = conf["total"]
    if total_key not in metrics:
        return f"── {conf['title']} ──\n(no samples for {total_key})\n"
    total = metrics[total_key]

    lines = []
    lines.append(f"── {conf['title']} ──")
    lines.append(
        f"samples={total['count']}  avg={total['avg']:.2f}ms  "
        f"p50={total['p50']:.2f}  p95={total['p95']:.2f}  "
        f"p99={total['p99']:.2f}  max={total['max']:.2f}"
    )
    if total_key == "client.frame.total_ms" and total["avg"] > 0:
        lines.append(f"→ avg FPS = {1000.0 / total['avg']:.1f}")
    lines.append("")
    lines.append(
        f"{'phase':<12}{'avg ms':>9}{'%':>7}{'p99 ms':>9}   category"
    )
    lines.append("-" * 78)

    summed = 0.0
    for name in conf["order"]:
        key = conf["prefix"] + name + conf["suffix"]
        m = metrics.get(key)
        if not m:
            continue
        pct = 100.0 * m["avg"] / total["avg"] if total["avg"] else 0.0
        summed += m["avg"]
        lines.append(
            f"{name:<12}{m['avg']:>9.2f}{pct:>6.1f}%{m['p99']:>9.2f}   "
            f"{conf['cat'].get(name, '')}"
        )

    lines.append("-" * 78)
    sum_pct = 100.0 * summed / total["avg"] if total["avg"] else 0.0
    delta = total["avg"] - summed
    delta_pct = 100.0 * delta / total["avg"] if total["avg"] else 0.0
    lines.append(f"{'sum':<12}{summed:>9.2f}{sum_pct:>6.1f}%")
    lines.append(f"{'unaccounted':<12}{delta:>9.2f}{delta_pct:>6.1f}%   "
                 f"(setup + probe jitter)")
    return "\n".join(lines) + "\n"


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ("client", "server"):
        print("usage: perf_report.py {client|server} <dump.txt>", file=sys.stderr)
        sys.exit(2)
    mode, path = sys.argv[1], sys.argv[2]
    if not os.path.exists(path):
        print(f"no dump file: {path}", file=sys.stderr)
        sys.exit(1)

    metrics = parse(path)
    if mode == "client":
        print(format_section(metrics, CLIENT))
        # PathExecutor is a client-side concern (Rule 4: AI on agent clients).
        # Only print if any path.executor.* samples exist this run — otherwise
        # we'd dump an empty section every time someone ran without AI ticks.
        if any(k.startswith("path.executor.") for k in metrics):
            print(format_section(metrics, PATH_EXEC))
    else:
        print(format_section(metrics, SERVER_FRAME))
        print(format_section(metrics, SERVER_TICK))


if __name__ == "__main__":
    main()

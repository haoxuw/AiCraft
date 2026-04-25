#!/usr/bin/env python3
"""Generate src/python/protocol_constants.py from the canonical C++ headers.

Single source of truth for wire-format constants is C++ (net_protocol.h +
logic/action.h). Python tools that talk the protocol (action_proxy.py,
tests/behavior_scenario_validation/protocol.py) import from the generated
file so drift between languages is impossible.

Re-run this script after any change to:
    src/platform/net/net_protocol.h   (PROTOCOL_VERSION, MsgType)
    src/platform/logic/action.h        (ActionProposal::Type)

and commit the resulting src/python/protocol_constants.py alongside the
header change.
"""

import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NET_HDR    = os.path.join(REPO_ROOT, "src/platform/net/net_protocol.h")
ACTION_HDR = os.path.join(REPO_ROOT, "src/platform/logic/action.h")
OUT_PATH   = os.path.join(REPO_ROOT, "src/python/protocol_constants.py")


def _read(path: str) -> str:
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def _strip_line_comment(line: str) -> str:
    # Good enough for our headers — no // inside string literals.
    i = line.find("//")
    return line if i == -1 else line[:i]


def parse_protocol_version(src: str) -> int:
    m = re.search(r"PROTOCOL_VERSION\s*=\s*(\d+)\s*;", src)
    if not m:
        sys.exit(f"FATAL: PROTOCOL_VERSION not found in {NET_HDR}")
    return int(m.group(1))


def parse_enum_block(src: str, enum_decl_re: str) -> list[tuple[str, int]]:
    """Return [(name, value)] for one enum body. Handles hex + implicit +1
    increments. Any entry preceded by an `= expr` resets the counter."""
    m = re.search(enum_decl_re + r"\s*\{([^}]*)\}", src, re.DOTALL)
    if not m:
        sys.exit(f"FATAL: enum matching /{enum_decl_re}/ not found")
    # Strip line comments FIRST (inline // comments contain commas that would
    # otherwise confuse the split below) before flattening to a comma stream.
    body = "\n".join(_strip_line_comment(l) for l in m.group(1).splitlines())

    out: list[tuple[str, int]] = []
    next_val = 0
    for raw in body.split(","):
        line = raw.strip()
        if not line:
            continue
        eq = re.match(r"([A-Za-z_]\w*)\s*=\s*(0x[0-9A-Fa-f]+|\d+)", line)
        if eq:
            name = eq.group(1)
            val  = int(eq.group(2), 0)
            out.append((name, val))
            next_val = val + 1
            continue
        bare = re.match(r"([A-Za-z_]\w*)\s*$", line)
        if bare:
            out.append((bare.group(1), next_val))
            next_val += 1
            continue
        # Skip malformed / multi-line entries loudly so we notice drift.
        sys.exit(f"FATAL: can't parse enum entry: {line!r}")
    return out


def format_constants(version: int,
                     msg_types: list[tuple[str, int]],
                     action_types: list[tuple[str, int]]) -> str:
    lines: list[str] = []
    lines.append('"""Generated from C++ headers — do NOT edit by hand.\n')
    lines.append("Regenerate with:  python3 tools/gen_protocol_py.py\n")
    lines.append("Sources:\n")
    lines.append("    src/platform/net/net_protocol.h  (PROTOCOL_VERSION, MsgType)\n")
    lines.append("    src/platform/logic/action.h       (ActionProposal::Type)\n")
    lines.append('"""\n\n')
    lines.append(f"PROTOCOL_VERSION = {version}\n\n")
    lines.append("ENTITY_NONE      = 0\n\n")
    lines.append("# ── MsgType (net_protocol.h) ────────────────────────────────────────\n")
    name_w = max(len(n) for n, _ in msg_types) if msg_types else 0
    for name, val in msg_types:
        lines.append(f"{name:<{name_w}} = 0x{val:04X}\n")
    lines.append("\n")
    lines.append("# ── ActionProposal::Type (action.h) ─────────────────────────────────\n")
    name_w2 = max(len(n) for n, _ in action_types) if action_types else 0
    for name, val in action_types:
        lines.append(f"TYPE_{name.upper():<{name_w2}} = {val}\n")
    return "".join(lines)


def main() -> int:
    net_src    = _read(NET_HDR)
    action_src = _read(ACTION_HDR)

    version      = parse_protocol_version(net_src)
    msg_types    = parse_enum_block(net_src,    r"enum\s+MsgType\s*:\s*uint32_t")
    action_types = parse_enum_block(action_src, r"enum\s+Type")

    output = format_constants(version, msg_types, action_types)
    with open(OUT_PATH, "w", encoding="utf-8") as f:
        f.write(output)
    print(f"Wrote {OUT_PATH}")
    print(f"  PROTOCOL_VERSION = {version}")
    print(f"  MsgType entries:   {len(msg_types)}")
    print(f"  Action types:      {len(action_types)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

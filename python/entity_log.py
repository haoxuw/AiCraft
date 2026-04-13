"""entity_log.py — Per-entity behavior log files.

Each entity writes to /tmp/modcraft_entity_<id>.log. Useful for debugging a
specific creature's decisions without drowning the shared game log in spam.

Usage from any Python module:
    from entity_log import log as elog
    elog(entity.id, "no chest, dropping %d logs" % logs)

Files are truncated on first write per-process (fresh run = fresh log) and
line-buffered so a tail -f shows events live.
"""

import os
import time

_LOG_DIR = "/tmp"
_files: dict[int, object] = {}


def log(entity_id: int, msg: str) -> None:
    f = _files.get(entity_id)
    if f is None:
        path = os.path.join(_LOG_DIR, f"modcraft_entity_{entity_id}.log")
        # Truncate on first use this process; line-buffered so tail works.
        f = open(path, "w", buffering=1)
        _files[entity_id] = f
    ts = time.strftime("%H:%M:%S")
    f.write(f"[{ts}] {msg}\n")

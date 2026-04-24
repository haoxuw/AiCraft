"""entity_log.py — Per-entity behavior log files.

Each entity writes to /tmp/civcraft_entity_<id>.log. Useful for debugging a
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
        path = os.path.join(_LOG_DIR, f"civcraft_entity_{entity_id}.log")
        # Open O_APPEND so our writes share the kernel's atomic-append offset
        # with the C++ side (entity_log.h), which also writes to this path.
        # The C++ side owns truncation — it unlinks the file on first use per
        # process, and its navigator trace fires before Python decide() runs.
        f = open(path, "a", buffering=1)
        _files[entity_id] = f
    ts = time.strftime("%H:%M:%S")
    f.write(f"[{ts}] {msg}\n")

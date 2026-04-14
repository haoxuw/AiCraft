#!/bin/bash
# Re-download the full music library (~12 GB, ~2100 tracks).
# Safe to re-run — both harvesters skip files already on disk.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$here/tracks"
echo "[redownload] Incompetech (Kevin MacLeod, CC-BY 4.0) — ~1441 tracks"
python3 "$here/harvest_incompetech.py"
echo "[redownload] OpenGameArt (CC0 / CC-BY) — ~700 tracks"
python3 "$here/harvest_oga.py"
count=$(find "$here/tracks" -type f \( -name '*.mp3' -o -name '*.ogg' -o -name '*.wav' -o -name '*.flac' \) | wc -l)
echo "[redownload] done: $count tracks in $here/tracks/"

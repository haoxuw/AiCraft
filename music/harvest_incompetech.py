"""Harvest Kevin MacLeod's Incompetech catalog (all CC-BY 4.0).

~1441 tracks via https://incompetech.com/music/royalty-free/pieces.json.
Writes directly into music/tracks/ with filenames prefixed `inc_`.
Resumable: skips files already on disk.
"""
from __future__ import annotations
import os, re, csv, json, urllib.parse
from concurrent.futures import ThreadPoolExecutor, as_completed
from urllib.request import Request, urlopen

BASE = "https://incompetech.com"
INDEX = BASE + "/music/royalty-free/pieces.json"
UA = "Mozilla/5.0 (harvest-for-game-dev)"
ROOT = os.path.dirname(os.path.abspath(__file__))
TRACKS = os.path.join(ROOT, "tracks")
MANIFEST = os.path.join(ROOT, "manifest_incompetech.csv")
MAX_FILE_MB = 20

def safe(s): return re.sub(r"[^A-Za-z0-9._-]", "_", s)

def fetch(url, binary=False, timeout=60):
    req = Request(url, headers={"User-Agent": UA})
    with urlopen(req, timeout=timeout) as r:
        data = r.read()
        return data if binary else data.decode("utf-8", errors="ignore")

def download(url, dest):
    if os.path.exists(dest) and os.path.getsize(dest) > 1024:
        return True, os.path.getsize(dest)
    try:
        data = fetch(url, binary=True, timeout=120)
        if len(data) > MAX_FILE_MB * 1024 * 1024:
            return False, f"too big ({len(data)//1024//1024}MB)"
        with open(dest, "wb") as f: f.write(data)
        return True, len(data)
    except Exception as e:
        return False, str(e)

def main():
    os.makedirs(TRACKS, exist_ok=True)
    pieces = json.loads(fetch(INDEX))
    print(f"[incompetech] {len(pieces)} tracks in catalog")
    rows = []; done = [0]
    with ThreadPoolExecutor(max_workers=10) as ex:
        futs = {}
        for p in pieces:
            title = p["title"].strip().replace("\r","").replace("\n"," ")
            fname = p["filename"].strip()
            url = BASE + "/music/royalty-free/mp3-royaltyfree/" + urllib.parse.quote(fname)
            dest = os.path.join(TRACKS, "inc_" + safe(fname))
            futs[ex.submit(download, url, dest)] = (p, title, dest)
        for f in as_completed(futs):
            p, title, dest = futs[f]
            ok, sz = f.result(); done[0] += 1
            if ok:
                rows.append(dict(
                    file=os.path.relpath(dest, ROOT),
                    title=title, author="Kevin MacLeod", licenses="CC-BY 4.0",
                    genre=p.get("genre",""), bpm=p.get("bpm",""), length=p.get("length",""),
                    source_page="https://incompetech.com/music/royalty-free/",
                    size_bytes=sz,
                ))
            if done[0] % 50 == 0:
                print(f"[incompetech] {done[0]}/{len(pieces)} ok={len(rows)}")
    with open(MANIFEST, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["file","title","author","licenses","genre","bpm","length","source_page","size_bytes"])
        w.writeheader(); w.writerows(rows)
    print(f"[incompetech] done: {len(rows)} tracks, manifest={MANIFEST}")

if __name__ == "__main__":
    main()

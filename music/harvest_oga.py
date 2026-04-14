"""Harvest royalty-free music from OpenGameArt.

Filters by commercial-safe license (CC0, CC-BY, OGA-BY, Public Domain);
skips CC-BY-SA-only and GPL-only. Writes to music/tracks/ with
filenames prefixed `oga_<slug>__<file>`. Resumable.
"""
from __future__ import annotations
import os, re, csv, urllib.parse
from concurrent.futures import ThreadPoolExecutor, as_completed
from urllib.request import Request, urlopen

BASE = "https://opengameart.org"
UA = "Mozilla/5.0 (harvest-for-game-dev)"
ROOT = os.path.dirname(os.path.abspath(__file__))
TRACKS = os.path.join(ROOT, "tracks")
MANIFEST = os.path.join(ROOT, "manifest_oga.csv")
MAX_PER_QUERY = 200
MAX_TOTAL = 1200
MAX_FILE_MB = 25
SAFE = {"CC0", "CC-BY 3.0", "CC-BY 4.0", "OGA-BY 3.0", "OGA-BY 3.0+", "Public Domain"}

QUERIES = [
    "synthwave","electronic","electronic loop","electronic music",
    "chiptune","chip","retro","retro wave","arcade","8bit","8-bit","16-bit",
    "action","fast","techno","house","trance","electro",
    "cyber","cyberpunk","futuristic","space",
    "racing","driving","menu","title","intro","outro",
    "level","stage","world","zone","map",
    "boss","final","epic","combat","battle","fight",
    "dance","edm","drum and bass","dnb","bass",
    "ambient","atmospheric","mystery","tension",
    "adventure","exploration","overworld","dungeon",
    "town","village","shop","rpg",
    "pixel","platformer","shmup","shooter",
    "loop","bgm","theme","victory","defeat","game over",
    "sad","happy","funny","quirky","catchy",
    "rock","metal","punk","jazz","orchestral",
    "piano","guitar","synth","bass line",
]

def safe(s): return re.sub(r"[^A-Za-z0-9._-]", "_", s)

def fetch(url, binary=False, timeout=30):
    req = Request(url, headers={"User-Agent": UA})
    with urlopen(req, timeout=timeout) as r:
        d = r.read()
        return d if binary else d.decode("utf-8", errors="ignore")

def search(q, art_type=12):
    url = f"{BASE}/art-search?keys={urllib.parse.quote_plus(q)}&field_art_type_tid%5B%5D={art_type}"
    html = fetch(url)
    return sorted(set(re.findall(r'href="(/content/[a-z0-9-]+)"', html)))

def parse_page(slug):
    html = fetch(BASE + slug)
    tm = re.search(r"<title>([^<]+) \| OpenGameArt", html)
    title = tm.group(1).strip() if tm else slug
    am = re.search(r"username'><a href=\"/users/[^\"]+\">([^<]+)</a>", html)
    author = am.group(1).strip() if am else "?"
    lic = set(re.findall(r"(CC0(?:\s*1\.0)?|CC-BY\s*\d\.\d|CC-BY-SA\s*\d\.\d|OGA-BY\s*\d\.\d\+?|GPL\s*\d?\.?\d?|Public Domain)", html))
    norm = set()
    for l in lic:
        l2 = re.sub(r"\s+"," ",l).strip()
        norm.add("CC0" if l2.startswith("CC0") else l2)
    audio = re.findall(r'href="(https?://opengameart\.org/sites/default/files/[^"]+\.(?:mp3|ogg|wav|flac))"', html)
    return dict(slug=slug, title=title, author=author, licenses=norm, audio=audio)

def is_safe(info):
    if not info["audio"]: return False
    return bool(info["licenses"] & SAFE)

def download(url, dest):
    if os.path.exists(dest) and os.path.getsize(dest) > 1024:
        return True, os.path.getsize(dest)
    try:
        d = fetch(url, binary=True, timeout=60)
        if len(d) > MAX_FILE_MB * 1024 * 1024: return False, "too big"
        with open(dest, "wb") as f: f.write(d)
        return True, len(d)
    except Exception as e:
        return False, str(e)

def main():
    os.makedirs(TRACKS, exist_ok=True)
    slugs = set()
    for q in QUERIES:
        try:
            slugs.update(search(q)[:MAX_PER_QUERY])
        except Exception as e:
            print(f"[oga] search {q!r}: {e}")
    print(f"[oga] {len(slugs)} content pages")

    pages = []
    with ThreadPoolExecutor(max_workers=8) as ex:
        futs = {ex.submit(parse_page, s): s for s in slugs}
        for f in as_completed(futs):
            try:
                info = f.result()
                if is_safe(info): pages.append(info)
            except Exception: pass
    print(f"[oga] {len(pages)} safe-licensed pages")

    jobs = []
    for info in pages:
        slug_tag = safe(info["slug"].strip("/").replace("content/", ""))
        for u in info["audio"]:
            fname = os.path.basename(urllib.parse.unquote(u))
            dest = os.path.join(TRACKS, f"oga_{slug_tag}__{safe(fname)}")
            jobs.append((info, u, dest))
            if len(jobs) >= MAX_TOTAL: break
        if len(jobs) >= MAX_TOTAL: break

    rows = []; done = [0]
    with ThreadPoolExecutor(max_workers=8) as ex:
        futs = {ex.submit(download, u, d): (i,u,d) for (i,u,d) in jobs}
        for f in as_completed(futs):
            info, url, dest = futs[f]
            ok, sz = f.result(); done[0] += 1
            if ok:
                rows.append(dict(
                    file=os.path.relpath(dest, ROOT),
                    title=info["title"], author=info["author"],
                    licenses="; ".join(sorted(info["licenses"])),
                    source_page=BASE + info["slug"],
                    size_bytes=sz,
                ))
            if done[0] % 50 == 0:
                print(f"[oga] {done[0]}/{len(jobs)} ok={len(rows)}")

    with open(MANIFEST, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["file","title","author","licenses","source_page","size_bytes"])
        w.writeheader(); w.writerows(rows)
    print(f"[oga] done: {len(rows)} tracks, manifest={MANIFEST}")

if __name__ == "__main__":
    main()

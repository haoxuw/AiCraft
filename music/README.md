# music/ — royalty-free music library

Bulk royalty-free music for LifeCraft (and anything else in this repo).
**Not checked in** — tracks (`~12 GB`, ~2,100 files) live under
`music/tracks/`, which is gitignored. Manifests, harvesters, and the
ratings file *are* checked in, so a fresh clone can re-download the full
library in one command.

## Layout

```
music/
  tracks/                  (gitignored, ~12 GB)
    inc_<name>.mp3         from incompetech.com   (CC-BY 4.0, Kevin MacLeod)
    oga_<slug>__<name>.*   from opengameart.org   (CC0 / CC-BY / OGA-BY)
  manifest_incompetech.csv  # per-track: title, author, license, bpm, genre, length
  manifest_oga.csv          # per-track: title, author, license(s), source URL
  harvest_incompetech.py    # re-run to refresh Incompetech catalog
  harvest_oga.py            # re-run to refresh OpenGameArt downloads
  redownload.sh             # one-command fetch, used by `make music`
  jukebox.py                # terminal curator (see below)
  ratings.tsv               # your per-track like/dislike decisions
```

## Re-downloading tracks on a fresh clone

```
make download_music  # from repo root — runs music/redownload.sh
```

Both harvesters are resumable: already-downloaded files are skipped, so
it's safe to re-run after a partial fetch.

## Curating with the jukebox

```
make jukebox         # terminal player, walks tracks/ one by one
                     # (depends on download_music — ensures tracks/ is populated)
```

Keys:

| Key       | Action                                             |
|-----------|----------------------------------------------------|
| `.`       | next — advance to the next undecided track        |
| `,`       | back — previous in history (lets you change a vote) |
| `+`       | like the current track, then advance               |
| `-`       | dislike the current track, then advance            |
| `space`   | pause / resume                                     |
| `q`       | quit (state is always saved)                        |

Once a track is rated, it's removed from the auto-queue. `.` only stops
on undecided tracks, but `,` walks history regardless so you can revise
a past decision. The jukebox resumes on the first undecided track the
next time you launch it.

## Ratings file format — `ratings.tsv`

One track per line, tab-separated. Readable as a big list:

```
# music ratings — 42 liked / 17 disliked / 2080 undecided / 2139 total
# format: <rating>\t<filename>   rating is +, -, or ?
+	inc_Action_Strike.mp3
-	inc_Aaronic_Priesthood.mp3
?	inc_Adventure_Meme.mp3
...
```

Safe to edit by hand — the jukebox reloads it at startup.

## Licenses

- **Incompetech**: CC-BY 4.0, all by Kevin MacLeod. Attribution required;
  one credit line covers the whole author.
- **OpenGameArt**: mix of CC0 (no attribution) and CC-BY (attribution
  required). See `manifest_oga.csv` column `licenses` per-track.
- CC-BY-SA-only and GPL-only tracks are **filtered out** by the harvester.

See `README_sources.md` for per-source details.

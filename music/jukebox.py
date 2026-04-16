"""Terminal music curator.

Walks every track under music/tracks/ and lets you sort each one into a
category while it plays. Every track starts as `new`; once assigned a
category, `.` will skip past it.

Keys:
    1 fight        5 peace
    2 tension      6 emotion
    3 boss         7 inspire
    4 menu         8 discarded
    .   next — advance to the next `new` (uncategorized) track
    ,   back — previous track in history (lets you change a past category)
    space  pause/resume
    q   quit

Ratings persist to music/ratings.tsv — one line per track:
    <category>\t<filename>
where <category> is one of: new, fight, tension, boss, menu, peace,
emotion, inspire, discarded. The file is readable and safe to edit by
hand — the jukebox reloads it at startup.

On assignment, the audio file is moved into music/tracks/<category>/ so
the filesystem mirrors the tsv. `new` (uncategorized) tracks live at
music/tracks/ root. Track discovery is recursive so previously-sorted
files are still picked up. At startup, any file whose on-disk location
disagrees with ratings.tsv is relocated to the correct folder.
"""
from __future__ import annotations
import os, sys, termios, tty, select, subprocess, signal, time

ROOT    = os.path.dirname(os.path.abspath(__file__))
TRACKS  = os.path.join(ROOT, "tracks")
RATINGS = os.path.join(ROOT, "ratings.tsv")
EXTS    = (".mp3", ".ogg", ".wav", ".flac")
PLAYER  = ["gst-play-1.0", "--quiet"]

# Order matters: the digit key = index into this list (1-based, digit 0 unused).
CATEGORIES = ["new", "fight", "tension", "boss", "menu", "peace", "emotion", "inspire", "discarded"]
DEFAULT    = "new"
KEY_TO_CAT = {str(i): CATEGORIES[i] for i in range(1, len(CATEGORIES))}  # "1"→fight … "8"→discarded


def discover_tracks():
    # Recursive: files may live at TRACKS/ (new) or TRACKS/<category>/.
    # Filenames are assumed unique across subfolders.
    out = []
    for dirpath, _, files in os.walk(TRACKS):
        for f in files:
            if f.lower().endswith(EXTS):
                out.append(f)
    return sorted(out)


def find_track(name):
    """Return absolute path for `name` wherever it currently lives under TRACKS."""
    root = os.path.join(TRACKS, name)
    if os.path.isfile(root):
        return root
    for c in CATEGORIES:
        p = os.path.join(TRACKS, c, name)
        if os.path.isfile(p):
            return p
    return None


def move_to_category(name, cat):
    """Relocate `name` into TRACKS/<cat>/ (or TRACKS/ when cat == DEFAULT)."""
    src = find_track(name)
    if src is None:
        return
    dst_dir = TRACKS if cat == DEFAULT else os.path.join(TRACKS, cat)
    os.makedirs(dst_dir, exist_ok=True)
    dst = os.path.join(dst_dir, name)
    if os.path.abspath(src) != os.path.abspath(dst):
        os.replace(src, dst)


def load_ratings():
    r = {}
    if os.path.exists(RATINGS):
        with open(RATINGS) as f:
            for line in f:
                line = line.rstrip("\n")
                if not line or line.startswith("#") or "\t" not in line: continue
                cat, name = line.split("\t", 1)
                cat = cat.strip()
                if cat in CATEGORIES: r[name] = cat
    return r


def save_ratings(ratings, all_tracks):
    counts = {c: 0 for c in CATEGORIES}
    for n in all_tracks:
        counts[ratings.get(n, DEFAULT)] += 1
    header = (
        "# music ratings — "
        + " / ".join(f"{counts[c]} {c}" for c in CATEGORIES)
        + f" / {len(all_tracks)} total\n"
        + "# format: <category>\\t<filename>\n"
        + "# categories: " + ", ".join(CATEGORIES) + "\n"
    )
    tmp = RATINGS + ".tmp"
    with open(tmp, "w") as f:
        f.write(header)
        for n in all_tracks:
            f.write(f"{ratings.get(n, DEFAULT)}\t{n}\n")
    os.replace(tmp, RATINGS)  # atomic


class Player:
    def __init__(self):
        self.proc = None
        self.path = None
        self.paused = False

    def play(self, path):
        self.stop()
        self.path = path
        self.paused = False
        self.proc = subprocess.Popen(
            PLAYER + [path],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            stdin=subprocess.DEVNULL,
            preexec_fn=os.setsid,
        )

    def stop(self):
        if self.proc and self.proc.poll() is None:
            try:
                os.killpg(os.getpgid(self.proc.pid), signal.SIGTERM)
                self.proc.wait(timeout=1)
            except Exception:
                try: os.killpg(os.getpgid(self.proc.pid), signal.SIGKILL)
                except Exception: pass
        self.proc = None

    def toggle_pause(self):
        if not self.proc or self.proc.poll() is not None: return
        sig = signal.SIGCONT if self.paused else signal.SIGSTOP
        try:
            os.killpg(os.getpgid(self.proc.pid), sig)
            self.paused = not self.paused
        except Exception:
            pass

    def finished(self):
        return self.proc is None or self.proc.poll() is not None


def raw_mode(fd):
    class Ctx:
        def __enter__(s):
            s.old = termios.tcgetattr(fd)
            tty.setcbreak(fd)
            return s
        def __exit__(s, *a):
            termios.tcsetattr(fd, termios.TCSADRAIN, s.old)
    return Ctx()


def readkey(fd, timeout=0.2):
    r, _, _ = select.select([fd], [], [], timeout)
    if r: return os.read(fd, 1).decode("utf-8", errors="ignore")
    return None


def next_new(tracks, ratings, from_idx):
    n = len(tracks)
    for step in range(1, n + 1):
        j = (from_idx + step) % n
        if ratings.get(tracks[j], DEFAULT) == DEFAULT:
            return j
    return -1


def render(tracks, ratings, idx, paused):
    name = tracks[idx]
    cur  = ratings.get(name, DEFAULT)
    counts = {c: 0 for c in CATEGORIES}
    for n in tracks: counts[ratings.get(n, DEFAULT)] += 1
    display = name if len(name) <= 72 else name[:69] + "..."
    flag = "[PAUSED] " if paused else ""

    # Menu: two rows of four categories each (digits 1-8).
    menu_line_1 = "  " + "  ".join(f"{i+1}){CATEGORIES[i+1]:<9}" for i in range(0, 4))
    menu_line_2 = "  " + "  ".join(f"{i+1}){CATEGORIES[i+1]:<9}" for i in range(4, 8))
    totals = "  ".join(f"{counts[c]} {c}" for c in CATEGORIES)

    return "\r\x1b[K\x1b[6A\x1b[J" + "\n".join([
        f"{flag}[{idx+1}/{len(tracks)}] {cur:<9}  {display}",
        menu_line_1,
        menu_line_2,
        f"  {totals}",
        "  keys: 1-8 assign category   . next-new   , back   space pause   q quit",
        "",
    ])


def main():
    if not os.path.isdir(TRACKS):
        print(f"error: {TRACKS} does not exist. Run `make download_music` first.")
        sys.exit(1)
    tracks = discover_tracks()
    if not tracks:
        print(f"error: no audio files in {TRACKS}.")
        sys.exit(1)

    ratings = load_ratings()
    # Reconcile on-disk layout with ratings.tsv (retroactive relocation for
    # tracks categorized before per-category subfolders existed).
    for name in tracks:
        move_to_category(name, ratings.get(name, DEFAULT))
    start = next((i for i, t in enumerate(tracks) if ratings.get(t, DEFAULT) == DEFAULT), 0)
    idx = start
    history = [idx]
    player = Player()
    player.play(find_track(tracks[idx]))

    fd = sys.stdin.fileno()
    try:
        with raw_mode(fd):
            sys.stdout.write("\n\n\n\n\n\n")  # reserve render area
            while True:
                sys.stdout.write(render(tracks, ratings, idx, player.paused))
                sys.stdout.flush()

                k = readkey(fd, 0.2)

                if k is None and player.finished():
                    nxt = next_new(tracks, ratings, idx)
                    if nxt < 0:
                        sys.stdout.write("\n\nAll tracks categorized.\n")
                        break
                    idx = nxt; history.append(idx)
                    player.play(find_track(tracks[idx]))
                    continue
                if k is None: continue

                if k == "q" or k == "\x03":
                    break
                elif k == ".":
                    nxt = next_new(tracks, ratings, idx)
                    if nxt < 0:
                        sys.stdout.write("\n\nAll tracks categorized.\n")
                        break
                    idx = nxt; history.append(idx)
                    player.play(find_track(tracks[idx]))
                elif k == ",":
                    if len(history) > 1:
                        history.pop()
                        idx = history[-1]
                        player.play(find_track(tracks[idx]))
                elif k in KEY_TO_CAT:
                    cat = KEY_TO_CAT[k]
                    ratings[tracks[idx]] = cat
                    # Stop playback before moving so we don't yank the file
                    # out from under gstreamer (inode survives on Linux, but
                    # this is cleaner and matches the existing play-next flow).
                    player.stop()
                    move_to_category(tracks[idx], cat)
                    save_ratings(ratings, tracks)
                    nxt = next_new(tracks, ratings, idx)
                    if nxt < 0:
                        sys.stdout.write("\n\nAll tracks categorized.\n")
                        break
                    idx = nxt; history.append(idx)
                    player.play(find_track(tracks[idx]))
                elif k == " ":
                    player.toggle_pause()
    finally:
        player.stop()
        save_ratings(ratings, tracks)
        sys.stdout.write("\n\n")
        counts = {c: 0 for c in CATEGORIES}
        for n in tracks: counts[ratings.get(n, DEFAULT)] += 1
        print(f"Saved ratings to {RATINGS}")
        print("  " + "  ".join(f"{counts[c]} {c}" for c in CATEGORIES))


if __name__ == "__main__":
    main()

"""Terminal music curator.

Walks every track under music/tracks/ and lets you rate each one while it
plays. Keys:
    .   next — skip to the next undecided track (auto-queue)
    ,   back — previous track in history (lets you change a past rating)
    +   like current, advance
    -   dislike current, advance
    space  pause/resume
    q   quit

Ratings persist to music/ratings.tsv — one line per track:
    +<TAB>filename      liked
    -<TAB>filename      disliked
    ?<TAB>filename      undecided (default for unheard tracks)

Re-running the jukebox reloads ratings.tsv and resumes from the first
undecided track.
"""
from __future__ import annotations
import os, sys, termios, tty, select, subprocess, signal, time

ROOT    = os.path.dirname(os.path.abspath(__file__))
TRACKS  = os.path.join(ROOT, "tracks")
RATINGS = os.path.join(ROOT, "ratings.tsv")
EXTS    = (".mp3", ".ogg", ".wav", ".flac")
PLAYER  = ["gst-play-1.0", "--quiet"]


def discover_tracks():
    return sorted(
        f for f in os.listdir(TRACKS)
        if f.lower().endswith(EXTS) and os.path.isfile(os.path.join(TRACKS, f))
    )


def load_ratings():
    r = {}
    if os.path.exists(RATINGS):
        with open(RATINGS) as f:
            for line in f:
                line = line.rstrip("\n")
                if not line or "\t" not in line: continue
                tag, name = line.split("\t", 1)
                if tag in ("+", "-", "?"): r[name] = tag
    return r


def save_ratings(ratings, all_tracks):
    liked    = sum(1 for n in all_tracks if ratings.get(n) == "+")
    disliked = sum(1 for n in all_tracks if ratings.get(n) == "-")
    undecided = len(all_tracks) - liked - disliked
    header = (
        f"# music ratings — {liked} liked / {disliked} disliked / "
        f"{undecided} undecided / {len(all_tracks)} total\n"
        f"# format: <rating>\\t<filename>   rating is +, -, or ?\n"
    )
    with open(RATINGS, "w") as f:
        f.write(header)
        for n in all_tracks:
            f.write(f"{ratings.get(n, '?')}\t{n}\n")


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


def next_undecided(tracks, ratings, from_idx):
    n = len(tracks)
    for step in range(1, n + 1):
        j = (from_idx + step) % n
        if ratings.get(tracks[j], "?") == "?":
            return j
    return -1


def status_line(tracks, ratings, idx, paused):
    name = tracks[idx]
    rating = ratings.get(name, "?")
    liked    = sum(1 for n in tracks if ratings.get(n) == "+")
    disliked = sum(1 for n in tracks if ratings.get(n) == "-")
    undecided = len(tracks) - liked - disliked
    flag = "[PAUSED] " if paused else ""
    display = name if len(name) <= 78 else name[:75] + "..."
    return (
        f"\r\x1b[K{flag}[{idx+1}/{len(tracks)}] {rating}  {display}\n"
        f"\x1b[K  +{liked} / -{disliked} / ?{undecided}   "
        f"keys: . next  , back  + like  - dislike  space pause  q quit\n\x1b[2A"
    )


def main():
    if not os.path.isdir(TRACKS):
        print(f"error: {TRACKS} does not exist. Run `make music` first.")
        sys.exit(1)
    tracks = discover_tracks()
    if not tracks:
        print(f"error: no audio files in {TRACKS}. Run `make music` first.")
        sys.exit(1)

    ratings = load_ratings()
    # Start at first undecided track, or index 0.
    start = next((i for i, t in enumerate(tracks) if ratings.get(t, "?") == "?"), 0)
    if start < 0: start = 0
    idx = start
    history = [idx]
    player = Player()
    player.play(os.path.join(TRACKS, tracks[idx]))

    fd = sys.stdin.fileno()
    last_render = ""
    try:
        with raw_mode(fd):
            sys.stdout.write("\n\n")  # reserve two lines
            while True:
                line = status_line(tracks, ratings, idx, player.paused)
                if line != last_render:
                    sys.stdout.write(line); sys.stdout.flush()
                    last_render = line

                k = readkey(fd, 0.2)

                # auto-advance on track finish
                if k is None and player.finished():
                    nxt = next_undecided(tracks, ratings, idx)
                    if nxt < 0:
                        sys.stdout.write("\n\nAll tracks rated.\n")
                        break
                    idx = nxt; history.append(idx)
                    player.play(os.path.join(TRACKS, tracks[idx]))
                    last_render = ""
                    continue

                if k is None: continue

                if k == "q" or k == "\x03":  # q or Ctrl+C
                    break
                elif k == ".":
                    nxt = next_undecided(tracks, ratings, idx)
                    if nxt < 0:
                        sys.stdout.write("\n\nAll tracks rated.\n")
                        break
                    idx = nxt; history.append(idx)
                    player.play(os.path.join(TRACKS, tracks[idx]))
                    last_render = ""
                elif k == ",":
                    if len(history) > 1:
                        history.pop()
                        idx = history[-1]
                        player.play(os.path.join(TRACKS, tracks[idx]))
                        last_render = ""
                elif k == "+" or k == "=":
                    ratings[tracks[idx]] = "+"
                    save_ratings(ratings, tracks)
                    nxt = next_undecided(tracks, ratings, idx)
                    if nxt < 0:
                        sys.stdout.write("\n\nAll tracks rated.\n")
                        break
                    idx = nxt; history.append(idx)
                    player.play(os.path.join(TRACKS, tracks[idx]))
                    last_render = ""
                elif k == "-" or k == "_":
                    ratings[tracks[idx]] = "-"
                    save_ratings(ratings, tracks)
                    nxt = next_undecided(tracks, ratings, idx)
                    if nxt < 0:
                        sys.stdout.write("\n\nAll tracks rated.\n")
                        break
                    idx = nxt; history.append(idx)
                    player.play(os.path.join(TRACKS, tracks[idx]))
                    last_render = ""
                elif k == " ":
                    player.toggle_pause()
                    last_render = ""
    finally:
        player.stop()
        save_ratings(ratings, tracks)
        sys.stdout.write("\n\n")
        liked    = sum(1 for n in tracks if ratings.get(n) == "+")
        disliked = sum(1 for n in tracks if ratings.get(n) == "-")
        print(f"Saved ratings to {RATINGS}")
        print(f"  +{liked} liked  -{disliked} disliked  ?{len(tracks)-liked-disliked} undecided")


if __name__ == "__main__":
    main()

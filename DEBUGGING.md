# ModCraft — Iterative Debugging Guide

This guide explains how to run, screenshot, and iterate on the game without
going through the menu each time.

## Quick launch (skip menu)

```bash
# Singleplayer, survival, skips main menu entirely
./build/modcraft --skip-menu

# Singleplayer, with F3 debug overlay on by default (press F3 in-game)
./build/modcraft --skip-menu

# Network client (also skips menu — auto-joins server directly)
./build/modcraft-client --host 127.0.0.1 --port 7777

# Dedicated server + client in one step (from Makefile)
make game 7890      # server on :7890 + client auto-joins, skips menu
make play           # same on default port 7777
```

## Automated screenshot pipeline

The game writes a screenshot to `/tmp/modcraft_auto_screenshot.ppm`
**3 seconds after connecting to a world** (survival or creative).

Convert and read:
```bash
python3 -c "
from PIL import Image
img = Image.open('/tmp/modcraft_auto_screenshot.ppm')
img.save('/tmp/shot.png')
print(img.size)
"
```

The `--demo` flag takes a full tour (FPS → TPS → RPG → RTS) and exits:
```bash
./build/modcraft --skip-menu --demo
# writes: /tmp/modcraft_view_1_fps.ppm  (FPS view)
#         /tmp/modcraft_view_2_3rd.ppm  (TPS view)
#         /tmp/modcraft_view_25_inventory.ppm
#         /tmp/modcraft_view_3_god.ppm  (RPG view)
#         /tmp/modcraft_view_4_rts.ppm  (RTS view)
```

Press **F2** in-game to take a manual screenshot:
```bash
ls /tmp/modcraft_screenshot_*.ppm
```

## Iterative HUD / rendering development loop

```bash
# Edit hud.cpp (or any source file)
cmake --build build -j$(nproc) && \
  pkill -f "build/modcraft" ; sleep 0.5 && \
  ./build/modcraft --skip-menu &
sleep 4
python3 -c "
from PIL import Image
Image.open('/tmp/modcraft_auto_screenshot.ppm').save('/tmp/shot.png')
"
# Claude can then read /tmp/shot.png directly
```

Or one-liner restart loop:
```bash
pkill -f "build/modcraft"; cmake --build build -j$(nproc) && \
  DISPLAY=:1 ./build/modcraft --skip-menu &
```

## In-game shortcuts

| Key     | Action                          |
|---------|---------------------------------|
| F2      | Screenshot → /tmp/modcraft_screenshot_N.ppm |
| F3      | Toggle debug overlay (FPS, XYZ, chunk, etc.) |
| F12     | Toggle admin/survival mode      |
| V       | Cycle camera mode (FPS→TPS→RPG→RTS) |
| Tab     | Toggle inventory panel          |
| Esc     | Back to menu / pause            |

## Useful debug overlay fields (F3)

- **FPS** — rendering performance
- **XYZ** — player position
- **Chunk** — current chunk coordinates
- **Entities / Particles** — active counts
- **Time / Sun** — world time (0–1) and sun brightness
- **Block** — name + coords of block under crosshair

## Claude screenshot workflow

Because the game window may be partially off-screen, use the in-process PPM
approach rather than X11 screenshot capture:

```python
from PIL import Image
import subprocess, time

# Rebuild
subprocess.run(["cmake", "--build", "build", "-j4"], cwd="/home/haoxuw/workspace/ModCraft")

# Kill old + launch new (headless-friendly)
subprocess.Popen(["pkill", "-f", "build/modcraft"])
time.sleep(0.5)
import os
env = {**os.environ, "DISPLAY": ":1"}
subprocess.Popen(["./build/modcraft", "--skip-menu"], env=env,
                 cwd="/home/haoxuw/workspace/ModCraft")

# Wait for auto-screenshot
time.sleep(4.5)
img = Image.open("/tmp/modcraft_auto_screenshot.ppm")
img.save("/tmp/shot.png")
# Now Claude can read /tmp/shot.png
```

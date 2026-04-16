# CivCraft — Iterative Debugging Guide

This guide explains how to run, screenshot, and iterate on the game without
going through the menu each time.

## Quick launch (skip menu)

```bash
# Singleplayer, survival, skips main menu entirely
./build/civcraft-ui --skip-menu

# Singleplayer, with F3 debug overlay on by default (press F3 in-game)
./build/civcraft-ui --skip-menu

# Network client (also skips menu — auto-joins server directly)
./build/civcraft-ui --host 127.0.0.1 --port 7777

# Dedicated server + client in one step (from Makefile)
make game                    # server on random port + client auto-joins, skips menu
make game GAME_PORT=7890     # same, on a fixed port
```

## Automated screenshot pipeline

The game writes a screenshot to `/tmp/civcraft_auto_screenshot.ppm`
**3 seconds after connecting to a world** (survival or creative).

Convert and read:
```bash
python3 -c "
from PIL import Image
img = Image.open('/tmp/civcraft_auto_screenshot.ppm')
img.save('/tmp/shot.png')
print(img.size)
"
```

Press **F2** in-game to take a manual screenshot:
```bash
ls /tmp/civcraft_screenshot_*.ppm
```

## Iterative HUD / rendering development loop

```bash
# Edit hud.cpp (or any source file)
cmake --build build -j$(nproc) && \
  pkill -f "build/civcraft-ui" ; sleep 0.5 && \
  ./build/civcraft-ui --skip-menu &
sleep 4
python3 -c "
from PIL import Image
Image.open('/tmp/civcraft_auto_screenshot.ppm').save('/tmp/shot.png')
"
# Claude can then read /tmp/shot.png directly
```

Or one-liner restart loop:
```bash
pkill -f "build/civcraft-ui"; cmake --build build -j$(nproc) && \
  DISPLAY=:1 ./build/civcraft-ui --skip-menu &
```

## In-game shortcuts

| Key     | Action                          |
|---------|---------------------------------|
| F2      | Screenshot → /tmp/civcraft_screenshot_N.ppm |
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
subprocess.run(["cmake", "--build", "build", "-j4"], cwd="/home/haoxuw/workspace/CivCraft")

# Kill old + launch new (headless-friendly)
subprocess.Popen(["pkill", "-f", "build/civcraft-ui"])
time.sleep(0.5)
import os
env = {**os.environ, "DISPLAY": ":1"}
subprocess.Popen(["./build/civcraft-ui", "--skip-menu"], env=env,
                 cwd="/home/haoxuw/workspace/CivCraft")

# Wait for auto-screenshot
time.sleep(4.5)
img = Image.open("/tmp/civcraft_auto_screenshot.ppm")
img.save("/tmp/shot.png")
# Now Claude can read /tmp/shot.png
```
